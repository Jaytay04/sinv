#!/usr/bin/env bash
set -Eeuo pipefail

target="${1:-}"
script_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
if [[ "$target" != "$script_root" ||
      ! "$(basename "$target")" =~ ^uefi-linux-[a-z0-9-]+-[0-9]{8}-[0-9]{6}-[a-f0-9]{10}$ ||
      "$(basename "$(dirname "$target")")" != "runs" ||
      "$(basename "$(dirname "$(dirname "$target")")")" != "uefiloader-host-runner" ||
      ! "$target" =~ ^/home/[a-z_][a-z0-9_-]*/\.cache/uefiloader-host-runner/runs/ ]]; then
    echo "Refuse unsafe Linux guest run-root cleanup target." >&2
    exit 2
fi
rm -rf -- "$target"
