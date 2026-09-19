#!/usr/bin/env bash
set -Eeuo pipefail

run_root="${1:-}"
windows_disk="${2:-}"
workflow="${3:-Replay}"
if [[ "$workflow" != "Replay" && "$workflow" != "BootNext" ]]; then
    echo "Unsupported nested Windows workflow." >&2
    exit 2
fi
if [[ ! "$run_root" =~ ^/home/[a-z_][a-z0-9_-]*/[A-Za-z0-9_./-]+/runs/uefi-linux-(nested-windows-e2e|bootnext-e2e)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$ &&
      ! "$run_root" =~ ^/mnt/[a-z]/[A-Za-z0-9_./-]+/UEFILoader/\.analysis/host-runner/runs/uefi-linux-(nested-windows-e2e|bootnext-e2e)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}/stage$ ]]; then
    echo "Unsafe or missing run root." >&2
    exit 2
fi
if [[ ! "$windows_disk" =~ ^/mnt/hgfs/UEFILoaderNestedE2E/uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.vmdk$ &&
      ! "$windows_disk" =~ ^/mnt/[a-z]/Users/[A-Za-z0-9._-]+/AppData/Local/UEFILoader/PreparedDisks/uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.vmdk$ ]]; then
    echo "Unsafe nested Windows disk path." >&2
    exit 2
fi

run_id="$(basename "$run_root")"
if [[ "$run_id" == "stage" ]]; then
    run_id="$(basename "$(dirname "$run_root")")"
fi
result_path="$run_root/result.json"
status="FAIL"
message="Nested Windows E2E failed."
swtpm_pid=""
data_socket=""
control_socket=""
monitor_socket=""

finish() {
    local rc=$?
    if [[ -n "$swtpm_pid" ]] && kill -0 "$swtpm_pid" 2>/dev/null; then
        kill "$swtpm_pid" 2>/dev/null || true
        wait "$swtpm_pid" 2>/dev/null || true
    fi
    [[ -n "$data_socket" ]] && rm -f -- "$data_socket"
    [[ -n "$control_socket" ]] && rm -f -- "$control_socket"
    [[ -n "$monitor_socket" ]] && rm -f -- "$monitor_socket"
    printf '{"SchemaVersion":1,"RunId":"%s","Status":"%s","Message":"%s","CompletedUtc":"%s"}\n' \
        "$run_id" "$status" "$message" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        > "$result_path"
    [[ "$status" == "PASS" ]] && exit 0
    (( rc == 0 )) && rc=1
    exit "$rc"
}
trap finish EXIT

cd "$run_root"
sha256sum -c SHA256SUMS.txt
for tool in git make gcc nasm iasl python3 qemu-system-x86_64 qemu-img \
    swtpm swtpm_setup tpm2_pcrread truncate parted mformat mmd mcopy pkg-config; do
    command -v "$tool" >/dev/null 2>&1 || {
        message="Required guest tool is missing: $tool"
        exit 3
    }
done
pkg-config --exists uuid || {
    message="The libuuid development package is missing."
    exit 3
}
[[ -r /dev/kvm && -w /dev/kvm ]] || {
    message="Guest account cannot access /dev/kvm."
    exit 3
}
[[ -r "$windows_disk" && -w "$windows_disk" ]] || {
    message="Prepared Windows VMDK is not read/write through HGFS."
    exit 3
}
qemu-img info --output=json "$windows_disk" > "$run_root/windows-disk-info.json"
grep -Fq '"format": "vmdk"' "$run_root/windows-disk-info.json"
grep -Fq '"parent-cid": 4294967295' "$run_root/windows-disk-info.json"

shell_efi="/usr/share/efi-shell-x64/shellx64.efi"
[[ -s "$shell_efi" ]] || {
    message="UEFI Shell image is missing."
    exit 3
}
cache_root="$HOME/.cache/uefiloader-host-runner"
edk2_root="$cache_root/edk2-stable202605"
mkdir -p "$cache_root"
if [[ ! -d "$edk2_root/.git" ]]; then
    git clone --branch edk2-stable202605 --depth 1 --recurse-submodules \
        --shallow-submodules https://github.com/tianocore/edk2.git "$edk2_root"
