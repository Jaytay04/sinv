#!/usr/bin/env bash
set -Eeuo pipefail

run_root="${1:-}"
mode="${2:-PositiveGate}"
if [[ ! "$run_root" =~ ^/home/[a-z_][a-z0-9_-]*/[A-Za-z0-9_./-]+/runs/uefi-linux-(positive|hypervisor-smoke|hypervisor-post-ebs-smoke|hypervisor-post-ebs-interrupt-smoke|replay-dry-run|pe-rehash|divergence-manifest|handoff-mechanics|production-handoff|oneshot-cycle)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$ ]]; then
    echo "Unsafe or missing run root." >&2
    exit 2
fi
if [[ "$mode" != "PositiveGate" && "$mode" != "HypervisorSmoke" && \
      "$mode" != "HypervisorPostEbsSmoke" && "$mode" != "ReplayDryRun" && \
      "$mode" != "HypervisorPostEbsInterruptSmoke" && \
      "$mode" != "PeRehash" && "$mode" != "DivergenceManifest" && \
      "$mode" != "HandoffMechanics" && "$mode" != "ProductionHandoff" && \
      "$mode" != "OneShotCycle" ]]; then
    echo "Unsupported Linux workflow mode." >&2
    exit 2
fi

run_id="$(basename "$run_root")"
result_path="$run_root/result.json"
status="FAIL"
message="Linux $mode workflow failed."
swtpm_pid=""
data_socket=""
control_socket=""

finish() {
    local rc=$?
    if [[ -n "$swtpm_pid" ]] && kill -0 "$swtpm_pid" 2>/dev/null; then
        kill "$swtpm_pid" 2>/dev/null || true
        wait "$swtpm_pid" 2>/dev/null || true
    fi
    if [[ -n "$data_socket" && -n "$control_socket" ]]; then
        rm -f -- "$data_socket" "$control_socket"
    fi
    printf '{"SchemaVersion":1,"RunId":"%s","Status":"%s","Message":"%s","CompletedUtc":"%s"}\n' \
        "$run_id" "$status" "$message" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        > "$result_path"
    if [[ "$status" == "PASS" ]]; then
        exit 0
    fi
    if (( rc == 0 )); then
        rc=1
    fi
    exit "$rc"
}
trap finish EXIT

cd "$run_root"
sha256sum -c SHA256SUMS.txt

missing_tools=()
for tool in git make gcc nasm iasl python3 qemu-system-x86_64 swtpm \
    tpm2_pcrread truncate mkfs.vfat mmd mcopy mdir cut; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        missing_tools+=("$tool")
    fi
done
if [[ ! -f /usr/include/uuid/uuid.h ]]; then
    missing_tools+=("uuid-dev")
