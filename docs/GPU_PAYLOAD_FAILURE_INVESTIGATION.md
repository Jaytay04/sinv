# GPU diagnostic payload failure investigation

## Run 193833: PCI quiesce audit stops activation; Windows handoff proceeds

Run `gpu-20261021T193833Z-c48461667089493988074edfaf008d56` used source
`15842e7d789eddaea401754734ce9540a10b69e7`. Its loader build cache enables
`IB_FIRMWARE_STORAGE_PROBE=ON`. Collection at
`20261021T195255483Z-Collect-eb9e5c37e9a6497ba033c8bb1b9413c3` found all 20
custom Windows health signatures `ABSENT`. All seven collected files match
their saved pins; the five installer-staged files also match their Stage pins.
Both diagnostic files were written and read back successfully on this run.

The saved 577-byte `esp-payload-diagnostic.txt` is SHA-256
`E7679BC5BE861DC410088D2C8104EF1DEE3B2EB1A1CF963E02B45B09F7D42226`.
It records a valid startup return with stage `ap_prepare`, reason
`operation_failed`, EFI status `0x800000000000001A` (`EFI_SECURITY_VIOLATION`),
and detail `2298614290` (`0x89021212`). All 20 processors are prepared and
VMX-ready, preparation failure is 10 (`MEMORY_POLICY`), and activation has not
begun. The physical IOMMU preparation error is classified under AP preparation
after the AP callbacks have completed; these counts do not identify a failing AP.

The detail decodes to IOMMU runtime install substep 9, install stage 2
(`PCI_QUIESCE`), primary and final runtime status 18 (`AUDIT_FAILED`). The
runtime installer returns at this stage before constructing the programming
plan or enabling VT-d. Two quiesce branches return that status: command-register
readback differs from the requested bus-master-cleared value, or the number
of eligible devices differs between the two PCI scans. The first branch is
the new readback guard. This record cannot distinguish them or identify the
segment/BDF or command values. A failed restoration would instead return
`ROLLBACK_FAILED` and take the physical fail-stop path. The observed result
is consistent with an audit rejection whose attempted PCI changes were restored.

The operator's `IMG_6820.MOV` is SHA-256
`63F1D3D0B294075B1CD100D0CFEBA1E85C6F04FD08C27176F76DC9A8DB299C5B`.
The decoder reports 64 frames and decodes 62, through 2.033 seconds. Frame 0
shows successful storage preparation and baseline Block I/O read with a valid
digest. Frame 36 shows diagnostic write/readback success and successful
Windows boot-manager read, device-path construction and LoadImage, followed
by its start-begin marker. The saved 617-byte `esp-handoff-status.txt`, SHA-256
`D371E06861AE50638D36023BA06A6A233E69208ED3BA119E9565236FD9C29C55`,
contains those same successful handoff phases and `payload=FAILED`.
Begin status `0x8000000000000013` is the trace's `EFI_NOT_STARTED` placeholder,
not a returned StartImage error. The operator reports Windows booted normally
without a forced power-off. The short video does not show the later Windows UI.

This is a clean fallback after an earlier payload refusal; it does not exercise
the prior post-activation storage failure. No traced OpenVolume failure is
observed in these records, so the optional post-filesystem-failure block read
was not triggered in the observed path. The baseline alone cannot establish
storage behavior under active VMX/IOMMU isolation. The current Windows
inventory cannot recover the firmware-time PCI command mismatch or scan counts.

The next diagnostic should distinguish the two quiesce audit branches and
retain the first failing segment/BDF, original/requested/readback command
values and restoration result, or the two scan counts. The existing audit
and restoration checks must remain enforced. No new payload change or live
test was performed in this investigation.

The operator subsequently completed rollback and reset. FinalVerify at
`20261021T200510255Z-FinalVerify-38149ac12747467cbb64e46552b7abae` passed reset,
Secure Boot, all 20 `ABSENT` results and the boot-manager hash. Only textual
firmware equality failed: baseline SHA-256 `98E5ECE5…EDC2D80` contains the three
empty generic CD/DVD/removable/network application blocks and their tail
display-order references; current SHA-256 `66786928…A5007` omits them. Windows
and both USB blocks and their retained order match. This saved-text comparison
does not prove historical raw firmware-variable equality. The fixed
`Inspect-GpuFinalFirmware.ps1 -Run193833` profile captures fresh read-only
evidence for the existing run's review without changing its selector or journal.

The operator's successful review receipt is
`8b8a27d744ae4fcd806c4017156da6af/result.json`, SHA-256
`82B73A584E7DF37F223D0E5F9B8E5E69B4E68A854F1905E17F9E9AA66B1346EF`
(16103 bytes). Both raw files are SHA-256
`450434A4447DC11F57FBC9859C5369960DBAA7D42B1700CAA8BC74B849099B9F`
(253 bytes), with no BootNext, BootOrder `[0,1,2]` and zero findings. Both mounted
firmware captures match the current 2890-byte text. Secure Boot and all 20
`ABSENT` results pass, and the selector/journal remain unchanged. The closed
completion profile binds those observations and the original failed result,
preserves an immutable pre-retry journal copy, and requires fresh observations
before accepting the specific firmware-text difference. Completion and Archive
were subsequently completed by the operator. FinalVerify action
`20261021T205810551Z-FinalVerify-8b145aca1e984c25b4558edcb91fd2cc` succeeded,
with phase `Complete` and no errors. Archive preserved the journal and copied
the selector to the run's archive path; the active selector is absent. This
closes recovery for the failed payload run. Ownership-reviewed stage retirement
remains separate; no host workflow was executed by the investigation.

File-only validation passed against the actual receipt, saved command outputs,
failed result, raw selection files and module pins, using a scratch copy of the
unchanged entry journal. The 193833 profile fixture passed 5 cases/45 assertions,
the journal-copy fixture passed 21 assertions, all 18 related Windows CTests
passed, and dispatcher/state coverage passed 53 cases/529 assertions. These
checks executed no native diagnostic, ESP mount, live host query or VM.

Future Prepare journals use a separately pinned version-1 policy that compares
all retained firmware entries while allowing only those exact empty generic
tail placeholders to change independently. Backup retains full text and raw
selection evidence, with external readback-verified copies. FinalVerify keeps
`TextEqual` and original hashes, adds the structured policy verdict, and
requires clean, stable fresh raw BootNext/BootOrder observations around the
comparison. The existing reset, health, Secure Boot, ESP and preflight gates
remain required. Old journals keep strict text comparison. The policy fixes
this recurring recovery false failure; it does not fix the PCI quiesce refusal
or prove GPU isolation.

Validation used saved files and hermetic host fixtures only: 26 comparison
cases, 21 Backup/final-comparison cases, 20 platform cases (including all final
recovery gates), 53 dispatcher/state cases, and the related readiness,
retirement and firmware-review suites passed. The audit boundary was mocked;
no VM, ESP mount, firmware capture/write, payload launch or live recovery action
was executed. The new tests are registered in the component's Windows CTest
suite so the next Build runs them.

## Run 160027: volume open fails after successful payload return

Run `gpu-20261021T160027Z-672213bf26314f69990e6d2305c55fff` used source
`1be532253d6c6cc428ae2d4588a4c6e7aacadb3e`. The operator's `IMG_6818.mov`
(SHA-256 `5618F8683C58FDA5F1E739AE0F8E4D262F382CD00B3DA48ED4D3DCFCB4555B98`)
shows payload launch `PASS`, startup stage `return`, status zero, 20 expected,
prepared and VMX-ready CPUs, and zero preparation or activation failures.
The new operation trace then records `payload diagnostic / open_volume` with
raw and effective `EFI_TIMEOUT` (`0x8000000000000012`). Subsequent handoff-status
writes and the loader's boot-manager image read fail at `open_volume` with
raw and effective `EFI_DEVICE_ERROR` (`0x8000000000000007`). These statuses
come directly from `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL.OpenVolume`, before a
file open, create, delete, read, write, flush, or boot-manager image launch.
The size fields are zero because OpenVolume has no size argument.

