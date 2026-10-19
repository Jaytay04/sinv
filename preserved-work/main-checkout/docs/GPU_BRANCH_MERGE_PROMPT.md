# Prompt: Reconcile and prepare feat/smbios-boot-serial-probe for merge

## State (verified)

- Branch `feat/smbios-boot-serial-probe` is **17 commits ahead / 2 behind** `main`.
- Merge-base is `9b82c1d`; main HEAD is `913c60c`.
- The branch carries TWO arcs, not just the GPU work:
  - GPU diagnostic scanout (the new work): `3e64513` renderer, `e528713` gated HAL, `0fb2f6a` admission + VM smoke test, `a42fa4e` native enforcement.
  - Older NIC / storage / identifier work: `e5c184d` … `3c41022`.
- Main has 2 commits the branch lacks: `f87bd63` (ESP CL-1426848 offsets) and `913c60c` (shim-chain verification, SBAT, inventory).
- `git diff --name-only main feat/smbios-boot-serial-probe` = 352 files. This is a real integration, not a fast-forward.

## Task

Prepare this branch for merge to `main`. Do NOT merge or push — reconcile, verify, and report.

### 1. Reconcile
- Bring the branch up to date with `main` (rebase the 17 commits onto `913c60c`, OR merge `main` into the branch — pick one and justify it). Prefer the option that keeps the GPU arc reviewable as a coherent unit.
- Resolve any conflicts. Pay particular attention to files touched by BOTH arcs: `components/hypervisor/src/efi/CMakeLists.txt`, `gpu_hal.*`, and anything `f87bd63` (ESP offsets) or `913c60c` (shim-chain) also modified.

### 2. Verify from a clean build
- Fresh configure + build of the combined superbuild (kernel profile 26100 / UBR 9711, pinned PDB).
- Full component CTest for hypervisor and uefi-loader — all green.
- `.hvscan` passes on the EFI image(s).
- Confirm renderer (`3e64513` content) and scanner sources are intact after reconciliation.

### 3. Deliver a merge-ready summary
A single written summary (markdown) covering:
- The full commit arc being merged (both GPU and NIC/storage/identifier), grouped, with one-line descriptions.
- The firmware/SMM trusted-prerequisite assumption and its failure mode.
- Current activation state: code-complete, live activation returns `HV_GPU_NOT_SUPPORTED` until host prerequisites (physical diagnostic display + one-time Intel adapter exclusion) are met.
- Host-only items NOT covered by VM validation (real UHD 770 scanout, dual-GPU topology).
- The exact merge command to run and the expected post-merge state.

## Constraints
- Work on `feat/smbios-boot-serial-probe`. Do not touch `main` except to reconcile against it.
- Conventional commits, no AI/"Co-Authored-By" attribution.
- No host configuration changes.
- Preserve all existing edits; other agents may be working concurrently.

## Report
Reconciliation method chosen + why, conflicts resolved (files), clean-build evidence, CTest counts, `.hvscan` result, final branch SHA, and the merge-ready summary (save it under `docs/`).
