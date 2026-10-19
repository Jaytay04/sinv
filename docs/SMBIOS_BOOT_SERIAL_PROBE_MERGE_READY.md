# SMBIOS boot serial probe merge preparation

Prepared on 2026-10-19 for `feat/smbios-boot-serial-probe`. This report covers
both the older NIC/storage/identifier work and the diagnostic GPU work.
No feature-to-main merge, push, host configuration change, or payload execution
was performed during this preparation.

## Reconciliation

Merged `main` at `913c60c75044d90dcfe82c306d5660bbbc93c4d6` into the feature
branch rather than rebasing. This preserves all 17 original commit IDs and
keeps the four GPU commits contiguous on the feature branch's first-parent
history. The reconciliation commit is
`46fef8d6d423c47b06f8c44fc0346a257a5e402d`, with the original feature tip
`a42fa4e393517dbff37a178beea61a14f8e300ef` as its first parent and `913c60c`
as its second parent.

**Conflict resolutions: none.** Git merged automatically. The shim-chain,
SBAT, inventory, and UEFI support content already matched main. The resulting
loader and `tools/efi_inventory` trees match main exactly. Main's ESP commit
changed these seven feature-worktree paths, which now also match main:

- `docs/arc-raiders-offsets.md`
- `modules/esp_analysis/arc_raiders/decrypt.cpp`
- `modules/esp_analysis/arc_raiders/offsets.h`
- `modules/esp_analysis/arc_raiders/pointer_chains.cpp`
- `modules/esp_analysis/arc_raiders/specs/cl_1426848_offsets.json`
- `modules/esp_analysis/esp_analysis.cpp`
- `modules/esp_analysis/hardened/hardened_esp.cpp`

`components/hypervisor/src/efi/CMakeLists.txt`, `gpu_hal.c`, and `gpu_hal.h`
retain their original feature-tip content. No root/component build contracts
or public EFI handoff names were changed.

## Original commit arc

### Storage and recovery

| Commit | Change |
| --- | --- |
| `e5c184d` | Guard boot-start storage testing, add recovery, and introduce the surrounding identifier/boot tooling. |
| `7424d03` | Accept blank BCD lines during preparation. |
| `d0e8bf6` | Reload the prepared release before enabling test mode. |
| `a89d306` | Resume completed installs after journal errors. |
| `d65bebf` | Record the physical-host storage round trip. |
| `c096b2a` | Record Secure Boot restoration after cleanup. |

### NIC

| Commit | Change |
| --- | --- |
| `805bf4f` | Correct NDIS registration and document VM coverage limits. |
| `2dabe6f` | Close the ordinary-reader gap and verify the VM round trip. |
| `f669ffa` | Snapshot active BCD through supported APIs. |
| `ade061e` | Validate VMnet address configuration before host runs. |
| `a3e6910` | Record the host MAC round trip and restoration. |

### Combined identifiers

| Commit | Change |
| --- | --- |
| `1946f03` | Gate combined identifier proof on VM acceptance. |
| `3c41022` | Record the simultaneous host identifier round trip and restoration. |

These commits retain historical evidence; this preparation did not repeat
their host procedures.

### Diagnostic GPU

| Commit | Change |
| --- | --- |
| `3e64513` | Add the caller-owned diagnostic overlay renderer and font ROM. |
| `e528713` | Add the gated Intel diagnostic scanout HAL. |
| `0fb2f6a` | Add boot GPU-exclusion admission and disposable-VM smoke tests. |
| `a42fa4e` | Enforce native guest exclusion, root GPU DMA ownership, and bounded WC mapping. |

### Main changes incorporated

| Commit | Change |
| --- | --- |
| `f87bd63` | Adopt CL-1426848 ESP offsets and correct camera/FOV handling. |
| `913c60c` | Add shim-chain verification, SBAT, and the inventory entry. |

## Verification

Validated source revision: `70c9bae89da96127e6f3a6610713fb696ab9b9a8`.
The final documentation commit adds only this report. The branch is **20 commits
ahead / 0 behind main** including that documentation commit.