Collection found all 20 custom health signatures `ABSENT`, and both diagnostic
files absent. All five collected staged-file hashes match their journal pins.
Frame findings and hashes are retained in
`.analysis/gpu-video-6818/findings.json`. The decoder reported 109 frames but
decoded 96; the analyzed frames show the failures above, and no unobserved
video tail or outer-shim message is asserted. The operator's rollback returned
`RECOVERY_PENDING_USER_POWER_CYCLE`; final reset verification and archive
were still pending at this investigation checkpoint.

The subsequent operator-run FinalVerify action
`20261021T165416133Z-FinalVerify-33a9f87568214ffbb7342a18fcfec7cc` observed boot ID
`2026-10-21T16:52:47.5000000Z`, different from the disarmed boot. Reset, Secure
Boot, 20 `ABSENT` health results, and the Windows boot-manager hash passed.
The sole error is the firmware text comparison. The 4,336-byte baseline is
`BC104DF2EBB63636540220FC2E085980F81AEA595B1EC77FE288116A245FA661`;
the 4,336-byte current listing is
`98E5ECE5B987B754CD1C059C34E9D87A1CFB569E758BCC21971CA21D4EDC2D80`.
Only the three generic CD/DVD, removable-device and network application GUIDs
and their display-order references changed; Windows and both USB entry blocks
are identical. The run remains `Disarmed`. The new `-Run160027` inspection
profile pins the exact failed journal, selector, captures, failed result,
source modules, tools and boot identities for a read-only review capture.
This is not completion approval or historical raw firmware-variable proof.
All ten related inspection, review, entry-journal and module-scope host checks
passed. A separate file-only validation matched the actual selector, failed
journal, firmware captures, result, tools and source-module pins. The selector
and journal stayed unchanged; no live inspection or ESP mount was run.

The operator then captured the read-only review under
`.analysis/gpu-final-firmware-review/2933898a57284570b022635a3398a3a4/`.
The successful 17,633-byte receipt is SHA-256
`EAA79DCFA55C09CEAD703A07E8A4C02CAF2A8C6992EEF006B4891BB8ADF2CBF6`.
Both raw captures report BootNext absent, BootOrder `[0,1,2,3,4,5]`, the saved
boot-manager/reference-log hashes, and zero integrity findings. Firmware text
and raw selection are stable, health is 20 `ABSENT` CPUs, cleanup succeeded,
and the selector and journal are unchanged. No ESP or firmware writes occurred.
These observations do not prove historical raw BootOrder equality.

The completion helper now binds this exact receipt, failed result, original
journal checkpoint, observations, tool hashes and source versions. It preserves
the original failed journal before writing a new verification intent and
rechecks mounted text, raw boot selection, reset, Secure Boot, health and
boot-manager integrity. The historical platform source captured by inspection
and the current integration are pinned separately. Recovery remains pending
until the operator's reviewed FinalVerify succeeds; no live completion action
was run during implementation.

All 14 related host checks passed, including the dispatcher, platform, older
review profiles, and module-scope regression. The new run-specific review
fixture passed 5 cases with 44 assertions; its entry-journal fixture passed
21 assertions. File-only checks also validated the actual failed result,
original journal copy, current platform pin, two saved raw observations, and
five saved command receipts. The active selector and journal stayed unchanged.

The operator subsequently completed reviewed FinalVerify in action
`20261021T191509538Z-FinalVerify-5f519cfe082b43f1a9e6b2ffb7f96433`, then archived
the selector. The completed 50,987-byte journal is SHA-256
`0978A1DEC6A586D08844CDD1C52D9E8382B603363FA6E15C1581EA8127ABAE83`;
the archived 533-byte selector is
`4A0E31FA7F4773F7191A70EA151BB41FCA8363042E8F289620156337F1ABD930`.
The journal is `Complete`, `InProgress` is null, and GPU isolation remains
unattested. The original firmware baseline and `TextEqual=false` are preserved.

Readiness report `22b660f2d7de488c82b0ee9d1abc6150` reports only the retained
stage blocker. Its five file hashes match the journal's Stage and collected
snapshots; both diagnostic files are explicitly absent in collection. The
`-CompletedRun160027` retirement profile binds this evidence to the exact
successful result and earlier review chain. Original build, read-only
inspection, and successful final-verification platform pins are distinct
and checked separately. The helper preserves the existing fresh health,
boot-selection, backup-copy, and exact file-ownership gates. No live retirement
or ESP change was performed while implementing the profile.

Validation passed all 21 related host checks, including earlier retirement
profiles and firmware review/module-scope regressions. The new profile fixture
passed 12 cases with 66 assertions. File-only validation of the actual readiness,
completed journal, archive, collection inventory, and 45 proof pins also passed;
the journal, archive and readiness hashes remained unchanged.

### Concrete verification defect and its causal limit

The IOMMU installer disables bus mastering on discovered PCI functions and
later restores their saved PCI Command values. Previously it trusted the
write callback without reading back the register; the physical callback
reports that a volatile ECAM store was issued. A new model fixture that
acknowledges but ignores a command write reproduces a false success in the
original implementation. This is a demonstrated verification defect. It
could leave a storage controller unable to DMA, but the physical run captured
neither the NVMe command register nor a post-failure VT-d fault record, so it
does not prove this caused the timeout.

The corrected quiesce and restore paths verify the complete command value.
Restore marks a device restored only after matching readback and continues
attempting the other saved devices on failure. An incomplete quiesce cleanup
retains its state count and reports `ROLLBACK_FAILED`; the physical caller
fail-stops instead of returning to firmware with unverified restoration,
even if remapper activation never began. No retry or weakened isolation
policy was added. Host fixtures cover dropped and incorrect writes, failed
readbacks, partial cleanup, and installer-level failure attribution.

Source review found no demonstrated missing mapping for ordinary EFI storage
buffers, unconditional BSP interrupt interception, TSC offset, or firmware
return ABI mismatch that explains the captured failure. These negative
findings are not hardware attestations. Firmware queues, DMA addresses,
interrupt activity, and post-failure VT-d FSTS/FRCD were not captured.
The loader has no existing safe interface for those root-only fault registers;
reading their guest-denied MMIO directly would enter the fatal EPT path.

### Bounded storage discriminator

The diagnostic host Build now enables `IB_FIRMWARE_STORAGE_PROBE=ON`; the
component's default remains OFF. After confirming the payload file exists,
the loader resolves Block I/O on its own loaded-image device handle and
allocates an aligned buffer before payload launch. It reads one logical
block at partition-local LBA 0 for a baseline, then makes at most one further
read after its first **traced** OpenVolume failure returns through the file
helper's existing cleanup, before the loader starts another filesystem
operation. It preserves the original filesystem error first.
There is no device enumeration, fallback, reset, write, flush, or read retry.

Console records include media identity and geometry, raw statuses, exact-success
hashes, comparison validity flags, BSP RFLAGS, and raw results from the existing
health CPUID leaf. Changed device/protocol/media identity prevents the second
read. Failed or warning reads never produce a valid digest. The buffer is
released after the failure sample, or before boot-manager StartImage if no
sample was needed. The probe does not cover earlier untraced existence checks
or filesystem calls inside a child image. A successful block read is not proof
of uncached hardware access or working DMA/interrupt delivery; the two samples
only narrow the failing layer. The extra read can change timing and cache state.
The private protocol declarations follow the
[EDK II Block I/O interface](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/BlockIo.h).

Both unsigned EFI images built. All 52 hypervisor host checks and all 32 loader
host tests passed, including the new storage probe test; the hypervisor's
55,478-byte `.hvscan` audit passed. The PCI regression failed for the intended
ignored-write case against the committed original implementation and passed
against the correction. Independent source review checked the readback/cleanup
and storage-probe contracts. No VM, EFI execution, signing, staging, firmware
write, or live privileged test was performed. The host's underlying failure
remains unconfirmed; this update closes a false-success path and adds evidence
for a later controlled observation.

## Seventh physical run: AP preparation succeeds; later image start reports Device Error