fi
cd "$edk2_root"
[[ "$(git rev-parse --short=7 edk2-stable202605)" == "b03a21a" ]] || {
    message="The cached EDK2 tag does not match b03a21a."
    exit 4
}
git reset --hard edk2-stable202605
git submodule update --init --recursive --depth 1
[[ "$edk2_root" == "$cache_root/edk2-stable202605" ]] || {
    message="Refuse unexpected EDK2 build-output path."
    exit 4
}
rm -rf -- "$edk2_root/Build/OvmfX64"
git apply --ignore-space-change --check "$run_root/edk2-no-measure.patch"
git apply --ignore-space-change "$run_root/edk2-no-measure.patch"
git diff --check
make -C BaseTools -j"$(nproc)" > "$run_root/edk2-build.log" 2>&1
rm -f -- Conf/BuildEnv.sh
set +u
. edksetup.sh BaseTools >> "$run_root/edk2-build.log" 2>&1
set -u
build -a X64 -t GCC -b RELEASE -p OvmfPkg/OvmfPkgX64.dsc \
    -D TPM2_ENABLE=TRUE -D TPM1_ENABLE=FALSE -D SECURE_BOOT_ENABLE=FALSE \
    -D SMM_REQUIRE=FALSE -D CC_MEASUREMENT_ENABLE=FALSE -D BUILD_SHELL=TRUE \
    >> "$run_root/edk2-build.log" 2>&1

ovmf_code="$edk2_root/Build/OvmfX64/RELEASE_GCC/FV/OVMF_CODE.fd"
ovmf_vars="$edk2_root/Build/OvmfX64/RELEASE_GCC/FV/OVMF_VARS.fd"
[[ -s "$ovmf_code" && -s "$ovmf_vars" ]] || {
    message="Patched OVMF output pair is missing."
    exit 5
}

runtime="$run_root/runtime"
rm -rf -- "$runtime"
mkdir -p "$runtime/swtpm-state"
socket_root="/tmp/uhr-${run_id##*-}"
data_socket="${socket_root}.sock"
control_socket="${data_socket}.ctrl"
monitor_socket="${socket_root}.mon"

reset_swtpm_state() {
    local log_path="$1"
    rm -rf -- "$runtime/swtpm-state"
    mkdir -p "$runtime/swtpm-state"
    swtpm_setup --tpm2 --tpm-state "$runtime/swtpm-state" \
        --pcr-banks sha256 --overwrite > "$log_path" 2>&1
}

start_swtpm() {
    local log_path="$1"
    local mode="${2:-qemu}"
    local startup="${3:-clear}"
    rm -f -- "$data_socket" "$control_socket"
    local -a swtpm_args=(
        socket --tpm2
        --tpmstate "dir=$runtime/swtpm-state"
        --ctrl "type=unixio,path=$control_socket"
    )
    if [[ "$mode" == "tools" ]]; then
        swtpm_args+=(--server "type=unixio,path=$data_socket")
        swtpm_args+=(--flags "not-need-init,startup-$startup")
    elif [[ "$mode" != "qemu" ]]; then
        message="Unsupported swtpm launch mode: $mode"
        exit 6
    fi
    swtpm "${swtpm_args[@]}" > "$log_path" 2>&1 &
    swtpm_pid=$!
    for _ in $(seq 1 100); do
        if [[ -S "$control_socket" ]] &&
           { [[ "$mode" == "qemu" ]] || [[ -S "$data_socket" ]]; }; then
            break
        fi
        sleep 0.1
    done
    if [[ ! -S "$control_socket" ]] ||
       { [[ "$mode" == "tools" ]] && [[ ! -S "$data_socket" ]]; }; then
        message="swtpm did not publish the sockets required for $mode mode."
        exit 6
    fi
    sleep 0.5
}

stop_swtpm() {
    if [[ -n "$swtpm_pid" ]] && kill -0 "$swtpm_pid" 2>/dev/null; then
        kill "$swtpm_pid" 2>/dev/null || true
        wait "$swtpm_pid" 2>/dev/null || true
    fi
    swtpm_pid=""
    rm -f -- "$data_socket" "$control_socket"
}

make_partitioned_fat() {
    local image="$1"
    local label="$2"
    truncate -s 130M "$image"
    parted -s "$image" mklabel msdos
    parted -s "$image" mkpart primary fat32 1MiB 129MiB
    parted -s "$image" set 1 boot on
    mformat -i "$image@@1048576" -F -T 262144 -v "$label" ::
}

windows_overlay="$runtime/windows-overlay.qcow2"
qemu-img create -f qcow2 -F vmdk -b "$windows_disk" \
    "$windows_overlay" > "$run_root/windows-overlay-create.txt"

condition_image="$runtime/conditioning.img"
condition_volume="$condition_image@@1048576"
make_partitioned_fat "$condition_image" UEFICOND
printf 'schema=1\n' > "$runtime/UEFILoader-Nested-Condition.marker"
mcopy -i "$condition_volume" \
    "$runtime/UEFILoader-Nested-Condition.marker" \
    ::/UEFILoader-Nested-Condition.marker
