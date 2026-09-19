#!/usr/bin/env bash
set -Eeuo pipefail

prepared_disk_id="${1:-}"
report_path="${2:-}"
if [[ ! "$prepared_disk_id" =~ ^uefi-windows-capture-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$ ]]; then
    echo "Unsafe prepared-disk identity." >&2
    exit 2
fi
if [[ ! "$report_path" =~ ^/tmp/uhr-cache-validation-[a-f0-9]{10}\.txt$ ]]; then
    echo "Unsafe cache-validation report path." >&2
    exit 2
fi

cache_root="$HOME/.cache/uefiloader-host-runner"
base_disk="$cache_root/prepared-qcow2-v1/$prepared_disk_id.qcow2"
condition_disk="$cache_root/conditioned-v4/$prepared_disk_id.qcow2"
[[ ! -L "$cache_root" && ! -L "$base_disk" && ! -L "$condition_disk" ]]
qemu-img check -q "$base_disk"
qemu-img check -q "$condition_disk"

run_directories=0
if [[ -d "$cache_root/runs" && ! -L "$cache_root/runs" ]]; then
    run_directories="$(find "$cache_root/runs" -mindepth 1 -maxdepth 1 \
        -type d -name 'uefi-linux-*' -printf '.' | wc -c)"
fi
available_bytes="$(df --output=avail -B1 "$cache_root" | tail -n 1 | tr -d ' ')"
base_bytes="$(du -B1 "$base_disk" | cut -f1)"
condition_bytes="$(du -B1 "$condition_disk" | cut -f1)"

{
    echo 'BASE_CHECK=PASS'
    echo 'CONDITION_CHECK=PASS'
    echo "RUN_DIRECTORIES=$run_directories"
    if [[ -d "$cache_root/runs" && ! -L "$cache_root/runs" ]]; then
        find "$cache_root/runs" -mindepth 1 -maxdepth 1 -type d \
            -name 'uefi-linux-*' -printf 'RUN_DIRECTORY=%f\n'
    fi
    if [[ -e "$cache_root/conditioned-v1" ||
          -e "$cache_root/conditioned-v2" ||
          -e "$cache_root/conditioned-v3" ]]; then
        echo 'LEGACY_CONDITION_CACHE=PRESENT'
    else
        echo 'LEGACY_CONDITION_CACHE=ABSENT'
    fi
    echo "AVAILABLE_BYTES=$available_bytes"
    echo "BASE_BYTES=$base_bytes"
    echo "CONDITION_BYTES=$condition_bytes"
    qemu-img info --output=json "$base_disk"
} > "$report_path"