Run `gpu-20261021T042142Z-8137a024c456457b840ce906358bbac2` used source
`a52aa25e918223abae295876800933df9b642e46`, which includes the four-level
VT-d table-format change. The operator's console photos show the payload
launch reported `PASS`, with `payload_launch=ok`, `payload_status=0`, and
`shim_load_phase=firmware_load` / `shim_load_status=0`. The startup record is
valid and reports stage `return`, reason `none`, status zero; all 20 CPUs
were prepared and VMX-ready. Resource, preparation, and activation statuses
are `0x00000002` (ready), with preparation and activation failure fields zero.
This run no longer shows the earlier AP-preparation failure.

A later console photo reports `start_image() returned Device Error`. The
available capture does not identify which image-start operation returned
that status. The collection recorded all 20 CPUs as `ABSENT`, Secure Boot on,
and the ESP matching its backup, but `handoff-status.txt` and
`payload-diagnostic.txt` were absent from the ESP at collection. There is no
persisted startup-record file from this collection. The photos and collection
therefore do not establish the exact handoff failure, whether the payload remained
resident, whether firmware fell back to another path, or any GPU-isolation
result. The loader source reports the payload diagnostic, writes handoff
status, may launch an optional inventory image, and then attempts the Windows
boot manager; the missing files prevent correlating the console error to
those operations.

Rollback and the operator's full power-cycle completed. FinalVerify confirmed
a changed boot ID, Secure Boot on, all 20 CPUs `ABSENT`, and the original
Windows boot-manager hash. Its only error was a textual firmware-enumeration
difference: the saved baseline is SHA-256
`66786928B0A56B57EE9E2137A0645783C9E6C090B6E9B65F88FBF5B91DAA5007`
(2890 bytes), and the current capture is
`7D975992028402E8B90F58AF695D0234F028C9B8B106E1BA36BCD5595E32D4E4`
(4336 bytes). The comparison shows three generic firmware application blocks
(`UEFI:CD/DVD Drive`, `UEFI:Removable Device`, and `UEFI:Network Device`)
and their display-order GUID references added; the Windows and two USB blocks
are unchanged. This is only a text comparison. The run has no historical raw
BootOrder capture, so it does not establish raw firmware-variable equality or
that the added entries are benign.