cp "$ovmf_vars" "$runtime/CONDITION_VARS.fd"
reset_swtpm_state "$run_root/conditioning-swtpm-setup.log"
start_swtpm "$run_root/conditioning-swtpm.log" qemu
run_condition_qemu() {
    timeout --signal=TERM 600 qemu-system-x86_64 \
        -machine q35,accel=kvm -cpu host -smp 2 -m 2048 \
        -drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
        -drive if=pflash,format=raw,file="$runtime/CONDITION_VARS.fd" \
        -chardev socket,id=chrtpm,path="$control_socket" \
        -tpmdev emulator,id=tpm0,chardev=chrtpm \
        -device tpm-tis,tpmdev=tpm0 \
        -drive if=none,id=condition,format=raw,file="$condition_image",cache=writeback,aio=threads \
        -device ide-hd,drive=condition,bootindex=2 \
        -drive if=none,id=windisk,format=qcow2,file="$windows_overlay",cache=writeback,aio=threads \
        -device nvme,drive=windisk,serial=UEFILoaderNestedE2E,bootindex=1 \
        -net none -display none -monitor none \
        -serial "file:$run_root/conditioning-serial.log" \
        -debugcon "file:$run_root/conditioning-debugcon.log" \
        -global isa-debugcon.iobase=0xe9 \
        2> "$run_root/conditioning-qemu-stderr.log"
}
set +e
run_condition_qemu
condition_rc=$?
if [[ $condition_rc -ne 0 ]] && grep -Fq \
    'tpm-emulator: Failed to send CMD_SET_DATAFD' \
    "$run_root/conditioning-qemu-stderr.log"; then
    cp "$run_root/conditioning-qemu-stderr.log" \
        "$run_root/conditioning-qemu-first-attempt-stderr.log"
    stop_swtpm
    rm -f -- "$windows_overlay"
    reset_swtpm_state "$run_root/conditioning-swtpm-retry-setup.log"
    qemu-img create -f qcow2 -F vmdk -b "$windows_disk" \
        "$windows_overlay" >> "$run_root/windows-overlay-create.txt"
    cp "$ovmf_vars" "$runtime/CONDITION_VARS.fd"
    start_swtpm "$run_root/conditioning-swtpm-retry.log" qemu
    run_condition_qemu
    condition_rc=$?
fi
set -e
if [[ $condition_rc -ne 0 ]]; then
    message="Windows QEMU conditioning failed or timed out with exit code $condition_rc."
    exit 6
fi
mcopy -i "$condition_volume" ::/condition-result.json \
    "$run_root/condition-result.json"
python3 - "$run_root/condition-result.json" <<'PY'
import json
import pathlib
import sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
if result.get("SchemaVersion") != 1 or result.get("Status") != "PASS":
    raise SystemExit(f"Windows conditioning result is not PASS: {result}")
print("PASS Windows QEMU hardware conditioning")
PY

if [[ "$workflow" == "BootNext" ]]; then
    stop_swtpm
    reset_swtpm_state "$run_root/bootnext-swtpm-setup.log"
    start_swtpm "$run_root/bootnext-swtpm-first.log" qemu

    bootnext_image="$runtime/bootnext-evidence.img"
    bootnext_volume="$bootnext_image@@1048576"
    make_partitioned_fat "$bootnext_image" UEFIBOOTNEXT
    for file in ib-install.exe loader.efi shimx64.efi \
        nested-SHA256SUMS.txt \
        UEFILoader-Nested-BootNext-Setup.marker; do
        mcopy -i "$bootnext_volume" "$run_root/$file" "::/$file"
    done
    cp "$ovmf_vars" "$runtime/BOOTNEXT_VARS.fd"

    run_bootnext_qemu() {
        local serial_log="$1"
        local debug_log="$2"
        local stderr_log="$3"
        timeout --signal=TERM 900 qemu-system-x86_64 \
            -machine q35,accel=kvm -cpu host -smp 2 -m 2048 \
            -drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
            -drive if=pflash,format=raw,file="$runtime/BOOTNEXT_VARS.fd" \
            -chardev socket,id=chrtpm,path="$control_socket" \
            -tpmdev emulator,id=tpm0,chardev=chrtpm \
            -device tpm-tis,tpmdev=tpm0 \
            -drive if=none,id=evidence,format=raw,file="$bootnext_image",cache=writeback,aio=threads \
            -device ide-hd,drive=evidence,bootindex=2 \
            -drive if=none,id=windisk,format=qcow2,file="$windows_overlay",cache=writeback,aio=threads \
            -device nvme,drive=windisk,serial=UEFILoaderBootNext,bootindex=1 \
            -net none -display none -monitor none \
            -serial "file:$serial_log" \
            -debugcon "file:$debug_log" \
            -global isa-debugcon.iobase=0xe9 \
            2> "$stderr_log"
    }

    set +e
    run_bootnext_qemu "$run_root/bootnext-first-serial.log" \
        "$run_root/bootnext-first-debugcon.log" \
        "$run_root/bootnext-first-qemu-stderr.log"
    bootnext_first_rc=$?
    set -e
    if [[ $bootnext_first_rc -ne 0 ]]; then
        message="BootNext setup boot failed or timed out with exit code $bootnext_first_rc."
        exit 7
    fi
    mcopy -i "$bootnext_volume" ::/bootnext-setup-result.json \
        "$run_root/bootnext-setup-result.json"
    python3 - "$run_root/bootnext-setup-result.json" <<'PY'
