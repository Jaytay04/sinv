# Workspace cleanup, 2026-10-20

At the user's request, two superseded disposable VM copies and an empty failed
preparation were retired. The workspace inventory decreased from 235.587 GiB
to 115.216 GiB. Removed files totaled 129,448,580,557 bytes (120.558 GiB);
about 192 MiB of configuration metadata and logs were preserved separately.

The paths below are relative to the main repository workspace.

## Removed disposable copies

| Directory under `.codex-build/gpu_payload_vm/` | Previous use |
| --- | --- |
| `vm-20261020T044842Z-6c60e35d` | Archived successful VM run `gpu-payload-20261020T051823Z-d60e12e73d004b94b24ce7a0e000fb1b` |
| `vm-20261020T182612Z-b4b5f39e` | Archived GuestInfo transport failure `gpu-payload-20261020T182528Z-7d1ce3dea92145b6840364370c03dbcb` |
| `vm-20261020T043957Z-a6ba612e` | Failed preparation; 854 bytes of metadata and an empty clone directory |

The first two runs' saved controller results confirmed before/after snapshots,
baseline revert, VM-off state, and no cleanup failures. VMware reported zero
running VMs before each removal. The retained VM configuration and disk
descriptors did not reference these retired copies. Exact paths, file
inventories, reparse-point checks, and metadata copies preceded removal.

Their VM disks and snapshots are permanently removed. Historical receipts
retain the original paths and hashes, but those original VM paths no longer
exist. Small files, including VM configuration, copy manifests, and logs, are
preserved under `.analysis/workspace-cleanup/retired-vm-metadata/`. The saved
controller evidence and signed artifacts outside these VM folders remain.

## Retained

- Default source VM: `.codex-build/gpu_windows_vm/run-01/standalone/`.
- Latest validated VM: `.codex-build/gpu_payload_vm/vm-20261020T184950Z-3845cd62/`.
- Both registered Git worktrees: main and `.codex-build/worktrees/gpu-windows-validation/`.
- Bare-metal journals, archives, signed images, collected diagnostics, photos,
  and pinned recovery/audit binaries, including `.codex-build/gb/afe4be60268b7df1/`.
- Latest VM workflow, signed artifacts, controller receipts, and build outputs.
- External recovery files on `E:` and the physical ESP stage.

Protected evidence and retained VM configuration hashes matched before and
after deletion. Neither source checkout was removed or reset. No host boot,
firmware, security, or GPU setting changed. This workspace cleanup does not
retire the retained EFI stage or satisfy the bare-metal readiness gate.

The original and latest VM account for almost all remaining space. Other build
directories are small and retain binaries referenced by diagnostic journals.

## Local receipts

`.analysis/workspace-cleanup/` contains `inventory-before.json`,
`removal-plan.json`, `removal-result.json`, and `inventory-after.json`.
These local receipts and VM metadata remain excluded from source control.