A fresh Release combined superbuild passed in the previously nonexistent
primary-workspace directory
`.codex-build/merge-ready-20261019/final-combined`, using Visual Studio 18 2026,
MSVC tools `14.51.36231`, and Windows SDK `10.0.28000.0`. The ExternalProject
component directories remain isolated. Static CRT is enabled; the driver,
historical SCM loader, and optional runtime companion/service builds are off.
The build includes the ESP archives and both EFI components.

The build used `COMBINED_REQUIRE_KERNEL_PROFILE=ON`, build **26100**, UBR
**9711**, and private pinned copies of the kernel image and matching PDB.
Fresh profile generation validated PDB GUID
`B661C6FE-7299-1D16-0FE3-7C48B8BC5470`, age **1**. Input SHA-256 values:

| Input | SHA-256 |
| --- | --- |
| `ntoskrnl.exe` | `4a6af9a3dbbeb440c4a9fbe8a4d6832eae751ac3a85860d860b8313a737a2e3a` |
| `ntkrnlmp.pdb` | `f4f5c5d8dbaf7df07538df9a4cb788c8d6840bdc06fb61e5977e6617f20e1c10` |

| Check | Result |
| --- | --- |
| Fresh configure and `combined_build`, Release | PASS |
| Full configured hypervisor component CTest registry | 29/29 passed |
| Full UEFI loader component CTest registry | 28/28 passed |
| Root superbuild contract CTest | 1/1 passed |
| Default resident EFI `.hvscan` | PASS, 58,710 bytes |
| Separate native/output-enabled build, not executed | PASS, 58,710 bytes |
| Separate VM-self-test build, not executed | PASS, 60,662 bytes |

All three resident audits used `Test-EfiRuntimeScannerImage.ps1` with
`-MaximumPages 16` (65,536 bytes). The normal combined image leaves native,
physical-output acknowledgement, and VM-self-test options off. Setting the
native/output options in the isolated build exercises compilation only; it
does not establish physical prerequisites or authorize booting that image.

The first fresh pinned-profile build exposed an audit false positive: MSVC's
second bounded RVA jump table reused an established image-base register, and
dumpbin decoded table data as an external memory reference. Commit `70c9bae`
corrects only the audit parser and its regression tests. It requires a prior
validated dispatch and a bounded block that preserves the base, keeps exact
data exemptions, and rejects clobbers, conflicting facts, incoming direct
edges, malformed targets, and targets into prior table data. Genuine external
calls/data references remain checked. This is local compiler-pattern validation,
not whole-program control-flow certification. Renderer and runtime code did
not change. A separate verification attempt encountered timestamp contention
when overlapping root targets were requested together; the final clean run
uses only `combined_build`.

Reproduction and raw evidence are retained under the primary workspace's
`.codex-build/merge-ready-20261019/`: `verify-clean.ps1`,
`verify-variant.ps1`, `input-hashes.json`, `final-configure.log`,
`final-build.log`, `final-ctest-{hypervisor,uefi-loader,root}.log`,
`final-hvscan.log`, and `{native,vm-selftest}-hvscan-final.log`.
`verification.json` records image hashes, source revision, profile, and log
hashes. Pinned binaries, generated build state, and raw logs are not committed.

The renderer and scanner remain byte-for-byte identical to `3e64513`:

| Source | Git blob |
| --- | --- |
| `overlay_render.c` | `4774d4ad0ffb68434196cb7526bb32d548df7e7d` |
| `overlay_render.h` | `b5ec19d09ee583a8e4bdec9b516452d7f006a6e3` |
| `font_rom.h` | `6cc3bf08265854b5e0095df54e65db66f871d2f7` |
| `scanner.cpp` | `7ba7e7a001bad6ec2fdb8e8aa85163469776c873` |

## Trusted platform prerequisite and activation state

The diagnostic GPU implementation is code-complete for its current scope.
Live HAL admission/rendering returns `HV_GPU_NOT_SUPPORTED` without the
required host prerequisites and valid installed ownership proof. A physically
verified, attached Intel diagnostic display and one-time pre-guest exclusion
of the Intel adapter are prerequisites; Windows must use the separate adapter.
`HYPERSINV_GPU_DIAGNOSTIC_NATIVE` and
`HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED` default to `OFF`. EFI boot
preparation returns `EFI_UNSUPPORTED` when its activation gates are disabled;
this is distinct from the HAL result. No live activation was attempted here.

