#!/usr/bin/env bash
set -Eeuo pipefail

run_root="${1:-}"
windows_disk="${2:-}"
workflow="${3:-Replay}"
if [[ "$workflow" != "Replay" && "$workflow" != "BootNext" &&
      "$workflow" != "Hypervisor" ]]; then
    echo "Unsupported nested Windows workflow." >&2
    exit 2
fi
if [[ ! "$run_root" =~ ^/home/[a-z_][a-z0-9_-]*/[A-Za-z0-9_./-]+/runs/uefi-linux-(nested-windows-e2e|nested-windows-hypervisor-e2e|bootnext-e2e)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$ &&
      ! "$run_root" =~ ^/mnt/[a-z]/[A-Za-z0-9_./-]+/UEFILoader/\.analysis/host-runner/runs/uefi-linux-(nested-windows-e2e|nested-windows-hypervisor-e2e|bootnext-e2e)-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}/stage$ ]]; then
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
watchdog_pid=""
monitor_sampler_pid=""

finish() {
    local rc=$?
    if [[ -n "$swtpm_pid" ]] && kill -0 "$swtpm_pid" 2>/dev/null; then
        kill "$swtpm_pid" 2>/dev/null || true
        for _ in $(seq 1 20); do
            kill -0 "$swtpm_pid" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$swtpm_pid" 2>/dev/null; then
            kill -KILL "$swtpm_pid" 2>/dev/null || true
        fi
        wait "$swtpm_pid" 2>/dev/null || true
    fi
    [[ -n "$data_socket" ]] && rm -f -- "$data_socket"
    [[ -n "$control_socket" ]] && rm -f -- "$control_socket"
    [[ -n "$monitor_socket" ]] && rm -f -- "$monitor_socket"
    if [[ -n "$watchdog_pid" ]] && kill -0 "$watchdog_pid" 2>/dev/null; then
        kill "$watchdog_pid" 2>/dev/null || true
        wait "$watchdog_pid" 2>/dev/null || true
    fi
    if [[ -n "$monitor_sampler_pid" ]] &&
       kill -0 "$monitor_sampler_pid" 2>/dev/null; then
        kill "$monitor_sampler_pid" 2>/dev/null || true
        wait "$monitor_sampler_pid" 2>/dev/null || true
    fi
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
    swtpm swtpm_setup tpm2_pcrread truncate parted mformat mmd mcopy mdir; do
    command -v "$tool" >/dev/null 2>&1 || {
        message="Required guest tool is missing: $tool"
        exit 3
    }
done
[[ -f /usr/include/uuid/uuid.h ]] || {
    message="The libuuid development package is missing."
    exit 3
}
[[ -r /dev/kvm && -w /dev/kvm ]] || {
    message="Guest account cannot access /dev/kvm."
    exit 3
}
[[ -r "$windows_disk" ]] || {
    message="Prepared Windows VMDK is not readable through the selected transport."
    exit 3
}
condition_timeout=1200
windows_timeout=900
windows_smp=2
windows_smp_topology="cpus=2,sockets=1,cores=1,threads=2"
nested_memory_mb=3072
if [[ "$workflow" == "Hypervisor" ]]; then
    # Match the target PC's 20 logical processors in one Windows-supported
    # socket.  The EFI payload must virtualize the BSP and all 19 APs before
    # handoff; per-CPU health validation proves the resident CPUID contract on
    # every logical processor.  Nested VMX at this width needs a larger but
    # still bounded Windows startup budget.
    windows_smp=20
    windows_smp_topology="cpus=20,sockets=1,cores=10,threads=2"
    windows_timeout=1800
fi
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
[[ ! -L "$cache_root" ]] || {
    message="Runner cache root must not be a symlink."
    exit 4
}
mkdir -p "$cache_root"
runs_cache_root="$cache_root/runs"
if [[ -d "$runs_cache_root" && ! -L "$runs_cache_root" ]]; then
    for stale_run in "$runs_cache_root"/uefi-linux-*; do
        [[ -d "$stale_run" && ! -L "$stale_run" ]] || continue
        [[ "$stale_run" != "$run_root" ]] || continue
        stale_run_name="$(basename "$stale_run")"
        if [[ "$stale_run_name" =~ ^uefi-linux-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$ ]]; then
            rm -rf -- "$stale_run"
        fi
    done
fi
for legacy_condition_version in conditioned-v1 conditioned-v2 conditioned-v3; do
    legacy_condition_cache="$cache_root/$legacy_condition_version"
    if [[ -d "$legacy_condition_cache" && ! -L "$legacy_condition_cache" &&
          "$legacy_condition_cache" == "$cache_root/$legacy_condition_version" ]]; then
        rm -rf -- "$legacy_condition_cache"
    fi
done
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
        for _ in $(seq 1 20); do
            kill -0 "$swtpm_pid" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$swtpm_pid" 2>/dev/null; then
            kill -KILL "$swtpm_pid" 2>/dev/null || true
        fi
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

prepared_disk_id="$(basename "$windows_disk" .vmdk)"
prepared_cache_root="$cache_root/prepared-qcow2-v1"
prepared_cache_disk="$prepared_cache_root/$prepared_disk_id.qcow2"
prepared_cache_temporary="$prepared_cache_disk.tmp-$run_id"
[[ "$prepared_cache_root" == "$cache_root/prepared-qcow2-v1" ]] || {
    message="Prepared-disk cache path escaped its fixed root."
    exit 6
}
[[ ! -L "$prepared_cache_root" ]] || {
    message="Prepared-disk cache root must not be a symlink."
    exit 6
}
mkdir -p "$prepared_cache_root"
for stale_base in "$prepared_cache_root"/uefi-windows-capture-*.qcow2; do
    [[ -e "$stale_base" || -L "$stale_base" ]] || continue
    stale_base_name="$(basename "$stale_base")"
    if [[ "$stale_base_name" =~ ^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.qcow2$ &&
          "$stale_base" != "$prepared_cache_disk" ]]; then
        rm -f -- "$stale_base"
    fi
done

prepared_cache_is_valid() {
    local candidate="$1"
    local candidate_info="$runtime/prepared-cache-candidate-info.json"
    [[ -f "$candidate" && ! -L "$candidate" ]] || return 1
    qemu-img check -q "$candidate" || return 1
    qemu-img info --output=json "$candidate" > "$candidate_info" || return 1
    python3 - "$run_root/windows-disk-info.json" "$candidate_info" <<'PY'
import json
import pathlib
import sys

source = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
candidate = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
if candidate.get("format") != "qcow2":
    raise SystemExit(1)
if candidate.get("backing-filename") is not None:
    raise SystemExit(1)
if candidate.get("virtual-size") != source.get("virtual-size"):
    raise SystemExit(1)
PY
}

if prepared_cache_is_valid "$prepared_cache_disk"; then
    printf 'CACHE_VERSION=1\nPREPARED_DISK=%s\nFORMAT=qcow2\n' \
        "$prepared_disk_id" > "$run_root/windows-base-cache-hit.txt"
else
    rm -f -- "$prepared_cache_temporary"
    available_bytes="$(df --output=avail -B1 "$prepared_cache_root" | tail -n 1 | tr -d ' ')"
    minimum_bytes=$((40 * 1024 * 1024 * 1024))
    if [[ ! "$available_bytes" =~ ^[0-9]+$ ]] ||
       (( available_bytes < minimum_bytes )); then
        message="Linux guest needs at least 40 GiB free to publish the local prepared-disk cache; available bytes: $available_bytes."
        exit 6
    fi
    if ! qemu-img convert -p -c -f vmdk -O qcow2 \
            -o cluster_size=65536,lazy_refcounts=on \
            "$windows_disk" "$prepared_cache_temporary" \
            > "$run_root/windows-base-cache-convert.log" 2>&1; then
        rm -f -- "$prepared_cache_temporary"
        message="Prepared Windows VMDK could not be converted into the Linux-local cache."
        exit 6
    fi
    if ! prepared_cache_is_valid "$prepared_cache_temporary"; then
        rm -f -- "$prepared_cache_temporary"
        message="Linux-local prepared-disk cache failed validation before publication."
        exit 6
    fi
    mv -- "$prepared_cache_temporary" "$prepared_cache_disk"
    printf 'CACHE_VERSION=1\nPREPARED_DISK=%s\nFORMAT=qcow2\n' \
        "$prepared_disk_id" > "$run_root/windows-base-cache-published.txt"
fi
qemu-img info --output=json "$prepared_cache_disk" \
    > "$run_root/windows-base-cache-info.json"

windows_overlay="$runtime/windows-overlay.qcow2"
condition_cache_root="$cache_root/conditioned-v4"
condition_cache_overlay="$condition_cache_root/$prepared_disk_id.qcow2"
condition_cache_result="$condition_cache_root/$prepared_disk_id.result.json"
[[ "$condition_cache_root" == "$cache_root/conditioned-v4" ]] || {
    message="Conditioned-overlay cache path escaped its fixed root."
    exit 6
}
[[ ! -L "$condition_cache_root" ]] || {
    message="Conditioned-overlay cache root must not be a symlink."
    exit 6
}
mkdir -p "$condition_cache_root"
for stale_condition in "$condition_cache_root"/uefi-windows-capture-*; do
    [[ -e "$stale_condition" || -L "$stale_condition" ]] || continue
    stale_condition_name="$(basename "$stale_condition")"
    if [[ "$stale_condition_name" =~ ^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.(qcow2|result\.json)$ &&
          "$stale_condition" != "$condition_cache_overlay" &&
          "$stale_condition" != "$condition_cache_result" ]]; then
        rm -f -- "$stale_condition"
    fi
done
condition_result_is_pass() {
    python3 - "$1" <<'PY'
import json
import pathlib
import sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
if result.get("SchemaVersion") != 1 or result.get("Status") != "PASS":
    raise SystemExit(1)
PY
}
condition_cache_hit=false
if [[ -f "$condition_cache_overlay" && ! -L "$condition_cache_overlay" &&
      -f "$condition_cache_result" && ! -L "$condition_cache_result" ]] &&
   qemu-img check -q "$condition_cache_overlay" &&
   condition_result_is_pass "$condition_cache_result"; then
    qemu-img create -f qcow2 -F qcow2 -b "$condition_cache_overlay" \
        "$windows_overlay" > "$run_root/windows-overlay-create.txt"
    cp "$condition_cache_result" "$run_root/condition-result.json"
    printf 'CACHE_VERSION=4\nPREPARED_DISK=%s\nOVERLAY_CHECK=PASS\n' \
        "$prepared_disk_id" > "$run_root/conditioning-cache-hit.txt"
    condition_cache_hit=true
fi

if [[ "$condition_cache_hit" == false ]]; then
qemu-img create -f qcow2 -F qcow2 -b "$prepared_cache_disk" \
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
    timeout --signal=TERM "$condition_timeout" qemu-system-x86_64 \
        -machine q35,accel=kvm -cpu host -smp 2 -m "$nested_memory_mb" \
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
archive_condition_attempt() {
    local label="$1"
    local source
    for source in serial debugcon qemu-stderr; do
        if [[ -f "$run_root/conditioning-$source.log" ]]; then
            cp "$run_root/conditioning-$source.log" \
                "$run_root/conditioning-$label-$source.log"
        fi
    done
}
prepare_condition_retry() {
    stop_swtpm
    rm -f -- "$windows_overlay" "$condition_image" \
        "$run_root/condition-result.json"
    qemu-img create -f qcow2 -F qcow2 -b "$prepared_cache_disk" \
        "$windows_overlay" >> "$run_root/windows-overlay-create.txt"
    make_partitioned_fat "$condition_image" UEFICOND
    mcopy -i "$condition_volume" \
        "$runtime/UEFILoader-Nested-Condition.marker" \
        ::/UEFILoader-Nested-Condition.marker
    cp "$ovmf_vars" "$runtime/CONDITION_VARS.fd"
    reset_swtpm_state "$run_root/conditioning-swtpm-retry-setup.log"
    start_swtpm "$run_root/conditioning-swtpm-retry.log" qemu
}
condition_attempt=1
set +e
run_condition_qemu
condition_rc=$?
condition_result_ready=false
if mcopy -i "$condition_volume" \
        ::/condition-result.json "$run_root/condition-result.json" \
        >/dev/null 2>&1; then
    condition_result_ready=true
fi
if [[ $condition_attempt -eq 1 && "$condition_result_ready" == false ]]; then
    archive_condition_attempt first-attempt
    prepare_condition_retry
    run_condition_qemu
    condition_rc=$?
    condition_attempt=2
    condition_result_ready=false
    if mcopy -i "$condition_volume" \
            ::/condition-result.json "$run_root/condition-result.json" \
            >/dev/null 2>&1; then
        condition_result_ready=true
    fi
fi
set -e
mdir -i "$condition_volume" :: \
    > "$run_root/conditioning-fat-directory.txt" 2>&1 || true
if [[ "$condition_result_ready" == false ]]; then
    if [[ $condition_rc -ne 0 ]]; then
        message="Windows QEMU conditioning failed or timed out with exit code $condition_rc."
        exit 6
    fi
    condition_status="$(grep -Eo '0x[0-9A-Fa-f]{8}' \
        "$run_root/conditioning-serial.log" | tail -n 1 || true)"
    if [[ -n "$condition_status" ]]; then
        message="Windows conditioning exited without a result; boot manager reported $condition_status."
    else
        message="Windows conditioning exited without publishing condition-result.json."
    fi
    exit 6
fi
python3 - "$run_root/condition-result.json" <<'PY'
import json
import pathlib
import sys
result = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
if result.get("SchemaVersion") != 1 or result.get("Status") != "PASS":
    raise SystemExit(f"Windows conditioning result is not PASS: {result}")
print("PASS Windows QEMU hardware conditioning")
PY
qemu-img check -q "$windows_overlay"
if [[ $condition_rc -ne 0 ]]; then
    printf 'QEMU_EXIT_AFTER_PASS=%d\nOVERLAY_CHECK=PASS\n' "$condition_rc" \
        > "$run_root/conditioning-post-result-qemu-exit.txt"
fi
cache_overlay_temporary="$condition_cache_overlay.tmp-$run_id"
cache_result_temporary="$condition_cache_result.tmp-$run_id"
rm -f -- "$cache_overlay_temporary" "$cache_result_temporary"
mv -- "$windows_overlay" "$cache_overlay_temporary"
if ! qemu-img check -q "$cache_overlay_temporary"; then
    mv -- "$cache_overlay_temporary" "$windows_overlay"
    message="Conditioned overlay failed validation before cache publication."
    exit 6
fi
if ! cp "$run_root/condition-result.json" "$cache_result_temporary"; then
    mv -- "$cache_overlay_temporary" "$windows_overlay"
    message="Conditioning result could not be staged for cache publication."
    exit 6
fi
mv -f -- "$cache_result_temporary" "$condition_cache_result"
mv -f -- "$cache_overlay_temporary" "$condition_cache_overlay"
qemu-img create -f qcow2 -F qcow2 -b "$condition_cache_overlay" \
    "$windows_overlay" >> "$run_root/windows-overlay-create.txt"
printf 'CACHE_VERSION=4\nPREPARED_DISK=%s\nOVERLAY_CHECK=PASS\n' \
    "$prepared_disk_id" > "$run_root/conditioning-cache-published.txt"
fi

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
        timeout --signal=TERM "$windows_timeout" qemu-system-x86_64 \
            -machine q35,accel=kvm -cpu host -smp 2 -m "$nested_memory_mb" \
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

boot_image="$runtime/boot.img"
boot_volume="$boot_image@@1048576"
fat_image="$runtime/evidence.img"
fat_volume="$fat_image@@1048576"
postboot_evidence_files=(
    nested-result.json postboot-tcglog.ib postboot-tcglog-full.bin
    postboot-probe.stdout.txt postboot-probe.stderr.txt
    postboot-capture.stdout.txt postboot-capture.stderr.txt
    postboot-audit-dump.stdout.txt postboot-audit-dump.stderr.txt
    postboot-verify-pcrs.stdout.txt postboot-verify-pcrs.stderr.txt
    postboot-progress.txt divergence.ib handoff-status.txt
)
capture_partial_postboot_evidence() {
    mdir -i "$fat_volume" :: \
        > "$run_root/postboot-fat-directory.txt" 2>&1 || true
    for file in "${postboot_evidence_files[@]}"; do
        mcopy -o -i "$fat_volume" "::/$file" "$run_root/$file" \
            >/dev/null 2>&1 || true
    done
}
make_partitioned_fat "$fat_image" UEFIE2E
for file in ib-tcg-dump.exe ibaudit.exe nested-SHA256SUMS.txt \
    UEFILoader-Nested-E2E.marker; do
    mcopy -i "$fat_volume" "$run_root/$file" "::/$file"
done
make_partitioned_fat "$boot_image" UEFIBOOT
mmd -i "$boot_volume" ::/EFI
mmd -i "$boot_volume" ::/EFI/BOOT
mcopy -i "$boot_volume" "$shell_efi" ::/EFI/BOOT/BOOTX64.EFI
for file in loader.efi payload.efi tcglog.ib bootmgfw.sha256; do
    mcopy -i "$boot_volume" "$run_root/$file" "::/$file"
done
cat > "$runtime/startup.nsh" <<'EOF'
map -r
if not exist fs2:\EFI\Microsoft\Boot\bootmgfw.efi then
  echo E2E_STAGE=NO_WINDOWS_ESP
  reset -s
endif
fs2:
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
mcopy -i "$boot_volume" "$runtime/startup.nsh" ::/startup.nsh
cp "$ovmf_vars" "$runtime/OVMF_VARS.fd"

prepare_replay_attempt() {
    rm -f -- "$boot_image" "$fat_image" "$runtime/OVMF_VARS.fd"
    make_partitioned_fat "$fat_image" UEFIE2E
    for file in ib-tcg-dump.exe ibaudit.exe nested-SHA256SUMS.txt \
        UEFILoader-Nested-E2E.marker; do
        mcopy -i "$fat_volume" "$run_root/$file" "::/$file"
    done
    make_partitioned_fat "$boot_image" UEFIBOOT
    mmd -i "$boot_volume" ::/EFI
    mmd -i "$boot_volume" ::/EFI/BOOT
    mcopy -i "$boot_volume" "$shell_efi" ::/EFI/BOOT/BOOTX64.EFI
    for file in loader.efi payload.efi tcglog.ib bootmgfw.sha256; do
        mcopy -i "$boot_volume" "$run_root/$file" "::/$file"
    done
    mcopy -i "$boot_volume" "$runtime/startup.nsh" ::/startup.nsh
    cp "$ovmf_vars" "$runtime/OVMF_VARS.fd"
}

archive_replay_attempt() {
    local label="$1"
    local source
    for source in qemu-debugcon.log qemu-serial.log qemu-stderr.log \
        qemu-exit-code.txt qemu-register-snapshots.txt qemu-screen.ppm \
        postboot-fat-directory.txt; do
        if [[ -f "$run_root/$source" ]]; then
            cp "$run_root/$source" "$run_root/${label}-${source}"
        fi
    done
}

terminate_qemu_timeout_tree() {
    local watched_pid="$1"
    local child
    local -a children=()
    mapfile -t children < <(pgrep -P "$watched_pid" -f \
        '^qemu-system-x86_64 ' 2>/dev/null || true)
    for child in "${children[@]}"; do
        kill -TERM "$child" 2>/dev/null || true
    done
    kill -TERM "$watched_pid" 2>/dev/null || true
    for _ in $(seq 1 20); do
        local alive=0
        kill -0 "$watched_pid" 2>/dev/null && alive=1
        for child in "${children[@]}"; do
            kill -0 "$child" 2>/dev/null && alive=1
        done
        (( alive == 0 )) && return
        sleep 0.1
    done
    for child in "${children[@]}"; do
        kill -KILL "$child" 2>/dev/null || true
    done
    kill -KILL "$watched_pid" 2>/dev/null || true
}

watch_hypervisor_fatal() {
    local watched_pid="$1"
    local debug_log="$2"
    local marker_path="$3"
    while kill -0 "$watched_pid" 2>/dev/null; do
        if [[ -s "$debug_log" ]] &&
           grep -Eq 'uefi hypervisor: FATAL INTERRUPT|uefi hypervisor: AP virtualization: FAIL STOP|^[FIVX]:[0-9A-Fa-f]{8}$' \
               "$debug_log"; then
            grep -Em1 \
                'uefi hypervisor: FATAL INTERRUPT|uefi hypervisor: AP virtualization: FAIL STOP|^[FIVX]:[0-9A-Fa-f]{8}$' \
                "$debug_log" > "$marker_path"
            terminate_qemu_timeout_tree "$watched_pid"
            return
        fi
        sleep 0.25
    done
}

sample_qemu_registers() {
    local watched_pid="$1"
    local monitor_path="$2"
    local output_path="$3"
    local stall_path="$4"
    python3 - "$watched_pid" "$monitor_path" "$output_path" \
        "$stall_path" "$windows_smp" <<'PY'
import datetime
import os
import re
import signal
import socket
import sys
import time

pid = int(sys.argv[1])
monitor_path = sys.argv[2]
output_path = sys.argv[3]
stall_path = sys.argv[4]
expected_cpu_count = int(sys.argv[5])
screen_path = os.path.join(os.path.dirname(output_path), "qemu-screen.ppm")
previous_cpu_state = None
stable_samples = 0
kernel_diagnostics_collected = False

def terminate_timeout_tree():
    children = []
    try:
        with open(f"/proc/{pid}/task/{pid}/children", encoding="ascii") as f:
            for value in f.read().split():
                child = int(value)
                try:
                    with open(f"/proc/{child}/cmdline", "rb") as command:
                        if command.read().startswith(b"qemu-system-x86_64\0"):
                            children.append(child)
                except FileNotFoundError:
                    pass
    except FileNotFoundError:
        pass
    for target in children + [pid]:
        try:
            os.kill(target, signal.SIGTERM)
        except ProcessLookupError:
            pass
    time.sleep(2)
    for target in children + [pid]:
        try:
            os.kill(target, signal.SIGKILL)
        except ProcessLookupError:
            pass

def process_alive():
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False

for sample in range(1, 61):
    time.sleep(30)
    if not process_alive():
        break
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(1.0)
            client.connect(monitor_path)
            try:
                client.recv(65536)
            except TimeoutError:
                pass
            # Preserve every vCPU state in each sample.  CPU 0 remains first
            # so kernel diagnostics keep their deterministic reference CPU.
            monitor_commands = ["info cpus"]
            for cpu in range(expected_cpu_count):
                monitor_commands.extend(
                    [f"cpu {cpu}", "info registers", "info lapic"])
            monitor_commands.extend(["cpu 0", "info irq"])
            client.sendall(("\n".join(monitor_commands) + "\n").encode("ascii"))
            time.sleep(0.25)
            chunks = []
            while True:
                try:
                    chunk = client.recv(65536)
                except TimeoutError:
                    break
                if not chunk:
                    break
                chunks.append(chunk)
            response = b"".join(chunks).decode("utf-8", errors="replace")
            rip = re.search(r"RIP=([0-9A-Fa-f]+)", response)
            rsp = re.search(r"RSP=([0-9A-Fa-f]+)", response)
            rbp = re.search(r"RBP=([0-9A-Fa-f]+)", response)
            if rip:
                commands = [
                    f"x /64bx 0x{max(int(rip.group(1), 16) - 16, 0):x}"]
                if rsp:
                    commands.append(f"x /16gx 0x{int(rsp.group(1), 16):x}")
                if rbp:
                    commands.append(f"x /16gx 0x{int(rbp.group(1), 16):x}")
                commands.append(f"screendump {screen_path}")
                rip_value = int(rip.group(1), 16)
                if (not kernel_diagnostics_collected and
                        rip_value >= 0xffff800000000000):
                    # Windows loads ntoskrnl on a 64 KiB boundary; a 2 MiB
                    # scan misses valid randomized bases.  Sixteen MiB covers
                    # the bounded backwards distance from xHalHaltSystem in
                    # the pinned prepared-disk kernel while avoiding an
                    # unbounded monitor-memory search.
                    aligned_rip = rip_value & ~((1 << 16) - 1)
                    commands.extend(
                        f"x /2bx 0x{aligned_rip - offset:x}"
                        for offset in range(0, 16 << 20, 1 << 16))
                client.sendall(("\n".join(commands) + "\n").encode("ascii"))
                time.sleep(0.25)
                chunks = []
                while True:
                    try:
                        chunk = client.recv(65536)
                    except TimeoutError:
                        break
                    if not chunk:
                        break
                    chunks.append(chunk)
                response += b"".join(chunks).decode(
                    "utf-8", errors="replace")
                if (not kernel_diagnostics_collected and
                        rip_value >= 0xffff800000000000):
                    kernel_base = re.search(
                        r"(?m)^([0-9A-Fa-f]+): 0x4d 0x5a\s*$", response)
                    if kernel_base:
                        base = int(kernel_base.group(1), 16)
                        # The prepared Windows disk is pinned to
                        # ntkrnlmp.pdb/72C69E726C648BC18257AF38FA78A2F21.
                        # Its public KiBugCheckData symbol is at RVA
                        # 0xF22B60.  Read it before the larger diagnostic
                        # dump so the bugcheck survives a dump failure.
                        bugcheck_data = base + 0xF22B60
                        command = f"x /8gx 0x{bugcheck_data:x}\n"
                        client.sendall(command.encode("ascii"))
                        time.sleep(1.0)
                        chunks = []
                        while True:
                            try:
                                chunk = client.recv(65536)
                            except TimeoutError:
                                break
                            if not chunk:
                                break
                            chunks.append(chunk)
                        bugcheck_response = b"".join(chunks).decode(
                            "utf-8", errors="replace")
                        response += bugcheck_response
                        response += f"KERNEL_BASE=0x{base:x}\n"
                        response += (
                            "BUGCHECK_DATA_PDB="
                            "72C69E726C648BC18257AF38FA78A2F21\n"
                            f"BUGCHECK_DATA_ADDRESS=0x{bugcheck_data:x}\n")
                        memory_lines = re.findall(
                            r"(?m)^[0-9A-Fa-f]+:\s+"
                            r"((?:0x[0-9A-Fa-f]{16}\s*)+)$",
                            bugcheck_response)
                        bugcheck_values = []
                        for memory_line in memory_lines:
                            bugcheck_values.extend(
                                int(value, 16) for value in re.findall(
                                    r"0x([0-9A-Fa-f]{16})", memory_line))
                        if len(bugcheck_values) >= 5:
                            response += (
                                f"BUGCHECK_CODE=0x{bugcheck_values[0]:x}\n"
                                f"BUGCHECK_PARAMETER1=0x{bugcheck_values[1]:x}\n"
                                f"BUGCHECK_PARAMETER2=0x{bugcheck_values[2]:x}\n"
                                f"BUGCHECK_PARAMETER3=0x{bugcheck_values[3]:x}\n"
                                f"BUGCHECK_PARAMETER4=0x{bugcheck_values[4]:x}\n")
                            stack_pointer = bugcheck_values[1]
                            context_record = bugcheck_values[3]
                            exception_record = bugcheck_values[4]
                            diagnostic_commands = []
                            if stack_pointer >= 0xffff800000000100:
                                diagnostic_commands.append(
                                    f"x /32gx 0x{stack_pointer - 0x100:x}")
                            if context_record >= 0xffff800000000000:
                                diagnostic_commands.append(
                                    f"x /64gx 0x{context_record:x}")
                            if exception_record >= 0xffff800000000000:
                                diagnostic_commands.append(
                                    f"x /20gx 0x{exception_record:x}")
                            if diagnostic_commands:
                                client.sendall((
                                    "\n".join(diagnostic_commands) + "\n"
                                ).encode("ascii"))
                                time.sleep(1.0)
                                chunks = []
                                while True:
                                    try:
                                        chunk = client.recv(65536)
                                    except TimeoutError:
                                        break
                                    if not chunk:
                                        break
                                    chunks.append(chunk)
                                response += b"".join(chunks).decode(
                                    "utf-8", errors="replace")
                        kernel_diagnostics_collected = True
    except (FileNotFoundError, ConnectionError, OSError) as error:
        response = f"monitor sample failed: {error}\n"
    timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
    with open(output_path, "a", encoding="utf-8", newline="\n") as output:
        output.write(f"=== sample {sample} {timestamp} ===\n{response}\n")
    states = re.findall(
        r"CPU#(\d+).*?RIP=([0-9A-Fa-f]+).*?HLT=([01])", response, re.DOTALL)
    cpu_state = tuple(sorted(
        (int(cpu), rip.lower(), hlt) for cpu, rip, hlt in states))
    complete_state = (
        len(cpu_state) == expected_cpu_count and
        len({cpu for cpu, _, _ in cpu_state}) == expected_cpu_count)
    if complete_state and cpu_state == previous_cpu_state:
        stable_samples += 1
    else:
        previous_cpu_state = cpu_state if complete_state else None
        stable_samples = 1 if complete_state else 0
    if (complete_state and all(hlt == "0" for _, _, hlt in cpu_state) and
            stable_samples >= 3):
        stable_cpus = ",".join(
            f"{cpu}:{rip}" for cpu, rip, _ in cpu_state)
        with open(stall_path, "w", encoding="utf-8", newline="\n") as marker:
            marker.write(
                f"STABLE_CPUS={stable_cpus} SAMPLES={stable_samples} HLT=0\n")
        terminate_timeout_tree()
        break
PY
}

run_windows_qemu() {
    rm -f -- "$monitor_socket"
    timeout --signal=TERM "$windows_timeout" qemu-system-x86_64 \
        -machine q35,accel=kvm -cpu host -smp "$windows_smp_topology" -m "$nested_memory_mb" \
        -drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
        -drive if=pflash,format=raw,file="$runtime/OVMF_VARS.fd" \
        -chardev socket,id=chrtpm,path="$control_socket" \
        -tpmdev emulator,id=tpm0,chardev=chrtpm \
        -device tpm-tis,tpmdev=tpm0 \
        -drive if=none,id=boot,format=raw,file="$boot_image",cache=writeback,aio=threads \
        -device ide-hd,drive=boot,bus=ide.0,bootindex=1 \
        -drive if=none,id=evidence,format=raw,file="$fat_image",cache=writeback,aio=threads \
        -device ide-hd,drive=evidence,bus=ide.1,bootindex=3 \
        -drive if=none,id=windisk,format=qcow2,file="$windows_overlay",cache=writeback,aio=threads \
        -device nvme,drive=windisk,serial=UEFILoaderNestedE2E,bootindex=2 \
        -net none -display none \
        -monitor "unix:$monitor_socket,server=on,wait=off" \
        -serial "file:$run_root/qemu-serial.log" \
        -debugcon "file:$run_root/qemu-debugcon.log" \
        -global isa-debugcon.iobase=0xe9 \
        2> "$run_root/qemu-stderr.log" &
    qemu_pid=$!
    if [[ "$workflow" == "Hypervisor" ]]; then
        watch_hypervisor_fatal "$qemu_pid" "$run_root/qemu-debugcon.log" \
            "$run_root/qemu-fatal-marker.txt" &
        watchdog_pid=$!
        sample_qemu_registers "$qemu_pid" "$monitor_socket" \
            "$run_root/qemu-register-snapshots.txt" \
            "$run_root/qemu-stall-marker.txt" &
        monitor_sampler_pid=$!
    fi
    wait "$qemu_pid"
    qemu_rc=$?
    printf '%d\n' "$qemu_rc" > "$run_root/qemu-exit-code.txt"
    if [[ -n "$watchdog_pid" ]]; then
        wait "$watchdog_pid" 2>/dev/null || true
        watchdog_pid=""
    fi
    if [[ -n "$monitor_sampler_pid" ]]; then
        kill "$monitor_sampler_pid" 2>/dev/null || true
        wait "$monitor_sampler_pid" 2>/dev/null || true
        monitor_sampler_pid=""
    fi
}

hypervisor_launch_markers_complete() {
    [[ -s "$run_root/qemu-debugcon.log" ]] &&
        grep -Fq 'uefi hypervisor: processor topology: PASS' \
            "$run_root/qemu-debugcon.log" &&
        grep -Fq 'uefi hypervisor: AP VMX preflight: PASS' \
            "$run_root/qemu-debugcon.log" &&
        grep -Fq 'uefi hypervisor: processor resources: PASS' \
            "$run_root/qemu-debugcon.log" &&
        grep -Fq 'uefi hypervisor: processor state: PASS' \
            "$run_root/qemu-debugcon.log" &&
        grep -Fq 'uefi hypervisor: AP virtualization: PASS' \
            "$run_root/qemu-debugcon.log" &&
        grep -Fq 'uefi hypervisor: VMX launch: PASS' \
            "$run_root/qemu-debugcon.log" &&
        grep -Fq 'uefi hypervisor: BSP virtualization: PASS' \
            "$run_root/qemu-debugcon.log" &&
        grep -Fq 'payload launch: PASS' "$run_root/qemu-debugcon.log"
}

set +e
run_windows_qemu
set -e
stop_swtpm
capture_partial_postboot_evidence

# The nested Windows boot manager occasionally returns STATUS_INVALID_PARAMETER
# before the startup task mounts the evidence disk.  A freshly conditioned disk
# can also power off cleanly without rendering that status even though every
# loader/hypervisor launch marker completed.  Retry either attributable,
# pre-evidence, clean-QEMU outcome once, from fresh overlays, firmware variables,
# and TPM state.  All VMX fatal/stall signals and every other failure remain
# terminal and retain their first-attempt diagnostics.
replay_retry_reason=""
if [[ $qemu_rc -eq 0 &&
      ! -s "$run_root/qemu-fatal-marker.txt" &&
      ! -s "$run_root/qemu-stall-marker.txt" &&
      ! -f "$run_root/nested-result.json" &&
      -s "$run_root/qemu-serial.log" ]]; then
    if grep -Fq '0xc000000d' "$run_root/qemu-serial.log"; then
        replay_retry_reason="BOOT_MANAGER_0xC000000D"
    elif [[ "$workflow" == "Hypervisor" ]] &&
         hypervisor_launch_markers_complete; then
        replay_retry_reason="CLEAN_MARKER_COMPLETE_PRE_EVIDENCE_SHUTDOWN"
    fi
fi
if [[ -n "$replay_retry_reason" ]]; then
    archive_replay_attempt first-attempt
    for file in "${postboot_evidence_files[@]}"; do
        rm -f -- "$run_root/$file"
    done
    rm -f -- "$windows_overlay" "$run_root/qemu-debugcon.log" \
        "$run_root/qemu-serial.log" "$run_root/qemu-stderr.log" \
        "$run_root/qemu-exit-code.txt" \
        "$run_root/qemu-register-snapshots.txt" \
        "$run_root/qemu-screen.ppm" "$run_root/postboot-fat-directory.txt" \
        "$run_root/qemu-fatal-marker.txt" "$run_root/qemu-stall-marker.txt"
    qemu-img create -f qcow2 -F qcow2 -b "$condition_cache_overlay" \
        "$windows_overlay" >> "$run_root/windows-overlay-create.txt"
    prepare_replay_attempt
    reset_swtpm_state "$run_root/swtpm-retry-setup.log"
    start_swtpm "$run_root/swtpm-retry.log" qemu
    set +e
    run_windows_qemu
    set -e
    stop_swtpm
    capture_partial_postboot_evidence
    printf 'ATTEMPTS=2\nFIRST_RESULT=%s\n' "$replay_retry_reason" \
        > "$run_root/windows-replay-retry.txt"
fi
if [[ -s "$run_root/qemu-fatal-marker.txt" ]]; then
    message="Nested Windows hypervisor emitted a fatal VM-exit marker: $(tr -d '\r\n' < "$run_root/qemu-fatal-marker.txt")"
    exit 8
fi
if [[ -s "$run_root/qemu-stall-marker.txt" ]]; then
    message="Nested Windows hypervisor reached a stable non-HLT vCPU stall: $(tr -d '\r\n' < "$run_root/qemu-stall-marker.txt")"
    exit 8
fi
if [[ $qemu_rc -ne 0 ]]; then
    message="Nested Windows QEMU run failed or timed out with exit code $qemu_rc."
    exit 7
fi

missing_postboot_evidence=()
for file in "${postboot_evidence_files[@]}"; do
    [[ -f "$run_root/$file" ]] || missing_postboot_evidence+=("$file")
done
if (( ${#missing_postboot_evidence[@]} != 0 )); then
    message="Nested Windows powered off without publishing required evidence: ${missing_postboot_evidence[*]}"
    exit 7
fi
for file in "${postboot_evidence_files[@]}"; do
    mcopy -o -i "$fat_volume" "::/$file" "$run_root/$file"
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

if [[ "$workflow" == "Hypervisor" ]]; then
    if grep -Fq 'uefi hypervisor: FATAL INTERRUPT' \
        "$run_root/qemu-debugcon.log" ||
       grep -Fq 'uefi hypervisor: AP virtualization: FAIL STOP' \
        "$run_root/qemu-debugcon.log" ||
       grep -Eq '^[FIVX]:[0-9A-Fa-f]{8}$' \
        "$run_root/qemu-debugcon.log"; then
        message="The nested Windows hypervisor path emitted a fatal or failure marker."
        exit 8
    fi
    if ! python3 - "$run_root/qemu-debugcon.log" "$windows_smp" > \
            "$run_root/nested-windows-hypervisor-order.txt" <<'PY'
import pathlib
import re
import sys

text = pathlib.Path(sys.argv[1]).read_text(errors="strict")
expected_ap_count = int(sys.argv[2]) - 1
markers = [
    ("loader production handoff", r"loader production handoff: START"),
    ("nonzero reference-log event count",
     r"reference log events: (?!00000000)[0-9A-Fa-f]{8}"),
    ("replay verdict", r"replay and divergence: PASS"),
    ("boot-manager restore", r"boot manager restore: PASS"),
    ("processor topology", r"uefi hypervisor: processor topology: PASS"),
    ("AP VMX preflight", r"uefi hypervisor: AP VMX preflight: PASS"),
    ("processor resources", r"uefi hypervisor: processor resources: PASS"),
    ("processor state", r"uefi hypervisor: processor state: PASS"),
    ("complete AP activation",
     rf"uefi hypervisor: AP virtualization: PASS\r?\n"
     rf"uefi hypervisor: activated AP count: 0x{expected_ap_count:08X}"),
    ("VMX launch", r"uefi hypervisor: VMX launch: PASS"),
    ("BSP virtualization", r"uefi hypervisor: BSP virtualization: PASS"),
    ("payload launch", r"payload launch: PASS"),
]
position = 0
observed = []
for label, pattern in markers:
    match = re.search(pattern, text[position:])
    if match is None:
        raise SystemExit(f"missing or out-of-order marker: {label}")
    observed.append(match.group(0))
    position += match.end()
print("PASS nested Windows hypervisor order: " + " -> ".join(observed))
PY
    then
    message="Nested Windows completed, but the hypervisor topology, AP preflight, or launch markers were missing or out of order."
        exit 8
    fi
    grep -Fq 'PASS nested Windows hypervisor order:' \
        "$run_root/nested-windows-hypervisor-order.txt"
    python3 - "$run_root/postboot-probe.stdout.txt" "$windows_smp" <<'PY'
import pathlib
import re
import sys

lines = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8-sig").splitlines()
expected_cpu_count = int(sys.argv[2])
marker = "UEFI_HYPERVISOR_WINDOWS_CPUID:PASS"
cpu_prefix = "UEFI_HYPERVISOR_WINDOWS_CPU:"
cpu_lines = [line for line in lines if line.startswith(cpu_prefix)]
cpu_pattern = re.compile(r"^UEFI_HYPERVISOR_WINDOWS_CPU:(\d+):(\d+):PASS$")
cpu_ids = []
for line in cpu_lines:
    match = cpu_pattern.fullmatch(line)
    if match is None:
        raise SystemExit(f"postboot probe reported a non-PASS CPU line: {line}")
    cpu_ids.append((int(match.group(1)), int(match.group(2))))
if len(cpu_ids) != expected_cpu_count or len(set(cpu_ids)) != expected_cpu_count:
    raise SystemExit(
        f"postboot probe did not report {expected_cpu_count} unique CPU PASS lines: {cpu_ids}")
if lines.count(marker) != 1 or any(
        line.startswith("UEFI_HYPERVISOR_WINDOWS_CPUID:") and line != marker
        for line in lines):
    raise SystemExit("postboot probe did not report exactly one hypervisor CPUID PASS line")
print(f"PASS Windows {expected_cpu_count}-CPU hypervisor CPUID health")
PY
    [[ -s "$run_root/postboot-audit-dump.stdout.txt" ]] || {
        message="Windows nested audit dump did not publish evidence."
        exit 8
    }
    status="PASS"
    message="Windows booted through the real UEFI hypervisor payload; topology-wide VMX preflight, resource ownership, processor-local state, AP VMX/INIT/SIPI execution, per-CPU Windows CPUID health, and TPM/audit evidence passed."
    exit 0
fi

status="PASS"
message="Patched OVMF replayed the genuine log, launched Windows, and Windows verified post-replay TPM state."