fi
if (( ${#missing_tools[@]} != 0 )); then
    message="Required guest tools are missing: ${missing_tools[*]}"
    exit 3
fi

if [[ ! -r /dev/kvm || ! -w /dev/kvm ]]; then
    message="Guest account cannot access /dev/kvm; enable nested virtualization and grant KVM group access."
    exit 3
fi

shell_efi="/usr/share/efi-shell-x64/shellx64.efi"
if [[ ! -f "$shell_efi" ]]; then
    message="UEFI Shell image is missing: $shell_efi"
    exit 3
fi

cache_root="$HOME/.cache/uefiloader-host-runner"
edk2_root="$cache_root/edk2-stable202605"
mkdir -p "$cache_root"

if [[ ! -d "$edk2_root/.git" ]]; then
    git clone \
        --branch edk2-stable202605 \
        --depth 1 \
        --recurse-submodules \
        --shallow-submodules \
        https://github.com/tianocore/edk2.git \
        "$edk2_root"
fi

cd "$edk2_root"
if [[ "$(git rev-parse --short=7 edk2-stable202605)" != "b03a21a" ]]; then
    message="The cached EDK2 tag does not match b03a21a."
    exit 4
fi

git reset --hard edk2-stable202605
git submodule update --init --recursive --depth 1
# Patched and stock OVMF builds cannot safely share generated objects. The
# source checkout is persistent, but this single target's build output is not.
if [[ "$edk2_root" != "$cache_root/edk2-stable202605" ]]; then
    message="Refuse unexpected EDK2 build-output path."
    exit 4
fi
rm -rf -- "$edk2_root/Build/OvmfX64"
if [[ "$mode" != "PeRehash" ]]; then
    git apply --ignore-space-change --check "$run_root/edk2-no-measure.patch"
    git apply --ignore-space-change "$run_root/edk2-no-measure.patch"
    git diff --check
fi

message="EDK2 BaseTools build failed."
make -C BaseTools -j"$(nproc)" > "$run_root/edk2-build.log" 2>&1
message="EDK2 environment setup failed."
# BuildEnv.sh is generated cache state, not source. A previously interrupted
# setup can leave a NUL-filled file that Bash refuses to source. Recreate it
# deterministically on every run while preserving the actual Conf templates.
rm -f -- Conf/BuildEnv.sh
set +u
. edksetup.sh BaseTools >> "$run_root/edk2-build.log" 2>&1
set -u
message="OVMF build failed."
build \
    -a X64 \
    -t GCC \
    -b RELEASE \
    -p OvmfPkg/OvmfPkgX64.dsc \
    -D TPM2_ENABLE=TRUE \
    -D TPM1_ENABLE=FALSE \
    -D SECURE_BOOT_ENABLE=FALSE \
    -D SMM_REQUIRE=FALSE \
    -D CC_MEASUREMENT_ENABLE=FALSE \
    -D BUILD_SHELL=TRUE \
    >> "$run_root/edk2-build.log" 2>&1

ovmf_code="$edk2_root/Build/OvmfX64/RELEASE_GCC/FV/OVMF_CODE.fd"
ovmf_vars="$edk2_root/Build/OvmfX64/RELEASE_GCC/FV/OVMF_VARS.fd"
if [[ ! -s "$ovmf_code" || ! -s "$ovmf_vars" ]]; then
    message="The OVMF CODE/VARS output pair is missing."
    exit 5
fi

runtime="$run_root/runtime"
rm -rf "$runtime"
mkdir -p "$runtime/swtpm-state"
socket_root="/tmp/uhr-${run_id##*-}"
data_socket="${socket_root}.sock"
control_socket="${data_socket}.ctrl"
rm -f -- "$data_socket" "$control_socket"

swtpm socket \
    --tpm2 \
    --tpmstate dir="$runtime/swtpm-state" \
    --server type=unixio,path="$data_socket" \
    --ctrl type=unixio,path="$control_socket" \
    --flags not-need-init,startup-clear \
    > "$run_root/swtpm.log" 2>&1 &
swtpm_pid=$!

for _ in $(seq 1 100); do
    [[ -S "$data_socket" && -S "$control_socket" ]] && break
    sleep 0.1
done
if [[ ! -S "$data_socket" || ! -S "$control_socket" ]]; then
    message="swtpm did not publish both sockets."
    exit 6
fi

message="Fresh swtpm PCR baseline read failed."
if ! TPM2TOOLS_TCTI="swtpm:path=$data_socket" \
        tpm2_pcrread sha256:0,1,2,3,4,5,6,7 \
        > "$run_root/baseline.txt" \
        2> "$run_root/pcrread-stderr.log"; then
    exit 7
fi
if grep -Eq '0x0*[1-9A-Fa-f]' "$run_root/baseline.txt"; then
    message="Fresh swtpm SHA-256 PCR 0-7 baseline is not zero."
    exit 7
fi

message="FAT boot image construction failed."
fat_image="$runtime/loader.img"
truncate -s 64M "$fat_image"
mkfs.vfat -F 32 "$fat_image" >/dev/null
mmd -i "$fat_image" ::/EFI
mmd -i "$fat_image" ::/EFI/BOOT
mcopy -i "$fat_image" "$shell_efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$fat_image" "$run_root/loader.efi" ::/loader.efi
if [[ "$mode" == "PositiveGate" ]]; then
    printf 'map -r\r\nfs0:\\loader.efi\r\nreset -s\r\n' > "$runtime/startup.nsh"
elif [[ "$mode" == "ReplayDryRun" ]]; then
    # A separate EFI application reads PCRs immediately after the replay
    # smoke target, while QEMU still owns the TPM transport.
    mcopy -i "$fat_image" "$run_root/pcrcheck.efi" ::/pcrcheck.efi
    printf 'map -r\r\nfs0:\\loader.efi\r\nfs0:\\pcrcheck.efi\r\nreset -s\r\n' \
        > "$runtime/startup.nsh"
elif [[ "$mode" == "PeRehash" ]]; then
    mmd -i "$fat_image" ::/EFI/Microsoft
    mmd -i "$fat_image" ::/EFI/Microsoft/Boot
    mcopy -i "$fat_image" "$run_root/bootmgfw.efi" \
        ::/EFI/Microsoft/Boot/bootmgfw.efi
    printf 'map -r\r\nfs0:\\loader.efi\r\nreset -s\r\n' > "$runtime/startup.nsh"
elif [[ "$mode" == "DivergenceManifest" ]]; then
    mmd -i "$fat_image" ::/EFI/insecure-boot
    printf 'map -r\r\nfs0:\\loader.efi\r\nreset -s\r\n' > "$runtime/startup.nsh"
else
    mmd -i "$fat_image" ::/EFI/insecure-boot
    mmd -i "$fat_image" ::/EFI/Microsoft
    mmd -i "$fat_image" ::/EFI/Microsoft/Boot
    mcopy -i "$fat_image" "$run_root/tcglog.ib" \
        ::/EFI/insecure-boot/tcglog.ib
    payload_source="$run_root/payload.efi"
    mcopy -i "$fat_image" "$payload_source" \
        ::/EFI/insecure-boot/payload.efi
    mcopy -i "$fat_image" "$run_root/replacement.efi" \
        ::/EFI/Microsoft/Boot/bootmgfw.efi
    mcopy -i "$fat_image" "$run_root/bootmgfw.efi.bak" \
        ::/EFI/Microsoft/Boot/bootmgfw.efi.bak
    printf '%s\r\n' "$(sha256sum "$run_root/bootmgfw.efi.bak" | \
        cut -d' ' -f1 | tr '[:lower:]' '[:upper:]')" \
        > "$runtime/bootmgfw.sha256"
    mcopy -i "$fat_image" "$runtime/bootmgfw.sha256" \
        ::/EFI/Microsoft/Boot/bootmgfw.sha256
    if [[ "$mode" == "OneShotCycle" ]]; then
        mcopy -i "$fat_image" "$run_root/grubx64.efi" \
            ::/EFI/Microsoft/Boot/grubx64.efi
        printf 'map -r\r\nfs0:\\EFI\\Microsoft\\Boot\\bootmgfw.efi\r\nreset -s\r\n' \
            > "$runtime/startup.nsh"
    else
        printf 'map -r\r\nfs0:\\loader.efi\r\nreset -s\r\n' > "$runtime/startup.nsh"
    fi
fi
mcopy -i "$fat_image" "$runtime/startup.nsh" ::/startup.nsh
mcopy -i "$fat_image" "$runtime/startup.nsh" ::/EFI/BOOT/startup.nsh
cp "$ovmf_vars" "$runtime/OVMF_VARS.fd"

message="Nested QEMU execution failed."
debug_log="$run_root/qemu-debugcon.log"
serial_log="$run_root/qemu-serial.log"
qemu_extra=()
if [[ "$mode" == "HypervisorPostEbsSmoke" || \
      "$mode" == "HypervisorPostEbsInterruptSmoke" ]]; then
    qemu_extra=(-device isa-debug-exit,iobase=0x501,iosize=0x04)
fi
set +e
timeout --signal=TERM 180 qemu-system-x86_64 \
    -machine q35,accel=kvm \
    -cpu host \
    -m 2048 \
    -drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
    -drive if=pflash,format=raw,file="$runtime/OVMF_VARS.fd" \
    -chardev socket,id=chrtpm,path="$control_socket" \
    -tpmdev emulator,id=tpm0,chardev=chrtpm \
    -device tpm-tis,tpmdev=tpm0 \
    -drive if=virtio,format=raw,file="$fat_image" \
    -boot order=c,menu=off \
    -net none \
    -display none \
    -monitor none \
    -serial "file:$serial_log" \
    -debugcon "file:$debug_log" \
    -global isa-debugcon.iobase=0xe9 \
    "${qemu_extra[@]}" \
    -no-reboot \
    2> "$run_root/qemu-stderr.log"
qemu_rc=$?
set -e
printf '%s\n' "$qemu_rc" > "$run_root/qemu-exit-code.txt"

if [[ "$mode" == "HypervisorPostEbsSmoke" ]]; then
    if [[ $qemu_rc -ne 33 ]]; then
        message="Post-ExitBootServices probe did not use the PASS debug-exit code; QEMU returned $qemu_rc."
        exit 8
    fi
    grep -Fxq '33' "$run_root/qemu-exit-code.txt"
elif [[ "$mode" == "HypervisorPostEbsInterruptSmoke" ]]; then
    # The guard writes 0x12 to isa-debug-exit; QEMU returns (0x12 << 1) | 1 = 37.
    if [[ $qemu_rc -ne 37 ]]; then
        message="Post-ExitBootServices guest-interrupt guard did not use the PASS debug-exit code; QEMU returned $qemu_rc."
        exit 8
    fi
    grep -Fxq '37' "$run_root/qemu-exit-code.txt"
elif [[ $qemu_rc -ne 0 ]]; then
    message="QEMU failed or timed out with exit code $qemu_rc."
    exit 8
fi
if [[ ! -f "$debug_log" || ! -f "$serial_log" ]]; then
    message="QEMU did not produce both debugcon and serial evidence files."
    exit 9
fi
if [[ ! -s "$debug_log" ]]; then
    message="loader.efi produced no QEMU debugcon evidence."
    exit 9
fi
if [[ "$mode" == "DivergenceManifest" || \
      "$mode" == "HandoffMechanics" || \
      "$mode" == "ProductionHandoff" ]]; then
    message="Divergence manifest extraction failed."
    mcopy -i "$fat_image" ::/EFI/insecure-boot/divergence.ib \
        "$run_root/divergence.ib"
    if [[ ! -s "$run_root/divergence.ib" ]]; then
        exit 13
    fi
    python3 - "$run_root/divergence.ib" > "$run_root/manifest-parse.txt" <<'PY'
import pathlib
import struct
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
if len(data) < 16:
    raise SystemExit("manifest header truncated")
magic, version, flags, count, payload_size = struct.unpack_from("<4sHHII", data)
if magic != b"IBDV" or version != 1 or flags != 0 or count != 1:
    raise SystemExit("manifest header mismatch")
if payload_size != len(data) - 16 or payload_size < 20:
    raise SystemExit("manifest payload mismatch")
record_size, event_index, pcr, event_type, digest_count = \
    struct.unpack_from("<IIIII", data, 16)
if record_size != payload_size or event_index != 0 or pcr != 0:
    raise SystemExit("manifest record identity mismatch")
if event_type != 0x80000008 or not 1 <= digest_count <= 4:
    raise SystemExit("manifest record content mismatch")
position = 36
expected_sizes = {0x0004: 20, 0x000B: 32, 0x000C: 48, 0x000D: 64}
for _ in range(digest_count):
    if position + 4 > len(data):
        raise SystemExit("digest header truncated")
    algorithm, digest_size = struct.unpack_from("<HH", data, position)
    position += 4
    if expected_sizes.get(algorithm) != digest_size or position + digest_size > len(data):
        raise SystemExit("digest record mismatch")
    position += digest_size
if position != len(data):
    raise SystemExit("manifest trailing bytes")
print(f"PASS IBDV records={count} event_index={event_index} pcr={pcr} type=0x{event_type:08X} digests={digest_count}")
PY

    # swtpm may close its data socket as soon as QEMU disconnects. Verify PCR
    # state in the EFI fixture while QEMU still owns the TPM transport.
    message="In-firmware post-extend PCR verification failed."
    grep -F 'post-extend SHA256 PCR0 nonzero: YES' "$debug_log" \
        > "$run_root/after-raw-extend.txt"
fi
if [[ "$mode" == "HandoffMechanics" || \
      "$mode" == "ProductionHandoff" || \
      "$mode" == "OneShotCycle" ]]; then
    message="Restored boot-manager extraction failed."
    mcopy -i "$fat_image" ::/EFI/Microsoft/Boot/bootmgfw.efi \
        "$run_root/final-bootmgfw.efi"
    expected_hash="$(sha256sum "$run_root/bootmgfw.efi.bak" | cut -d' ' -f1)"
    actual_hash="$(sha256sum "$run_root/final-bootmgfw.efi" | cut -d' ' -f1)"
    {
        printf 'expected=%s\n' "$expected_hash"
        printf 'actual=%s\n' "$actual_hash"
    } > "$run_root/restore-verify.txt"
    if [[ "$actual_hash" != "$expected_hash" ]]; then
        message="Restored boot-manager hash mismatch."
        exit 16
    fi
    if mcopy -i "$fat_image" ::/EFI/Microsoft/Boot/bootmgfw.efi.bak \
            "$run_root/unexpected-backup.efi" 2>/dev/null; then
        message="Successful restore left bootmgfw.efi.bak behind."
        exit 16
    fi
    if mcopy -i "$fat_image" ::/EFI/Microsoft/Boot/bootmgfw.efi.ibnew \
            "$run_root/unexpected-temp.efi" 2>/dev/null; then
        message="Successful restore left its temporary file behind."
        exit 16
    fi
    if mcopy -i "$fat_image" ::/EFI/Microsoft/Boot/bootmgfw.efi.ibold \
            "$run_root/unexpected-displaced.efi" 2>/dev/null; then
        message="Successful restore left its displaced image behind."
        exit 16
    fi
    mdir -i "$fat_image" ::/EFI/Microsoft/Boot \
        > "$run_root/fat-directory.txt"
    if [[ "$mode" == "ProductionHandoff" ]]; then
        mcopy -i "$fat_image" ::/EFI/insecure-boot/handoff-status.txt \
            "$run_root/handoff-status.txt"
        grep -Fq 'schema=1' "$run_root/handoff-status.txt"
        grep -Fq 'state=READY_BOOT_MANAGER' "$run_root/handoff-status.txt"
        grep -Fq 'replay=PASS' "$run_root/handoff-status.txt"
        grep -Fq 'restore=PASS' "$run_root/handoff-status.txt"
        grep -Fq 'payload=PASS' "$run_root/handoff-status.txt"
        grep -Fq 'exit_boot_services_calls=0' "$run_root/handoff-status.txt"
    fi
    python3 - "$debug_log" "$mode" > "$run_root/handoff-order.txt" <<'PY'
import pathlib
import sys

text = pathlib.Path(sys.argv[1]).read_text(errors="strict")
mode = sys.argv[2]
if mode == "OneShotCycle":
    markers = [
        "loader production handoff: START",
        "ERROR: PCR banks not empty - aborting replay",
        "boot manager restore: PASS",
        "payload launch: SKIPPED",
        "handoff boot manager: START",
        "handoff boot manager: PASS",
        "boot manager launch: PASS",
        "loader fail-closed handoff: PASS",
    ]
else:
    outer_start = ("loader production handoff: START"
                   if mode == "ProductionHandoff"
                   else "handoff mechanics gate: START")
    outer_pass = ("loader production handoff: PASS"
                  if mode == "ProductionHandoff"
                  else "handoff mechanics gate: PASS")
    markers = [
        outer_start,
        "reference log events: 00000001",
        "replay and divergence: PASS",
        "boot manager restore: PASS",
        "handoff payload: START",
        "handoff payload: PASS",
        "payload launch: PASS",
        "handoff boot manager: START",
        "handoff boot manager: PASS",
        "boot manager launch: PASS",
        outer_pass,
    ]
position = -1
for marker in markers:
    next_position = text.find(marker, position + 1)
    if next_position < 0:
        raise SystemExit(f"missing or out-of-order marker: {marker}")
    position = next_position
print("PASS handoff order: " + " -> ".join(markers))
PY
fi
if [[ "$mode" == "PositiveGate" ]]; then
    message="Loader positive-path evidence validation failed."
    grep -Fq 'EFI_TCG2_PROTOCOL: located' "$debug_log"
    grep -Fq 'TPM present: YES' "$debug_log"
    grep -Eq 'SHA256 .*PCR\[0-7\] 00000000' "$debug_log"
    if grep -Eq 'PCR\[0-7\] [01]*1' "$debug_log"; then
        message="At least one active PCR bank was populated before loader.efi."
        exit 10
    fi
    grep -Fq 'PCR banks empty - replay may proceed' "$debug_log"
    status="PASS"
    message="Empty-PCR positive path reached with TCG2 and SHA-256 active."
elif [[ "$mode" == "ReplayDryRun" ]]; then
    message="Replay EFI dry-run evidence validation failed."
    grep -Fq 'replay EFI dry-run: START' "$debug_log"
    grep -Eq 'active PCR banks: 0x[0-9A-Fa-f]*[2367ABEFabef]' "$debug_log"
    grep -Fq 'synthetic events: 00000003' "$debug_log"
    grep -Fq 'callback sequence: HRH' "$debug_log"
    grep -Eq 'raw active digests: 0000000[1-4]' "$debug_log"
    grep -Fq 'null DataToHash calls: 00000000' "$debug_log"
    grep -Fq 'PCR extensions performed: 00000000' "$debug_log"
    grep -Fq 'replay EFI dry-run: PASS' "$debug_log"
    if grep -Fq 'replay EFI dry-run: FAIL' "$debug_log"; then
        message="Replay EFI dry-run reported failure."
        exit 11
    fi
    message="Independent EFI PCR check failed after replay dry-run."
    grep -Fq 'EFI_TCG2_PROTOCOL: located' "$debug_log"
    grep -Eq 'SHA256 .*PCR\[0-7\] 00000000' "$debug_log"
    if grep -Eq 'PCR\[0-7\] [01]*1' "$debug_log"; then
        message="Replay EFI dry-run changed at least one active PCR bank."
        exit 12
    fi
    grep -Fq 'PCR banks empty - replay may proceed' "$debug_log"
    grep -E 'PCR\[0-7\]|gate verdict' "$debug_log" \
        > "$run_root/after-pcr-check.txt"
    status="PASS"
    message="Synthetic replay core completed under OVMF; independent EFI PCR check remained empty."
elif [[ "$mode" == "PeRehash" ]]; then
    message="PE pre-relocation rehash evidence validation failed."
    grep -Fq 'PE pre-relocation rehash gate: START' "$debug_log"
    grep -Eq 'active PCR banks: 0x[0-9A-Fa-f]*[2367ABEFabef]' "$debug_log"
    grep -Fq 'source: \EFI\Microsoft\Boot\bootmgfw.efi' "$debug_log"
    grep -Eq 'pre-relocation bytes: 0x0*[1-9A-Fa-f][0-9A-Fa-f]*' "$debug_log"
    grep -Eq 'verified active digests: 0x0*[1-5]' "$debug_log"
    grep -Fq 'HashLogExtendEvent flags: PE_COFF_IMAGE' "$debug_log"
    grep -Fq 'PE pre-relocation rehash gate: PASS' "$debug_log"
    if grep -Fq 'PE pre-relocation rehash gate: FAIL' "$debug_log"; then
        message="PE pre-relocation rehash gate reported failure."
        exit 13
    fi
    status="PASS"
    message="Real bootmgfw.efi pre-relocation bytes rehashed to every recorded active-bank digest."
elif [[ "$mode" == "DivergenceManifest" ]]; then
    message="Divergence manifest gate evidence validation failed."
    grep -Fq 'divergence manifest gate: START' "$debug_log"
    grep -Eq 'active PCR banks: 0x[0-9A-Fa-f]*[2367ABEFabef]' "$debug_log"
    grep -Fq 'synthetic events: 00000001' "$debug_log"
    grep -Fq 'PCR extensions performed: 00000001' "$debug_log"
    grep -Fq 'post-extend SHA256 PCR0 nonzero: YES' "$debug_log"
    grep -Fq 'manifest records: 00000001' "$debug_log"
    grep -Fq 'record event index: 00000000' "$debug_log"
    grep -Fq 'record PCR: 00000000' "$debug_log"
    grep -Fq 'record type: 80000008' "$debug_log"
    grep -Eq 'record active digests: 0000000[1-4]' "$debug_log"
    grep -Fq 'manifest path: \EFI\insecure-boot\divergence.ib' "$debug_log"
    grep -Fq 'divergence manifest gate: PASS' "$debug_log"
    grep -Fq 'PASS IBDV records=1 event_index=0 pcr=0 type=0x80000008' \
        "$run_root/manifest-parse.txt"
    if grep -Fq 'divergence manifest gate: FAIL' "$debug_log"; then
        message="Divergence manifest gate reported failure."
        exit 15
    fi
    status="PASS"
    message="One raw-extended firmware-blob event produced exactly one persisted divergence record."
elif [[ "$mode" == "HandoffMechanics" ]]; then
    message="Handoff mechanics gate evidence validation failed."
    grep -Fq 'handoff mechanics gate: START' "$debug_log"
    grep -Fq 'reference log events: 00000001' "$debug_log"
    grep -Fq 'replay and divergence: PASS' "$debug_log"
    grep -Fq 'boot manager restore: PASS' "$debug_log"
    grep -Fq 'payload launch: PASS' "$debug_log"
    grep -Fq 'boot manager launch: PASS' "$debug_log"
    grep -Fq 'ExitBootServices calls: 00000000' "$debug_log"
    grep -Fq 'handoff mechanics gate: PASS' "$debug_log"
    grep -Fq 'PASS handoff order:' "$run_root/handoff-order.txt"
    if grep -Fq 'handoff mechanics gate: FAIL' "$debug_log"; then
        message="Handoff mechanics gate reported failure."
        exit 17
    fi
    status="PASS"
    message="Replay, verified restore, payload launch, and restored boot-manager launch completed in order."
elif [[ "$mode" == "HypervisorSmoke" ]]; then
    message="UEFI hypervisor smoke evidence validation failed."
    grep -Fq 'loader production handoff: START' "$debug_log"
    grep -Fq 'reference log events: 00000001' "$debug_log"
    grep -Fq 'replay and divergence: PASS' "$debug_log"
    grep -Fq 'boot manager restore: PASS' "$debug_log"
    grep -Fq 'uefi hypervisor: processor resources: BSP ONLY' "$debug_log"
    grep -Fq 'uefi hypervisor: processor state: BSP ONLY' "$debug_log"
    grep -Fq 'uefi hypervisor: VMX launch: PASS' "$debug_log"
    grep -Fq 'uefi hypervisor: BSP virtualization: PASS' "$debug_log"
    grep -Fq 'UEFI_HYPERVISOR_PROBE:PASS' "$debug_log"
    grep -Fq 'payload launch: PASS' "$debug_log"
    grep -Fq 'boot manager launch: PASS' "$debug_log"
    grep -Fq 'ExitBootServices calls: 00000000' "$debug_log"
    grep -Fq 'loader production handoff: PASS' "$debug_log"
    python3 - "$debug_log" > "$run_root/hypervisor-order.txt" <<'PY'
import pathlib
import sys

text = pathlib.Path(sys.argv[1]).read_text(errors="strict")
markers = [
    "loader production handoff: START",
    "replay and divergence: PASS",
    "boot manager restore: PASS",
    "uefi hypervisor: processor resources: BSP ONLY",
    "uefi hypervisor: processor state: BSP ONLY",
    "uefi hypervisor: VMX launch: PASS",
    "uefi hypervisor: BSP virtualization: PASS",
    "payload launch: PASS",
    "UEFI_HYPERVISOR_PROBE:PASS",
    "boot manager launch: PASS",
    "loader production handoff: PASS",
]
position = -1
for marker in markers:
    next_position = text.find(marker, position + 1)
    if next_position < 0:
        raise SystemExit(f"missing or out-of-order marker: {marker}")
    position = next_position
print("PASS hypervisor handoff order: " + " -> ".join(markers))
PY
    grep -Fq 'PASS hypervisor handoff order:' "$run_root/hypervisor-order.txt"
    status="PASS"
    message="UEFI hypervisor payload launched VMX on the BSP and returned through the loader production handoff."
elif [[ "$mode" == "HypervisorPostEbsSmoke" ]]; then
    message="UEFI hypervisor post-ExitBootServices evidence validation failed."
    grep -Fq 'loader production handoff: START' "$debug_log"
    grep -Fq 'reference log events: 00000001' "$debug_log"
    grep -Fq 'replay and divergence: PASS' "$debug_log"
    grep -Fq 'boot manager restore: PASS' "$debug_log"
    grep -Fq 'uefi hypervisor: processor resources: BSP ONLY' "$debug_log"
    grep -Fq 'uefi hypervisor: processor state: BSP ONLY' "$debug_log"
    grep -Fq 'uefi hypervisor: VMX launch: PASS' "$debug_log"
    grep -Fq 'uefi hypervisor: BSP virtualization: PASS' "$debug_log"
    grep -Fq 'payload launch: PASS' "$debug_log"
    grep -Fq 'UEFI_HYPERVISOR_NATIVE_DETACH:PASS' "$debug_log"
    grep -Fq 'UEFI_HYPERVISOR_POST_EBS_PROBE:PASS' "$debug_log"
    if grep -Fq 'UEFI_HYPERVISOR_POST_EBS_PROBE:FAIL' "$debug_log" ||
       grep -Fq 'uefi hypervisor: FATAL INTERRUPT' "$debug_log" ||
       grep -Eq '^[FIVX]:[0-9A-F]{8}$' "$debug_log"; then
        message="The post-ExitBootServices path emitted a hypervisor fatal marker."
        exit 19
    fi
    python3 - "$debug_log" > "$run_root/hypervisor-post-ebs-order.txt" <<'PY'
import pathlib
import sys

text = pathlib.Path(sys.argv[1]).read_text(errors="strict")
markers = [
    "loader production handoff: START",
    "replay and divergence: PASS",
    "boot manager restore: PASS",
    "uefi hypervisor: processor resources: BSP ONLY",
    "uefi hypervisor: processor state: BSP ONLY",
    "uefi hypervisor: VMX launch: PASS",
    "uefi hypervisor: BSP virtualization: PASS",
    "payload launch: PASS",
    "UEFI_HYPERVISOR_NATIVE_DETACH:PASS",
    "UEFI_HYPERVISOR_POST_EBS_PROBE:PASS",
]
position = -1
for marker in markers:
    next_position = text.find(marker, position + 1)
    if next_position < 0:
        raise SystemExit(f"missing or out-of-order marker: {marker}")
    position = next_position
print("PASS post-EBS hypervisor order: " + " -> ".join(markers))
PY
    grep -Fq 'PASS post-EBS hypervisor order:' \
        "$run_root/hypervisor-post-ebs-order.txt"
    status="PASS"
    message="The resident hypervisor handled its health CPUID after ExitBootServices, completed native VMXOFF, and terminated through the bounded debug-exit gate."
elif [[ "$mode" == "HypervisorPostEbsInterruptSmoke" ]]; then
    message="UEFI hypervisor post-ExitBootServices guest-interrupt guard evidence validation failed."
    grep -Fq 'loader production handoff: START' "$debug_log"
    grep -Fq 'reference log events: 00000001' "$debug_log"
    grep -Fq 'replay and divergence: PASS' "$debug_log"
    grep -Fq 'boot manager restore: PASS' "$debug_log"
    grep -Fq 'uefi hypervisor: processor resources: BSP ONLY' "$debug_log"
    grep -Fq 'uefi hypervisor: processor state: BSP ONLY' "$debug_log"
    grep -Fq 'uefi hypervisor: VMX launch: PASS' "$debug_log"
    grep -Fq 'uefi hypervisor: BSP virtualization: PASS' "$debug_log"
    grep -Fq 'payload launch: PASS' "$debug_log"
    grep -Fq 'UEFI_HYPERVISOR_GUEST_INTERRUPT_VECTOR:06' "$debug_log"
    grep -Fq 'UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS' "$debug_log"
    if grep -F 'UEFI_HYPERVISOR_GUEST_INTERRUPT_VECTOR:' "$debug_log" |
       grep -Fvq 'UEFI_HYPERVISOR_GUEST_INTERRUPT_VECTOR:06'; then
        message="The post-ExitBootServices probe trapped a guest interrupt other than the expected #UD vector 06."
        exit 19
    fi
    if grep -Fq 'UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:FAIL' "$debug_log" ||
       grep -Fq 'uefi hypervisor: FATAL INTERRUPT' "$debug_log" ||
       grep -Eq '^[FIVX]:[0-9A-Fa-f]{8}$' "$debug_log"; then
        message="The post-ExitBootServices guest-interrupt guard emitted a hypervisor fatal or failure marker."
        exit 19
    fi
    python3 - "$debug_log" > "$run_root/hypervisor-post-ebs-interrupt-order.txt" <<'PY'
import pathlib
import sys

text = pathlib.Path(sys.argv[1]).read_text(errors="strict")
markers = [
    "loader production handoff: START",
    "reference log events: 00000001",
    "replay and divergence: PASS",
    "boot manager restore: PASS",
    "uefi hypervisor: processor resources: BSP ONLY",
    "uefi hypervisor: processor state: BSP ONLY",
    "uefi hypervisor: VMX launch: PASS",
    "uefi hypervisor: BSP virtualization: PASS",
    "payload launch: PASS",
    "UEFI_HYPERVISOR_GUEST_INTERRUPT_VECTOR:06",
    "UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS",
]
position = -1
for marker in markers:
    next_position = text.find(marker, position + 1)
    if next_position < 0:
        raise SystemExit(f"missing or out-of-order marker: {marker}")
    position = next_position
print("PASS post-EBS guest-interrupt guard order: " + " -> ".join(markers))
PY
    grep -Fq 'PASS post-EBS guest-interrupt guard order:' \
        "$run_root/hypervisor-post-ebs-interrupt-order.txt"
    status="PASS"
    message="The resident hypervisor passed its post-ExitBootServices guest-interrupt guard and terminated through the bounded debug-exit gate."
elif [[ "$mode" == "OneShotCycle" ]]; then
    message="Shim one-shot fail-closed evidence validation failed."
    grep -Fq 'loader production handoff: START' "$debug_log"
    grep -Fq 'EFI_TCG2_PROTOCOL: located' "$debug_log"
    grep -Fq 'ERROR: PCR banks not empty - aborting replay' "$debug_log"
    grep -Fq 'boot manager restore: PASS' "$debug_log"
    grep -Fq 'payload launch: SKIPPED' "$debug_log"
    grep -Fq 'boot manager launch: PASS' "$debug_log"
    grep -Fq 'ExitBootServices calls: 00000000' "$debug_log"
    grep -Fq 'loader fail-closed handoff: PASS' "$debug_log"
    if grep -Fq 'replay and divergence: PASS' "$debug_log" ||
       grep -Fq 'payload launch: PASS' "$debug_log"; then
        message="Shim one-shot bypassed the non-empty PCR hard gate."
        exit 18
    fi
    grep -Fq 'PASS handoff order:' "$run_root/handoff-order.txt"
    status="PASS"
    message="Shim reached the loader; the non-empty PCR gate blocked replay, the backup was hash-restored, and normal boot handoff completed."
else
    message="Production loader handoff evidence validation failed."
    grep -Fq 'loader production handoff: START' "$debug_log"
    grep -Fq 'reference log events: 00000001' "$debug_log"
    grep -Fq 'replay and divergence: PASS' "$debug_log"
    grep -Fq 'boot manager restore: PASS' "$debug_log"
    grep -Fq 'payload launch: PASS' "$debug_log"
    grep -Fq 'boot manager launch: PASS' "$debug_log"
    grep -Fq 'ExitBootServices calls: 00000000' "$debug_log"
    grep -Fq 'loader production handoff: PASS' "$debug_log"
    grep -Fq 'state=READY_BOOT_MANAGER' "$run_root/handoff-status.txt"
    grep -Fq 'PASS handoff order:' "$run_root/handoff-order.txt"
    if grep -Fq 'loader production handoff: FAIL' "$debug_log"; then
        message="Production loader handoff reported failure."
        exit 18
    fi
    status="PASS"
    message="Production loader completed replay, verified restore, payload launch, and restored boot-manager launch in order."
fi