The operator captured fresh evidence with the run-specific read-only
inspection:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\gpu-windows-validation\tools\gpu_diagnostic\Inspect-GpuFinalFirmware.ps1 -Run042142
```

The successful receipt is
`.analysis/gpu-final-firmware-review/255f08b53f3a432cbe8cd2b386447781/result.json`,
SHA-256 `AD43DF7601413CDAA13A561D499F6902E47AEC53932872014D55B009EB2EC736`
(17,633 bytes). It reports unchanged selector/journal, successful ESP cleanup,
no ESP or firmware writes, and no errors. Both raw captures have BootNext
absent, BootOrder `[0,1,2,3,4,5]`, the saved Windows boot-manager and reference-log
hashes, and zero integrity findings. The firmware text and raw captures are
stable; health reports all 20 CPUs `ABSENT`. This establishes the reviewed
current observations, without proving historical raw BootOrder equality or
the cause of the image-start failure. GPU isolation remains unverified.

The operator then ran `FinalVerify -ReviewedFirmwareDifference`, which passed
with `FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED`.
The completion helper checked the exact receipt, original journal checkpoint,
saved result and source pins, plus fresh mounted-ESP text, raw boot selection,
boot-manager integrity, reset, Secure Boot, and ABSENT health observations.
The original firmware baseline is preserved, `TextEqual` remains false,
and the reviewed exception is recorded separately.

Archive subsequently returned `SELECTOR_ARCHIVED`. File-only verification
confirmed the selector is absent, the journal is `Complete`, and its final
errors are empty. The archived selector retains SHA-256
`B4CF6844894A920912EFF85B80012ED3B0B5504D7BD01341A531316C8FC8424C`;
the preserved final journal is
`A4B6D6C8DB917F5A0578A9D4A6775D422CED476BD164B579C4506F73B0F9BF9F`.
This run requires no further recovery commands. Its staged files remain
retained for a separate ownership-reviewed retirement before another stage.
Recovery is verified; GPU isolation remains unverified.

Offline validation passed all seven related firmware-inspection, review,
entry-journal, module-scope, and host-platform tests. The new review fixtures
cover accepted completion and reject changed receipts, source pins, failed
results, journal copies, health, boot identities, raw selection, and integrity
findings. A separate file-only validation checked the actual receipt and its
saved files, the original failed journal context, and the current reviewed
platform source. The selector and journal stayed unchanged. No VM or live
inspection was run during this validation.

### Diagnostics added for the next handoff test

The available evidence still does not identify the later Device Error. The
ordinary image launcher now supports an optional synchronous observer while
retaining its existing API and error/cleanup behavior. The production loader
records before/after boundaries for file read, device-path construction,
shim-aware LoadImage, and StartImage separately for the optional inventory
image and Windows Boot Manager. Load completion also records the shim
subphase/status. A return record preserves the final child status, including
the existing Device Error normalization for a successful load with no child
handle. Firmware validation inside LoadImage is not presented as a separate
observable operation.

The console prints the records before each operation. The existing schema-1
`handoff-status.txt` retains its original fields and appends the bounded
progress records; no additional ESP filename is introduced. Both this file
and `payload-diagnostic.txt` now use diagnostic-only write verification:
write, flush, and close statuses are recorded; the file is reopened and its
length, bytes, and SHA-256 are checked immediately. Skipped readback is labeled
`NOT_RUN`, with a separate attempt flag distinguishing it from an attempted
read returning EFI_NOT_STARTED. Failures remain nonfatal to Windows handoff.
Immediate readback does not prove persistence across firmware fallback or a
subsequent boot; if I/O fails, console evidence may be the only output.

The unsigned Release loader built successfully at 71,168 bytes, SHA-256
`99B5020CD29B333A74ABA20D191F765C7068319796E5711BD9C07930E7E950B8`.
All eight focused host tests passed, covering file-I/O failures and bounds,
launch phase ordering/statuses, formatter bounds, existing restore behavior,
shim behavior, and ABI checks. No EFI image was executed, signed, staged, or
armed during this validation, and no VM was started. These changes improve
the next observation; they do not establish a fix for the physical failure.

## Sixth physical run: VT-d supports only the 48-bit table format

Run `gpu-20261021T032117Z-e70cc55505f948aab50c6c830b9048b1` used clean source
`c6a36ffcda3e7d599ceff57439b8d7d6049f40da`. Its collected 577-byte
`esp-payload-diagnostic.txt` has SHA-256
`1E162092649E707EB6D185816BB33C4B0F09B0B70CE548EF02432ABFB960FB8D`.
The packed startup detail is `2707108112` (`0xA15B3110`): unit 0 rejected the
requested SAGAW format. VER major is 4, SAGAW mask `4` advertises 48-bit
tables only, MGAW is 39 bits, and 2 MiB pages are supported. Software
requested 39-bit tables. This identifies the incompatibility: the table
format is unsupported even though the remapper's maximum guest address width
is 39 bits.
Intel defines SAGAW and MGAW as separate VT-d capabilities in its
[VT-d specification](https://cdrdv2-public.intel.com/671081/vt-directed-io-spec.pdf).

The image shows the separate PCR replay gate refusing to replay because the
PCR banks are not empty. That refusal is not the payload failure: the loader
continued the Windows handoff, loaded the payload successfully, and the
payload returned `EFI_SECURITY_VIOLATION` (`0x800000000000001A`) after AP
preparation. All 20 CPUs were prepared and VMX-ready; activation status and
failure were zero. The recorded SAGAW rejection identifies the payload's
preparation failure independently of the PCR replay gate.

The offline fix now chooses one second-level table format
supported by every remapping unit, preferring 39-bit when common and otherwise
using 48-bit when common. Both formats retain the 39-bit DMA and table-address
limits. For the four-level format, the legacy domain activates the reserved
PML4 page 257, with entry 0 pointing to PDPT page 258; the sparse GPU domain
uses one additional page for its PDPT. Builders and resident audits verify
matching context AW values and require unused PML4 entries to remain zero,
preventing aliases above the allowed range. The native DMA-denial walker
recognizes both geometries. Runtime installation rechecks the selected SAGAW
on every unit before remapper writes, while MGAW and DMAR host-width checks
still use the separate configured DMA limit. Existing entry points preserve
their prior semantics through wrappers; all structure layouts and the 80-byte
version-1 handoff record remain unchanged.

The unsigned native Release image is 189,952 bytes, SHA-256
`08C480BF64C437DC3D619556C166E2A6207D69753807C258C260A8B640EF9852`.
Its resident `.hvscan` audit passed at 55,478 bytes, and all 43 host tests
passed serially. New fixtures cover common/disjoint SAGAW masks, the measured
major-4/SAGAW-4/MGAW-39 capability tuple, AW2 contexts, private-page denial,
read-only GPU mappings, upper-PML4 aliases, the unchanged 39-bit boundary,
and cleanup on insufficient table capacity. Independent source review found
no blocking issue. Build logs are retained locally under
`.analysis/vtd-four-level-validation/`.

The rebuilt image also exposed a compiler-emitted bounded RVA jump-table
shape using `movsxd` that the resident-image checker did not recognize.
The checker now recognizes that exact unsigned-bound/register-flow pattern
and validates every table target and raw/virtual bound. Regression fixtures
retain detection of genuine external references and reject mismatched
registers, missing bounds, corrupt targets, and invalid table extents.
No audit bypass or image-specific exemption was added.

This corrected the measured table-format mismatch in source and host models.
At that point, the rebuilt image had not been signed, staged, armed, or booted;
no VM was started for that run. The seventh physical run above later exercised
the updated image, but successful GPU isolation remains unverified.

FinalVerify completed with Secure Boot enabled, all 20 CPUs ABSENT, the reset
boundary passed, the Windows boot-manager and firmware-text baselines matched,
and no verification errors. Archive removed the selector and preserved the
`Complete` journal (SHA-256
`152259205B75D5E2CAB6F0D036041707E0AAA9DDD3A2431BFF239C421F38699F`). No
further recovery commands are needed. The retained stage belongs to
`032117`; retirement profiles for older runs cannot remove it.

## Fifth physical run: VT-d programming-plan capability rejection

Run `gpu-20261021T023306Z-f33dfae1e322470ca9315bbbf8d49185` used clean source
`49fbf270093f5e65a3f00fd63077b113d662f301`. Its collected 577-byte
`esp-payload-diagnostic.txt` has SHA-256
`32C75210326A2C0253270A5CDD38F17B0F9E83C6253F4BACC9814B212797BC36`.
The operator did not see the diagnostic console, but the saved valid startup
record reports payload entry, `ap_prepare`, all 20 CPUs prepared and VMX-ready,
and `startup_detail=2298678027` (`0x89030B0B`). This decodes to IOMMU runtime
installation (substep 9), programming-plan construction (stage 3), and
`REMAPPING_UNSUPPORTED` (11) as both primary and final result. The payload
returned `EFI_SECURITY_VIOLATION` (`0x800000000000001A`) from its own guard;
firmware image loading had returned success. Activation status/failure are zero.

The failing planner reads VER/CAP/ECAP/GSTS/RTADDR/FSTS before checking the
remapper's version and capability fields. At this exact stage, status 11 can
come from major version zero, absent support for the requested adjusted guest
address width (SAGAW), insufficient maximum address width (MGAW), or absent
2 MiB second-level page support (SLLPS). This failure precedes runtime register
programming. Its diagnostic does not record which unit or predicate failed,
and the saved run contains no raw register values that disambiguate them.

The physical implementation requests 39-bit adjusted address width and builds
matching three-level tables. Its ordinary identity and dedicated GPU table
builders, validators, and range limits also assume 39 bits. Intel specifies
SAGAW bit 1 for 39-bit tables and bit 2 for 48-bit tables; hardware must support
the table format software selects. A 48-bit-only remapper is therefore a
compatibility candidate, not an established diagnosis of this host. Changing
only the requested width would leave the table format inconsistent. See the
[Intel capability-register definitions](https://edc.intel.com/content/www/us/en/design/publications/13th-generation-core-processor-datasheet-volume-2-of-2/capability-register-cap-reg-0-0-0-vtdbar-offset-8/).

Rollback succeeded, followed by the operator's shutdown/power-on boundary.
FinalVerify action `20261021T024702343Z-FinalVerify-d4bc5edd8707406f8b22c2fa341b6217`
returned `FINAL_RESET_VERIFIED_GPU_UNATTESTED` with no errors: Secure Boot on,
20 ABSENT CPUs, unchanged Windows Boot Manager, and the mounted firmware text
equal to its saved baseline (`66786928...1DAA5007`). Archive removed the selector;
the journal is `Complete` with no action in progress, SHA-256
`AF913F6952E4614AB3A806DBC9C82667D62E5CA2CD955950030934D172DABAAC`.
This run needs no further recovery commands. Its retained stage belongs to
`023306`; the older `-CompletedRun224018` retirement profile cannot remove it.

### Capability evidence for a future diagnostic run

The planner now records the first rejected capability predicate and its unit
index, requested width, VER, and CAP in caller-owned transient storage. It
uses the existing reads, retains the original predicate order and status 11,
and preserves a later PCI-restore failure as the final status. An incomplete
register read leaves capability evidence unavailable. Every installer call
initializes the evidence so a later invocation cannot reuse stale values.

The BSP packs the fields used by the four checks into the existing 32-bit
`startup_detail`. The 80-byte version-1 record and all previous detail values
remain unchanged. Tagged substeps 1..24 retain the older layout below.
Capability rejections use substeps 32..63 with this separate layout:

| Field | Meaning |
| --- | --- |
| Bit 31 | Tagged detail |
| Bits 30:24 | `32 + unitIndex * 4 + (failure - 1)` |
| Bits 23:19 | Final runtime result enum |
| Bit 18 | Requested adjusted width: 0 = 39, 1 = 48 |
| Bit 17 | CAP.SLLPS 2 MiB support |
| Bits 16:11 | Encoded CAP.MGAW; add 1 for the address width |
| Bits 10:6 | CAP.SAGAW mask |
| Bits 5:2 | VER major version |
| Bits 1:0 | Reserved, zero |

The unit index is zero-based in that invocation's parsed DMAR unit list.
Failure values are VERSION (1), SAGAW (2), MGAW (3), and SLLPS (4).
Programming-plan stage 3 and primary status 11 are implicit in this detail
group. Unrepresentable indices or result values fall back to the old
substep-9 record; they are not truncated. Only the check inputs are persisted,
not unrelated CAP/ECAP fields or a complete raw-register dump.

For example, **synthetic** detail `0xA55B3104` (unsigned `2774216964`) reports
unit 1, SAGAW rejection, requested width 39, major version 1, SAGAW mask 4
(48-bit tables only), MGAW 39, 2 MiB support, and final status 11. This is a
transport fixture, not a measurement from the host. The existing loader saves
it without a production formatter change, even if console text is missed.

The unsigned native Release image is 188,928 bytes, SHA-256
`3A198EA3BFA4C669B5143A75A2B0B94D7BD6AF855B5913AE78A6762FE89C6BD5`.
Its `.hvscan` audit passed at 54,870 bytes. Six relevant hypervisor host tests
and the loader formatter regression passed serially. Callback fixtures cover
all four predicates, failure on the second unit, unavailable reads, stale
evidence reset, and preservation of the primary cause when PCI restore fails.
The new image was not signed, staged, armed, or booted; no VM was started.
This supplies missing evidence and does not repair or bypass the capability
rejection. Another physical measurement is needed to establish this host's
specific capability mismatch.

## Fourth physical run: IOMMU preparation failed

Run `gpu-20261020T224018Z-31b7c15e56584f84a68d89b7b3ecb1a1` used source
`69a3c3d94f33d1903778ef5ec5b0a1f015b1be29`. The collected 568-byte
`esp-payload-diagnostic.txt` has SHA-256
`89F6368DDAA5446CA95ABF05F77BFB93EACFDE5E3D777696DA1A660D62EC15D4`.
It reports `ap_prepare`, `operation_failed`, EFI status
`0x8000000000000015`, detail `4`, preparation failure `10`, and all three
expected/prepared/VMX-ready counts equal to 20. Activation status and failure
remain zero.

This run passed the earlier memory-policy block and AP preparation callbacks.
Detail 4 selects `HvEfiPreparePhysicalIommu`. Its caller converted every EFI
error there to `MEMORY_POLICY` and ultimately returned `EFI_ABORTED`, so the
record does not identify the failed IOMMU predicate. The previous MMIO-union
correction removed the earlier blocker; successful VMX launch or GPU isolation
has not been established.

The historical DMAR capture described below has a GPU-only 72-MiB RMRR
`[0x7c000000,0x80800000)` scoped to endpoint `00:02.0`. The GPU ownership path
explicitly excludes that RMRR from the legacy guest domain. That table alone
does not predict a scope or RMRR/deny rejection. It was not captured from this
UEFI invocation, and the saved run contains no firmware memory map, VT-d
capability/status values, or inner runtime result. A pre-enabled remapper,
unsupported capabilities, map capacity, GPU-domain policy, and runtime
callback failures remain candidates, not diagnoses. The PCR replay refusal
still does not explain this independently attempted payload return.

Recovery completed through the fixed firmware review. FinalVerify action
`20261020T232519113Z-FinalVerify-5a786491a066456facce99b21b3bf06a` returned
`FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED`.
It recorded the post-disarm reset, Secure Boot enabled, 20 ABSENT CPUs, the
unchanged Windows boot-manager hash, accepted reviewed firmware text, and no
verification errors. Both fresh raw captures have BootNext absent, BootOrder
`[0,1,2]`, and zero integrity findings. Archive subsequently removed the active
selector and preserved the `Complete` journal. Retained stage files still
require a run-specific retirement; the old `201140` profile does not own them.

### IOMMU failure diagnostic refinement

The existing 80-byte version-1 startup record and all legacy detail values
remain unchanged. For new AP-preparation IOMMU results, the high bit of
`startup_detail` identifies a packed observation:

| Bits | Meaning |
| --- | --- |
| 30:24 | IOMMU preparation substep |
| 23:16 | First failing runtime-install stage, or 0 outside that transaction |
| 15:8 | Primary result enum before rollback/restore |
| 7:0 | Final result enum actually returned |

Substeps are argument validation (1), state validation (2), mapped-range pool
allocation (3), denied-range pool allocation (4), memory descriptor validation
(5), RMRR ownership (6), legacy identity domain (7), GPU domain (8), and runtime
installation (9). Result codes 255 mean not applicable; the raw EFI error
remains in `startup_status` and `payload_status`.

The installer stage values are argument (1), PCI quiesce (2), programming plan
(3), initialization (4), enable (5), audit (6), rollback (7), and PCI restore
(8). Complete success is stage 9 with both statuses zero. Runtime enum values
retain their definitions in `iommu_runtime.h`: for example, 10 is capacity,
11 remapping unsupported, 15 already enabled, 16 callback failed, 17 timeout,
18 audit failed, and 19 rollback failed. The first failure is retained even if
later recovery replaces the returned enum. Rollback runs after a recorded
failure, so its failure normally changes the final status while preserving
the earlier stage. Boolean/EFI-only GPU gates have result codes 255 and retain
their EFI status. This does not attest a successful cleanup or guarantee that a
fail-stop can return a diagnostic to the loader.

For example, synthetic detail `0x89030F0F` means runtime installation,
programming-plan stage, and `ALREADY_ENABLED` as both primary and final status.
It is not an observation from this run. The existing loader prints this value
as unsigned decimal `2298679055`; no loader production change is required.

Physical preparation now preserves an EFI allocation/GPU-domain error through
the AP caller instead of replacing it with `EFI_ABORTED`. Tagged details
separate descriptor/RMRR/domain rejection from the runtime transaction.
Existing policy predicates, runtime programming/rollback/PCI restore order,
and fail-stop behavior remain in place. The diagnostic is caller-owned and
allocation-free; AP callbacks and resident code do not access the loader record.

The native-enabled Release EFI build passed its `.hvscan` audit at 54,870 bytes.
The unsigned image is 187,904 bytes, SHA-256
`4A236650B3BB1AA2FDEED97A4DCE0A7C5EFCBCDCAE263D358E446D1E65595450`.
It was not signed, staged, armed, or executed. The loader formatter regression
also passed for the tagged high-bit detail. All 41 hypervisor component tests
passed, including callback-injected first/final failure preservation, the
startup-record round trip, and BSP diagnostic lifetime contracts. This
refinement fixes missing failure information; it does not establish or repair
the physical IOMMU cause.

### VM coverage and GPU-domain diagnostic completion

The maintained VM path cannot reveal the physical host's failing VT-d state.
`HvEfiAdmitDmaIsolation` sees an outer hypervisor and selects external DMA
isolation. `HvEfiPreparePhysicalIommu` returns without programming physical
units when the isolation mode is not `VTD_MEDIATED`. A VM boot can validate
startup/return and diagnostic transport, while callback-driven host tests can
exercise synthetic planner and hardware-status failures. Neither recovers the
unrecorded CAP/ECAP, PCI scope, memory map, or firmware enable state of run
`224018`. No VM was started for this refinement.

The generic GPU-domain substep 8 is now refined inside its builder so a future
failure does not require another diagnostic revision merely to split that
branch. Existing substeps 1 through 9 retain their values:

| Substep | GPU-domain operation |
| --- | --- |
| 10 | Validate GPU/MCFG profile |
| 11 | Allocate temporary planner storage |
| 12 | Check remapping-unit capability |
| 13 | Check unit scopes |
| 14 | Check PCI requester capacity |
| 15 | Resolve PCI requester ownership |
| 16 | Require the integrated GPU requester |
| 17 | Classify RMRR ownership |
| 18 | Resolve RMRR endpoint scope |
| 19 | Require the RMRR requester in the inventory |
| 20 | Check mapping-storage capacity |
| 21 | Build the ordinary identity plan |
| 22 | Build the dedicated GPU plan |
| 23 | Check the GPU DID against each unit's capability |
| 24 | Build the dedicated GPU translation tables |

Substeps 21 and 22 retain the exact `HV_EFI_IOMMU_STATUS` values from
`iommu_isolation.h`, rather than the distinct runtime enum: for example,
scope mismatch is 9, capacity 14, and deny violation 15. Substep 24 uses the
runtime enum from `iommu_runtime.h`. These GPU substeps use install-stage zero;
only substep 9 uses the runtime transaction's stages. Each failed allocation
preserves the firmware's actual EFI status. The planner call order and all
admission, domain separation, range limits, and cleanup behavior remain intact.

A new pure planner fixture exercises the converted two-unit topology:
segment-0 DRHDs at `FED90000` and `FED91000`, the exact integrated requester on
the first unit, an include-all fallback on the second, and exclusion of the
GPU-only RMRR from ordinary domains. With synthetic valid ranges and explicit
capabilities, identity and root plans pass; aliased DIDs and omitted GGTT deny
coverage still fail. This removes a model coverage gap without claiming the
physical host used those synthetic values.

The unsigned native-enabled Release image is 188,416 bytes, SHA-256
`AC4F8665D66133CEEC60EBCEFBF4E1332879E72602A112E81E09EB0CEFF90723`.
The `.hvscan` audit remains self-contained at 54,870 bytes. Six relevant host
tests passed serially: static contracts, diagnostic transport, IOMMU isolation,
IOMMU runtime, BSP diagnostic lifetime, and resident RVA tables. The image was
not signed or booted. This is improved evidence collection, not a confirmed
fix for the physical failure.

## Third physical run: memory-policy preparation aborted

Run `gpu-20261020T201140Z-6e6e86230a2549b28be3274dfe56cec7` used source
`933b4c676318f26396cdf2c66de01c3ccbf1a950`. Its 567-byte collected diagnostic,
SHA-256 `DCFDD2F7DA355781F73BDE7D5DA078CF757BD9F17CF2AD17833407FAB9DC2AAA`,
contains a valid returned startup record:

```text
startup_stage=ap_prepare
startup_reason=operation_failed
startup_status=0x8000000000000015
startup_detail=2
startup_expected_cpus=20
startup_prepared_cpus=1
startup_vmx_ready_cpus=20
startup_resource_status=0x00000002
startup_preparation_status=0x00000003
startup_preparation_failure=10
startup_activation_status=0x00000000
startup_activation_failure=0
```

Detail 2 is `AP_MEMORY_MAP`; preparation failure 10 is `MEMORY_POLICY`.
The payload entry executed and returned `EFI_ABORTED` before AP preparation
callbacks, GPU arming, and AP activation. The resource-ready result and
VMX-ready count describe earlier setup and capability checks, not successful
processor launches. The prepared count is initialized to one for the BSP.

The failing block in the tested revision of `HvEfiPrepareApProcessorStates`
combines two pool allocations, `HvEfiGetMemoryMap`, the descriptor-version
check, the EPT memory-type plan, the EPT deny inventory, and the scanner RAM
allowlist. All failures in this block produce the same diagnostic. The saved
run does not contain the firmware memory map, allocation results, deny
inventory, or individual helper verdicts. Consequently, it cannot identify
the rejected predicate or a specific physical hardware cause. Missing EPT
coverage alone selects conservative UC memory; it is not a plan rejection.
The separate PCR replay refusal does not explain this payload return.

### VT-d MMIO inventory overlap confirmed and corrected

A subsequent read-only capture of this host's DMAR table found DRHD register
bases `0xFED90000` and `0xFED91000`. The table is 136 bytes, SHA-256
`3F5B6BAF70DC8CD829B666B7AFA5F7D6F51CDB0A6B84902669467443267E43F8`.
It was retrieved through the Windows
[GetSystemFirmwareTable API](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemfirmwaretable)
and its signature, length, record bounds, and ACPI checksum were checked.
The retained capture is
`.analysis/ap-preparation-fix/dmar-5c5eaa9e899a406eaa72e48254f6f3e6/dmar.bin`.
This observation wrote only local evidence; it did not change firmware or boot
selection.

The old deny inventory reserves four 4-KiB pages at each DRHD register base.
Those windows are `[0xFED90000, 0xFED94000)` and
`[0xFED91000, 0xFED95000)`. Its final overlap check therefore rejects this
host's inventory deterministically before AP callbacks can run. This is a
confirmed blocker consistent with the saved `AP_MEMORY_MAP`/`MEMORY_POLICY`
record. The coarse historical record cannot establish whether an earlier
predicate in the same block also failed.

`HvEfiBuildEptMmioDenyRanges` now validates and sorts the firmware MMIO windows
and forms their union before they enter the combined deny inventory. For this
host the result is `[0xFED90000, 0xFED95000)`, five pages. All intended protected
bytes remain denied. Only firmware MMIO windows are normalized; overlap
between that union and CPU/GPU/private allocations still fails the final
ownership check. The helper admits at most eight windows and uses a 64-byte
scratch array. No context layout, C/MASM offset, or loader handoff ABI changed.

The preparation block also records each substep separately and preserves a
failing firmware call's EFI status through cleanup. Existing detail values
retain their meanings; new values are:

| `startup_detail` | Preparation substep |
| --- | --- |
| 10 | Allocate EPT memory-type storage |
| 11 | Allocate deny-range storage |
| 12 | Retrieve the UEFI memory map |
| 13 | Validate memory descriptor version |
| 14 | Build the EPT memory-type plan |
| 15 | Build the combined deny inventory |
| 16 | Build the scanner RAM allowlist |

The host regression exercises the captured address pair, all five denied EPT
pages, accessible neighboring pages, unsorted/disjoint/adjacent/duplicate
windows, the eight-window bound, malformed input and unchanged outputs on
rejection, the 39-bit boundary, and continued rejection of private/MMIO
overlap. Diagnostic tests preserve all seven substep values and their raw EFI
errors through the existing startup record. The native-enabled Release EFI
build passed its `.hvscan` self-containment audit at 54,870 bytes. Build outputs
are isolated under `.codex-build/apfix/h`; original signed run artifacts and
recovery evidence are preserved. A new boot has not yet confirmed physical
startup after this correction, and GPU isolation remains unattested.

The unsigned EFI image is 186,368 bytes, SHA-256
`6EF5A1B4C1BBFF039D1CA1EDEEC8F5B7D6AA8A3C5516C4E6E7E0B6F9B8419FC5`.
It was built for source verification, without signing, staging, or arming it.
All 37 hypervisor component tests passed in 98.38 seconds, including the new
MMIO-union and diagnostic regressions. The CTest log is retained at
`.analysis/ap-preparation-fix/ctest.log`.

### Reset passed; firmware text still differs

Rollback succeeded. FinalVerify at `2026-10-20T20:50:30Z` observed a changed
boot ID, Secure Boot enabled, expected ABSENT health on all 20 CPUs, both
display adapters, and the original Microsoft boot-manager hash. Its only
reported error was the firmware textual comparison. At that checkpoint the
journal remained `Disarmed`; completion and Archive were established later
through the reviewed recovery described below.

Both firmware captures are 4,336 bytes. The backup hash is
`ECB8F364A0058CB7D60B313E3941FCF61760F0C8F814EA188FD9BE5122C478CE`; the final
capture hash is
`4A1535FACBC5A6E66F7BFA80275D70B63BEC583F779B734D668B48109A723FAE`.
Offline comparison verified that all seven blocks agree after accounting for
the following three identifier substitutions and the printed application-block
order. The Windows Boot Manager and both USB application blocks are unchanged.

| Description | Backup identifier | Final identifier |
| --- | --- | --- |
| UEFI:CD/DVD Drive | `1eb2958e-ccbc-11f1-8b18-806e6f6e6963` | `a81bb0ad-ccc7-11f1-8b1b-806e6f6e6963` |
| UEFI:Removable Device | `1eb2958f-ccbc-11f1-8b18-806e6f6e6963` | `a81bb0ae-ccc7-11f1-8b1b-806e6f6e6963` |
| UEFI:Network Device | `1eb29590-ccbc-11f1-8b18-806e6f6e6963` | `a81bb0af-ccc7-11f1-8b1b-806e6f6e6963` |

These are BCD identifiers, not raw UEFI Boot numbers. The installer receipts
report that Arm selected owned `Boot0006` and rollback cleared owned one-shot
state without changing BootOrder. There is no saved raw pre-arm BootOrder
snapshot, so historical raw restoration cannot be proved from these records.
The comparison result does not justify deleting these generic entries,
rewriting the original baseline, or treating the failed FinalVerify as passed.

`Inspect-GpuFinalFirmware.ps1` collects a separate observation for this exact
failed run. It pins the three imported modules, audit tool, health probe,
reference log, and both reviewed firmware captures. It requires the same host,
the post-disarm reset boundary, fresh 20-CPU ABSENT health, exact current
mounted-ESP firmware text, and successful raw boot-selection checks before and
after health. It releases only its owned temporary ESP mount and verifies that
the selector, journal, and pinned inputs stayed unchanged. The raw audit scans
BootNext and entries referenced by BootOrder; it does not enumerate orphan
Boot variables. Its result explicitly leaves `RecoveryCompleted` false.

Seven mocked cases with 18 assertions passed, covering preview, journal
selection, successful capture, health failure, unmount failure, raw selection
change, and failure of the second firmware capture. A separate non-admin
preflight reached the expected administrator guard after validating the actual
saved inputs; no raw capture or host query ran, and selector/journal hashes
remained unchanged. Elevated physical-host observation remains an operator
step. These checks do not mark FinalVerify successful or authorize Archive.

### Read-only firmware inspection passed

The operator's inspection succeeded. The result at
`.analysis/gpu-final-firmware-review/8abb7b84ca644a7c84019a1f29b35bb4/result.json`
is 16,924 bytes, SHA-256
`BEA6DCA1FC904B13830908471D6239A906F71AA6937630B32CBA6DC3C2C023D9`.
The two 259-byte raw boot-selection captures both hash to
`B5D4EB51A58A2170EBCA905749E2BDAD177CFD71629A1079A14934F3F5BF63D0`.
They report BootNext absent, BootOrder `[0,1,2,3,4,5]`, expected boot-manager
and reference-log hashes, and zero findings. Both accompanying integrity
commands exited zero. Fresh health passed expected ABSENT on all 20 CPUs;
Secure Boot remained on, the reviewed firmware text stayed stable, and owned
ESP cleanup succeeded. The selector and journal hashes stayed unchanged.

This evidence supports a bounded review of the known textual difference.
`FinalVerify -ReviewedFirmwareDifference` requires this exact pinned receipt,
run, original/current capture pair, and post-reset session, plus ordinary final
checks and fresh raw observations. It leaves textual equality false and records
review acceptance separately. Neither the source baseline nor the failed
FinalVerify result is rewritten. The default FinalVerify remains strict.
No new physical boot, TPM operation, firmware write, or GPU activation is part
of this recovery review. Historical raw equality and unreferenced Boot
variables remain outside the evidence's scope.

The review helper's fixture checks passed 5 cases with 19 assertions; platform
verification passed 18 cases with 406 assertions, and dispatcher/state checks
passed 51 cases with 514 assertions. The actual saved receipt and its cited
evidence also passed offline validation with no native query or journal change.
The opt-in preview reports no host changes. Fresh native observations and the
final journal transition still require the operator's elevated invocation;
these development checks do not themselves complete the run.

### Reviewed verification interrupted by a module reload

The reviewed FinalVerify action at `2026-10-20T21:26:30Z` saved fresh 20-CPU
ABSENT health, matching reviewed firmware text, two identical raw boot-selection
captures with zero findings, and successful boot-manager integrity commands.
Its owned ESP unmount receipt also succeeded. Journal completion and the
failure receipt then failed because `Write-GpuJson` was no longer visible in
the dispatcher. The journal retains phase `Disarmed` and the interrupted
FinalVerify intent; these observations do not establish completion.

Isolated Windows PowerShell reproduction with the actual modules identified
the cause: force-importing `GpuHostState.psm1` from a nested module removes
its exports from the caller. The retirement operations factory performs that
same forced import. The repair reuses loaded dependencies and builds the
review callbacks around existing private capture functions, leaving the
pinned retirement module unchanged. A regression test exercises the actual
wrapper and public helper imports, checks caller command visibility, and
writes fixture journal and failure JSON through the real state writer.
All 17 assertions passed, including the replacement capture callback wiring.
Restoring the forced state import in the isolated module copy reproduced the
missing-writer failure, confirming that the regression test detects the bug.

The saved review receipt and its cited immutable evidence still pass offline
validation without journal changes or native queries. Retry
`FinalVerify -ReviewedFirmwareDifference` through the normal dispatcher; its
existing recovery rule accepts interrupted FinalVerify in phase `Disarmed`.
No manual journal repair, baseline replacement, new rollback, or additional
reset is required for this writer failure.

The operator's retry completed at `2026-10-20T21:37:47Z` with
`FINAL_RESET_VERIFIED_REVIEWED_FIRMWARE_DIFFERENCE_GPU_UNATTESTED`, success,
phase `Complete`, no active intent, and no verification errors. The saved
record retains `TextEqual: false` and records review acceptance separately.
Archive then returned `SELECTOR_ARCHIVED`; the archive exists, the active
selector is absent, and the completed journal is preserved. This closes the
run's recovery workflow without attesting GPU isolation or resolving the
historical raw BootOrder limitation.

## Second physical run: entry returned EFI_ABORTED

Run `gpu-20261020T150318Z-59a346ed3cb8422bafe4be60268b7df1` used clean source
`d79806d51187963cd81578b7287076e17558c2e2`. Unlike the first run below, its
collected launch diagnostic selects the failing phase:

```text
schema=1
payload_launch=start
payload_status=0x8000000000000015
shim_load_phase=firmware_load
shim_load_status=0x0000000000000000
```

Firmware image loading succeeded. The resident launcher then called the
relocated PE entry directly, and that entry returned `EFI_ABORTED`. This path
does not call firmware `StartImage`; its `ExitData` cannot explain this failure.
The diagnostic file was 130 bytes, SHA-256
`7E7C99AF5816BA4539E070A3AF80AB89F67870AA4020384BF720157DAA97651A`.
The collected payload matched the staged signed image, SHA-256
`242DF01C0B66C4DB48EB04DD1ED954F35DE657462D63637DDEE50AE991CF2FED`.

The operator's boot photo shows SHA-256 PCRs 0 through 7 as nonzero. Each `1`
in `11111111` means that at least one digest byte is nonzero. It does not mean
the digests contain all `FF` bytes. The loader's PCR guard therefore refused
replay. `IB_PAYLOAD_REPLAY_INDEPENDENT=ON` allowed the separate payload attempt,
so replay refusal alone does not explain its returned error. No TPM clear or
weaker PCR check is justified by this evidence.

The exact internal startup check remains unknown. BSP binding, private paging,
state invariants, and AP resource/preparation failures can return the same
`EFI_ABORTED`. The existing hypervisor startup messages use COM1 and port
`0xE9`; this run has no captured serial trace. Failure cleanup frees the context
containing the preparation result. Later fail-stop paths halt instead of
returning, so a clean return to Windows does not select one of those paths.

After rollback and the operator's reset, FinalVerify passed expected ABSENT
health on all 20 CPUs, Secure Boot, the boot-manager hash, and the textual
firmware baseline captured with the ESP mounted at `S:`. The selector was
archived and the journal remains `Complete`. GPU isolation remains unattested.
The retained seven-file ESP stage and all run evidence must be preserved. The
older retirement helper is pinned to the first run and must not be used on
this run.

The sections below retain the first investigation and its earlier diagnostic
coverage. The second run narrows the failure to the payload's internal startup;
it does not establish a specific DMA, EPT, GPU, or CPU failure.

## Observed run and recovery

Run `gpu-20261020T031023Z-9a2a92a17bf54b4faf4b60c6b7231ec7` was prepared from
`c6e65a2b265d1f3c13ac83bfe61c69281907c003`. The operator performed the physical
boot. This investigation used saved copies and source; it did not execute
another EFI payload or change host boot configuration.

The signed payload was 185,192 bytes, SHA-256
`3CB7AC7866CB296C5E6075D1FD299C482A86BE906DF806FBBE1D90B12C4D0E31`.
Stage readback, Arm, and the collected ESP copy agree on these bytes. The
collected `esp-handoff-status.txt` contains:

```text
schema=1
state=READY_BOOT_MANAGER
replay=ABORTED
restore=PASS
payload=FAILED
exit_boot_services_calls=0
```

The post-boot probe reported the test hypervisor absent on all 20 logical CPUs.
After disarm and reset, FinalVerify returned
`FINAL_RESET_VERIFIED_GPU_UNATTESTED`, with Secure Boot enabled, expected ABSENT
health, unchanged Windows Boot Manager, and matching textual firmware
enumeration. The journal is `Complete`. These are recovery checks, not evidence
of successful payload execution or GPU isolation.

Evidence is under `.analysis/gpu-baremetal/runs/<run-id>` in the primary checkout.
The relevant action directories are `20261020T032900399Z-Collect-*` and
`20261020T035702484Z-FinalVerify-*`. Preserve the journal and receipts.

## Diagnosis and its limits

The exact failure cause cannot be recovered from the saved summary. The
production caller in `components/uefi-loader/src/loader/efi_entry.c` discarded
the launch category and raw `EFI_STATUS` returned by `image_launch.c`.
`shim_chain.c` also retained its load gate/status only in memory. Several
failures produce the same `payload=FAILED` value: a filesystem presence/read
error, PE validation, device-path construction, shim/firmware image loading,
loaded-image validation, or an EFI error returned by the payload entry point.
There is no saved host serial or console trace selecting one of these paths.
The constant `exit_boot_services_calls=0` is not a measured child counter and
cannot establish whether its entry point ran.

The deployed loader cache has `IB_PAYLOAD_REPLAY_INDEPENDENT=ON`; its payload
branch runs despite replay refusal. The remembered gate/replay-aborted message
does not establish the reason for the payload failure. The matching collected
payload also does not support a missing or altered staged image as the cause.

Source-supported native startup candidates include strict DMA admission,
allocation failures, PAT/MTRR compatibility for WC mappings, and an already
active firmware VT-d unit. In particular, `iommu_runtime.c` rejects units with
TES/QIES/IRES already set; physical IOMMU preparation maps installation failure
to an EFI error. None is established as the cause of this boot. Do not weaken
those checks or disable platform protection based on these candidates. An
ordinary GPU discovery `EFI_UNSUPPORTED` is optional in `efi_hypervisor.c` and
does not by itself explain failure of the entire payload.

VM run 21 used different artifacts, native GPU/output confirmation off, and
Secure Boot off. It passed EFI/root WC rendering, model/latch checks, and
four-CPU Windows health with VMware SVGA and build 26200.9457. The physical run
used native/output confirmation on, Secure Boot on, 20 CPUs, and build
26200.9711. The VM result does not validate the failed signed native-enabled
configuration. Real UHD 770 scanout, physical Intel DMA isolation, and the
dual-adapter topology remain unverified.

## Initial launch diagnostic coverage

The loader now prints and attempts to write the optional
`EFI\insecure-boot\payload-diagnostic.txt` before inventory and Windows handoff.
Its bounded, allocation-free formatter records five ASCII lines, for example:

```text
schema=1
payload_launch=load
payload_status=0x800000000000001A
shim_load_phase=security2_lookup
shim_load_status=0x800000000000000E
```

This example is not a result from the failed boot. The legacy handoff status
schema/bytes are unchanged. Diagnostic errors cannot change the launch verdict
or bypass trust, ownership, DMA, or WC checks. A write error is printed and
does not prevent the existing Windows handoff. Collect preserves the optional
file with a verified hash; older runs without it remain collectable. The file
is not GPU attestation or a freshness guarantee: if writing failed, an existing
file may be stale. Correlate it with console output and the reviewed stage/run.

Launch phases are `skipped`, `absent`, `presence_error`, `argument`, `not_found`,
`read`, `device_path`, `image_format`, `load`, `protocol`, `start`, and `ok`.
In the resident path, `start` means the direct payload entry returned an EFI
error; firmware StartImage is not used there. The shim trace is captured before
another child load overwrites it. Pre-load failures use `not_applicable` and
`EFI_NOT_STARTED`, so an older load trace is not attributed to the payload.

Headless tests cover exact records, raw errors, bounds, stale-trace handling,
resident failure cleanup, and optional evidence capture. No new VM or physical
boot was performed for this instrumentation. Future disposable-VM validation
should exercise the signed Secure Boot chain with native build options; absent
Intel hardware must still be unsupported. If a later diagnostic reaches
`start`, hypervisor startup-stage output is still needed to distinguish its
internal checks. No further recovery command or physical retry is required
for the completed run.

The instrumentation revision built successfully with MSVC as an unsigned
loader. CTest passed 29/29 loader and 32/32 hypervisor checks. The existing
signed hypervisor payload passed its static `.hvscan` audit at 54,870 bytes;
its binary, renderer, and scanner sources were unchanged. The new loader was
not signed, staged, or executed.

## Startup diagnostic extension

The loader now supplies an optional versioned 80-byte record through the
child's `EFI_LOADED_IMAGE_PROTOCOL.LoadOptions` during the synchronous direct
entry call. It restores both original LoadOptions fields immediately after
return, validates the record against the actual returned EFI status, and copies
valid data before unloading a failed child. The existing resident launch API
and the original five schema-1 lines remain compatible.

New launched children append `startup_record=valid`, `unavailable`, or `invalid`
to `payload-diagnostic.txt`. Only a valid record appends startup version, stage,
reason, status, observer processor index, detail, CPU counts, and resource,
preparation, and activation result fields. Output is bounded to 1,024 bytes.
An old payload that leaves the initialized record untouched is unavailable;
missing or malformed data cannot be presented as a known startup stage.

The BSP records setup phases from context allocation through AP preparation and
VMX launch. Safe failure paths snapshot scalar results before cleanup frees
the context, then mark a returned result only after cleanup completes. The
success path takes its context snapshot before VMLAUNCH because that context
is EPT-denied after successful guest return. AP callbacks and resident VMX-root
code do not access the loader-owned record. No context ABI, resident scanner,
renderer, admission checks, or fail-stop behavior is changed.

`startup_processor` identifies the observed BSP/current processor, not
necessarily a failing AP; `4294967295` means unavailable. Counts and status
fields are the existing context snapshot, not new per-CPU attestations.
`startup_detail` is stage-dependent: at `dma_admission` it is the raw DMA
admission-state value; at `ap_prepare` it identifies a bounded BSP substep
defined in `efi_boot_diagnostic.h`. Preparation-failure numbers retain their
existing meanings in `efi_hypervisor.h`. These values narrow a returned error
without authorizing a weaker check.

This transport cannot persist a fail-stop or a boot that never returns to the
loader. File-write failure can still leave a stale diagnostic file. It cannot
retroactively recover the second run's missing startup reason or prove GPU
ownership, scanout, or DMA isolation. Another physical boot is not part of this
investigation increment.

The native-enabled Release payload and loader built with MSVC 19.51.36248.
The loader used replay independence on and debug ports off, matching those
options in the second physical run. CTest passed 37/37 hypervisor checks and
30/30 loader checks. The unsigned payload passed the `.hvscan` self-containment
audit at 54,870 bytes. Separate mutation checks confirmed that the lifetime
contract test rejects an AP diagnostic write, premature cleanup-return marker,
and a context dereference after successful guest return. These initial static
artifacts were not signed or booted. Subsequent VM validation uses separate
signed copies with loader debug ports enabled, as recorded below.

## VM validation transport failure

Candidate `d4c8101c406694d2cec7c80343f4586c33ad3f37` built and signed through the
existing VM-only workflow, passing both component suites and unsigned/signed
image audits. A fresh independent copy of the reviewed offline Windows VM
verified all 93 files, totaling 58,094,667,483 bytes. Validation run
`gpu-payload-20261020T182528Z-7d1ce3dea92145b6840364370c03dbcb` then passed
guest inspection but failed while returning the Stage result:

```text
vmtoolsd info-set failed: Total guest info exceeds maximum allocated space
```

The request ZIP was 762,672 bytes, transported as 1,016,896 Base64 characters
across 85 chunks. The worker retained those request values while publishing
response evidence. The saved authenticated failure identifies an `info-set`
failure during response publication, but does not identify the failed chunk
or manifest write. The subsequent diagnostic request also failed to allocate
GuestInfo space. This was a VM transport failure before the controller's signed
EFI stage, not evidence of another payload startup rejection.

The controller created before/after snapshots, reverted to before, and confirmed
the VM off with no cleanup failures. The failed run and its used preparation
receipt were archived; the VM and all evidence remain on disk. Its result is
`FAIL`, SHA-256
`B4B47BE2E020E4BA152C8376AFA193ABA98C6025EE397C76175F4D84E2592B5F`.

The follow-up transport change releases only the authenticated Stage request's
current-RunId chunk values after the VM/SYSTEM and package checks pass, before
publishing setup diagnostics. The verified local package remains available.
The controller also clears a saved failed response's owned mailbox values
before requesting Diagnose, after preserving any late inspection evidence.
The focused controller suite passes 114 pure
checks, including exact chunk names, rejection without writes, write failure,
cleanup ordering, and producer/consumer chunk-size agreement.

The subsequent fresh VM execution at candidate
`06a13691ccbaa328c00789ed5c4688af3b50b015` passed Stage after releasing its
85 consumed request chunks, then passed signed EFI execution and authenticated
four-CPU Windows health. Its 545-byte payload diagnostic reports a valid
version-1 record, `startup_stage=return`, `startup_reason=none`, and EFI status
zero. The EFI serial and authenticated Windows copies contain the same startup
fields. Both snapshots were created, baseline revert completed, and the VM was
confirmed off without cleanup failures. Graceful shutdown timed out and used
the bounded hard-stop fallback; graceful shutdown remains unvalidated.

See [the startup diagnostic VM result](GPU_STARTUP_DIAGNOSTIC_VM_RESULT.md)
for pinned artifacts, paths, and limits. This validates the new diagnostic
transport in the nested VM. It does not recover the original physical
rejection reason or validate Intel physical GPU isolation. The archived
physical run's journal and archive hashes remain unchanged.