Firmware and SMM are trusted prerequisites. Guest PCI/MMIO filtering, the
GPU's root-owned VT-d domain, and the private WC CPU mapping do not constrain
firmware/SMM writes to the display plane, GGTT, framebuffer, PAT, or MTRRs.
Such writes or DMA can silently violate ownership; transient writes that
restore checked register values may evade the ownership-loss latch. Physical
cache policy must remain stable while rendering. Detected ownership loss stops
rendering, drains/unmaps the CPU window, and returns `HV_GPU_NOT_SUPPORTED`
without re-arming. Reboot/reset is the teardown boundary; errors after physical
VT-d isolation activates fail-stop. See
[GPU_DIAGNOSTIC_ACTIVATION.md](GPU_DIAGNOSTIC_ACTIVATION.md).

There is an initial boot render, with no runtime render pump or live hand-back
to WDDM in this increment. Historical disposable-VM validation covers synthetic
enforcement, actual VMX-root WC mapping/render/drain/unmap, and nested VMX
launch/return. It does not cover real UHD 770 scanout, physical Intel PCI/EPT/VT-d
enforcement or cache policy, or the Intel-excluded/NVIDIA-primary dual-GPU
Windows topology and driver compatibility. Those remain host-only validation
items; the existing VM was EFI-only with no Intel passthrough or Windows
session. This preparation performed static builds and host-side model/source
tests only, with no new VM boot. See
[GPU_DIAGNOSTIC_VM_VALIDATION.md](GPU_DIAGNOSTIC_VM_VALIDATION.md).

## Exact merge command and checkout precondition

The branch is intended to fast-forward main after this preparation. The main
checkout remains at `913c60c75044d90dcfe82c306d5660bbbc93c4d6`.

The parent checkout currently has **60 untracked `spoof-handoff` files** whose
paths become tracked on this branch. Of these, 55 have matching Git content
and five differ:

- `spoof-handoff/.gitignore`
- `spoof-handoff/README.md`
- `spoof-handoff/tools/hwid_spoofer/hwid_spoofer.h`
- `spoof-handoff/tools/hwid_spoofer/src/device_identifiers.cpp`
- `spoof-handoff/tools/hwid_spoofer/tests/device_identifiers_tests.cpp`

They were left untouched. Before merging in that checkout, coordinate with
any agents using it and preserve that directory outside the tracked paths.
The following **future operator commands were not executed**. They archive
the entire existing directory, including its distinct edits and local evidence,
under the ignored build root; the destination must not already exist:

```powershell
$repo = 'C:\Users\jayli\OneDrive\Documents\Projects\hsinv'
$archive = Join-Path $repo '.codex-build\pre-merge-spoof-handoff'
if ((git -C $repo branch --show-current) -ne 'main') { throw 'Expected main' }
git -C $repo merge-base --is-ancestor main feat/smbios-boot-serial-probe
if ($LASTEXITCODE -ne 0) { throw 'Reconciliation required before merging' }
if (Test-Path -LiteralPath $archive) { throw 'Archive destination already exists' }
Move-Item -LiteralPath (Join-Path $repo 'spoof-handoff') -Destination $archive
git -C $repo merge --ff-only feat/smbios-boot-serial-probe
```

The exact Git merge command is the final line. Without preserving the colliding
untracked paths first, Git may refuse the merge; do not delete/reset those files
to force it. Keep the archive for review of the five distinct files; do not
blindly copy it back over the merged tree.

Expected post-merge state: `main` and the prepared feature tip resolve to the
same commit, with all original 17 feature commits, both main commits, the
reconciliation, the audit correction, and this report reachable. A fast-forward
adds no further merge commit. Native GPU activation remains off. No push or
host changes are part of this command. If main advances first, reconcile and
revalidate again; `--ff-only` safely refuses a diverged history.