import json
import pathlib
import sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
if result.get("SchemaVersion") != 1 or result.get("Status") != "PASS":
    raise SystemExit(f"BootNext setup result is not PASS: {result}")
print("PASS Windows set BootNext")
PY

    stop_swtpm
    start_swtpm "$run_root/bootnext-swtpm-second.log" qemu
    set +e
    run_bootnext_qemu "$run_root/bootnext-second-serial.log" \
        "$run_root/bootnext-second-debugcon.log" \
        "$run_root/bootnext-second-qemu-stderr.log"
    bootnext_second_rc=$?
    set -e
    if [[ $bootnext_second_rc -ne 0 ]]; then
        message="BootNext consumption boot failed or timed out with exit code $bootnext_second_rc."
        exit 7
    fi
    stop_swtpm
    for file in bootnext-result.json bootnext-set.stdout.txt \
        bootnext-set.stderr.txt bootnext-clear.stdout.txt \
        bootnext-clear.stderr.txt bootnext-firmware-before.txt \
        bootnext-firmware-after-set.txt \
        bootnext-firmware-before-cleanup.txt \
        bootnext-firmware-after-cleanup.txt \
        bootnext-bootmgfw-before.sha256 bootnext-bootmgfw-after.sha256 \
        bootnext-handoff-status.txt; do
        mcopy -i "$bootnext_volume" "::/$file" "$run_root/$file"
    done
    python3 - "$run_root/bootnext-result.json" <<'PY'
import json
import pathlib
import sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
if result.get("SchemaVersion") != 1 or result.get("Status") != "PASS":
    raise SystemExit(f"BootNext result is not PASS: {result}")
print("PASS Windows verified BootNext consumption and cleanup")
PY
    grep -Fq 'loader production handoff: START' \
        "$run_root/bootnext-second-debugcon.log"
    grep -Fq 'ERROR: PCR banks not empty - aborting replay' \
        "$run_root/bootnext-second-debugcon.log"
    grep -Fq 'boot manager restore: PASS' \
        "$run_root/bootnext-second-debugcon.log"
    grep -Fq 'replay=ABORTED' "$run_root/bootnext-handoff-status.txt"
    grep -Fq 'BootNext set to owned Boot' \
        "$run_root/bootnext-set.stdout.txt"
    grep -Fq 'BootOrder was not changed.' \
        "$run_root/bootnext-set.stdout.txt"
    grep -Fq 'Owned BootNext/Boot#### state cleared' \
        "$run_root/bootnext-clear.stdout.txt"

    status="PASS"
    message="Windows set BootNext through the production installer; firmware consumed it once, the loader handed off to Windows, and BootOrder returned byte-for-byte unchanged."
    exit 0
fi

stop_swtpm
reset_swtpm_state "$run_root/swtpm-baseline-setup.log"
start_swtpm "$run_root/swtpm-baseline.log" tools clear
TPM2TOOLS_TCTI="swtpm:path=$data_socket" \
    tpm2_pcrread sha256:0,1,2,3,4,5,6,7 > "$run_root/baseline.txt"
if grep -Eq '0x0*[1-9A-Fa-f]' "$run_root/baseline.txt"; then
    message="Fresh swtpm SHA-256 PCR0-7 baseline is not zero."
    exit 6
fi
stop_swtpm
reset_swtpm_state "$run_root/swtpm-setup.log"
start_swtpm "$run_root/swtpm.log" qemu

