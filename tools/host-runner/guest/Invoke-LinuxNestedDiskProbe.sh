#!/usr/bin/env bash
set -Eeuo pipefail

run_root="${1:-}"
windows_disk="${2:-}"
if [[ ! "$run_root" =~ ^/home/[a-z_][a-z0-9_-]*/[A-Za-z0-9_./-]+/runs/uefi-linux-nested-disk-probe-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$ ]]; then
    echo "Unsafe or missing run root." >&2
    exit 2
fi
if [[ ! "$windows_disk" =~ ^/mnt/hgfs/UEFILoaderNestedE2E/uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}\.vmdk$ ]]; then
    echo "Unsafe nested Windows disk path." >&2
    exit 2
fi

run_id="$(basename "$run_root")"
result_path="$run_root/result.json"
status="FAIL"
message="Nested Windows disk mapping probe failed."

finish() {
    local rc=$?
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
for tool in qemu-system-x86_64 qemu-img truncate mkfs.vfat mmd mcopy; do
    command -v "$tool" >/dev/null 2>&1 || {
        message="Required guest tool is missing: $tool"
        exit 3
    }
done
[[ -r "$windows_disk" ]] || {
    message="Prepared Windows VMDK is not readable through HGFS."
    exit 3
}

qemu-img info --output=json "$windows_disk" > "$run_root/windows-disk-info.json"
grep -Fq '"format": "vmdk"' "$run_root/windows-disk-info.json"

ovmf_code="/usr/share/OVMF/OVMF_CODE_4M.fd"
ovmf_vars="/usr/share/OVMF/OVMF_VARS_4M.fd"
shell_efi="/usr/share/efi-shell-x64/shellx64.efi"
for path in "$ovmf_code" "$ovmf_vars" "$shell_efi"; do
    [[ -s "$path" ]] || {
        message="Required firmware asset is missing: $path"
        exit 3
    }
done

runtime="$run_root/runtime"
rm -rf -- "$runtime"
mkdir -p "$runtime"
fat_image="$runtime/probe.img"
truncate -s 64M "$fat_image"
mkfs.vfat -F 32 "$fat_image" >/dev/null
mmd -i "$fat_image" ::/EFI
mmd -i "$fat_image" ::/EFI/BOOT
mcopy -i "$fat_image" "$shell_efi" ::/EFI/BOOT/BOOTX64.EFI
cat > "$runtime/startup.nsh" <<'EOF'
map -r
if exist fs1:\EFI\Microsoft\Boot\bootmgfw.efi then
  echo WINESP=fs1
else
  echo WINESP=missing
endif
reset -s
EOF
mcopy -i "$fat_image" "$runtime/startup.nsh" ::/startup.nsh
cp "$ovmf_vars" "$runtime/OVMF_VARS.fd"

set +e
timeout --signal=TERM 120 qemu-system-x86_64 \
    -machine q35,accel=kvm \
    -cpu host \
    -m 768 \
    -drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
    -drive if=pflash,format=raw,file="$runtime/OVMF_VARS.fd" \
    -drive if=none,id=probe,format=raw,file="$fat_image",cache=writeback,aio=threads \
    -device ide-hd,drive=probe,bootindex=1 \
    -drive if=none,id=windisk,format=vmdk,readonly=on,file="$windows_disk",cache=writeback,aio=threads \
    -device nvme,drive=windisk,serial=UEFILoaderNestedProbe,bootindex=2 \
    -net none \
    -display none \
    -monitor none \
    -serial "file:$run_root/qemu-serial.log" \
    -no-reboot \
    2> "$run_root/qemu-stderr.log"
qemu_rc=$?
set -e
if [[ $qemu_rc -ne 0 ]]; then
    message="Nested QEMU disk probe failed or timed out with exit code $qemu_rc."
    exit 4
fi

grep -Eo 'WINESP=fs[0-9]+' "$run_root/qemu-serial.log" |
    sort -u > "$run_root/windows-esp-map.txt"
if [[ "$(wc -l < "$run_root/windows-esp-map.txt")" -ne 1 ]]; then
    message="Nested OVMF did not identify exactly one Windows ESP."
    exit 5
fi

status="PASS"
message="Nested OVMF identified exactly one Windows ESP on the prepared VMDK."