fat_image="$runtime/evidence.img"
fat_volume="$fat_image@@1048576"
make_partitioned_fat "$fat_image" UEFIE2E
mmd -i "$fat_volume" ::/EFI
mmd -i "$fat_volume" ::/EFI/BOOT
mcopy -i "$fat_volume" "$shell_efi" ::/EFI/BOOT/BOOTX64.EFI
for file in loader.efi payload.efi tcglog.ib bootmgfw.sha256 \
    ib-tcg-dump.exe ibaudit.exe nested-SHA256SUMS.txt \
    UEFILoader-Nested-E2E.marker; do
    mcopy -i "$fat_volume" "$run_root/$file" "::/$file"
done
cat > "$runtime/startup.nsh" <<'EOF'
map -r
if not exist fs1:\EFI\Microsoft\Boot\bootmgfw.efi then
  echo E2E_STAGE=NO_WINDOWS_ESP
  reset -s
endif
fs1:
mkdir \EFI\insecure-boot
cp fs0:\loader.efi \EFI\insecure-boot\loader.efi
cp fs0:\payload.efi \EFI\insecure-boot\payload.efi
cp fs0:\tcglog.ib \EFI\insecure-boot\tcglog.ib
cp \EFI\Microsoft\Boot\bootmgfw.efi \EFI\Microsoft\Boot\bootmgfw.efi.bak
cp fs0:\bootmgfw.sha256 \EFI\Microsoft\Boot\bootmgfw.sha256
rm fs0:\startup.nsh
rm fs0:\EFI\BOOT\BOOTX64.EFI
echo E2E_STAGE=READY
\EFI\insecure-boot\loader.efi
EOF
mcopy -i "$fat_volume" "$runtime/startup.nsh" ::/startup.nsh
cp "$ovmf_vars" "$runtime/OVMF_VARS.fd"

set +e
timeout --signal=TERM 900 qemu-system-x86_64 \
    -machine q35,accel=kvm -cpu host -smp 2 -m 2048 \
    -drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
    -drive if=pflash,format=raw,file="$runtime/OVMF_VARS.fd" \
    -chardev socket,id=chrtpm,path="$control_socket" \
    -tpmdev emulator,id=tpm0,chardev=chrtpm \
    -device tpm-tis,tpmdev=tpm0 \
    -drive if=none,id=evidence,format=raw,file="$fat_image",cache=writeback,aio=threads \
    -device ide-hd,drive=evidence,bootindex=1 \
    -drive if=none,id=windisk,format=qcow2,file="$windows_overlay",cache=writeback,aio=threads \
    -device nvme,drive=windisk,serial=UEFILoaderNestedE2E,bootindex=2 \
    -net none -display none \
    -monitor "unix:$monitor_socket,server=on,wait=off" \
    -serial "file:$run_root/qemu-serial.log" \
    -debugcon "file:$run_root/qemu-debugcon.log" \
    -global isa-debugcon.iobase=0xe9 \
    2> "$run_root/qemu-stderr.log"
qemu_rc=$?
set -e
if [[ $qemu_rc -ne 0 ]]; then
    message="Nested Windows QEMU run failed or timed out with exit code $qemu_rc."
    exit 7
fi

stop_swtpm
for file in nested-result.json postboot-tcglog.ib postboot-tcglog-full.bin \
    postboot-probe.stdout.txt postboot-probe.stderr.txt \
    postboot-capture.stdout.txt postboot-capture.stderr.txt \
    postboot-audit-dump.stdout.txt postboot-audit-dump.stderr.txt \
    postboot-verify-pcrs.stdout.txt postboot-verify-pcrs.stderr.txt \
    divergence.ib handoff-status.txt; do
    mcopy -i "$fat_volume" "::/$file" "$run_root/$file"
done

grep -Fq 'E2E_STAGE=READY' "$run_root/qemu-serial.log"
grep -Fq 'PCR banks empty - replay may proceed' "$run_root/qemu-debugcon.log"
grep -Fq 'post-replay SHA256 PCR[0-7] exact: YES' "$run_root/qemu-debugcon.log"
grep -Fq 'replay and divergence: PASS' "$run_root/qemu-debugcon.log"
grep -Fq 'boot manager restore: PASS' "$run_root/qemu-debugcon.log"
grep -Fq 'payload launch: PASS' "$run_root/qemu-debugcon.log"
grep -Fq 'state=READY_BOOT_MANAGER' "$run_root/handoff-status.txt"
grep -Fq 'replay=PASS' "$run_root/handoff-status.txt"
python3 - "$run_root/nested-result.json" <<'PY'
import json
import pathlib
import sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
if result.get("SchemaVersion") != 1 or result.get("Status") != "PASS":
    raise SystemExit(f"nested Windows result is not PASS: {result}")
print("PASS nested Windows startup result")
PY

status="PASS"
message="Patched OVMF replayed the genuine log, launched Windows, and Windows verified post-replay TPM state."
