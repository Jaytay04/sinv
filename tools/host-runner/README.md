# Combined UEFILoader/Hypervisor host runner

This runner provides repeatable host-controlled tests against the two VMware
guests and the WSL2/KVM environment used by this project:

- Linux builds OVMF and boots a staged EFI target under nested QEMU with swtpm. It verifies the positive empty-PCR-bank gate and the P5 replay, PE/COFF rehash, divergence-manifest, and handoff-mechanics gates.
- `RunLinuxHypervisorSmoke` builds both components, passes the production loader's
  payload handoff to `uefi_hypervisor.efi`, then launches the fixed
  `uefi_hypervisor_probe.efi` as the restored boot-manager image. The gate requires
  ordered VMX-launch, BSP-virtualization, probe-CPUID, and loader-production-handoff
  markers in captured debugcon evidence.
- `RunLinuxHypervisorPostEbsSmoke` uses a runtime-resident probe to call
  `ExitBootServices`, execute the hypervisor health CPUID afterward, and terminate
  nested QEMU through a bounded debug-exit device. This is a distinct gate because
  the normal handoff cannot return to firmware after boot services have ended.
- `RunLinuxHypervisorPostEbsInterruptSmoke` stages the dedicated
  `uefi_hypervisor_post_ebs_interrupt_probe.efi` through the same production
  loader flow, then requires the exact post-`ExitBootServices` guest-interrupt
  guard marker before terminating QEMU through debug-exit status 37. It is kept
  separate from the health-CPUID gate because the guard exercises a different
  resident interrupt path.
- `RunLinuxNestedWindowsHypervisorE2E` is the VMware nested-Windows gate. It
  always rebuilds the loader, the real `uefi_hypervisor.efi`, and the static
  Windows probe/audit tools, then stages the hypervisor as `payload.efi` on the
  immutable prepared Windows disk workflow. It requires ordered VMX-launch,
  BSP-virtualization, and payload markers, an exact
  `UEFI_HYPERVISOR_WINDOWS_CPUID:PASS` line from Windows processor 0, and the
  existing nested TPM/audit result. On multi-vCPU guests it additionally
  requires successful firmware topology discovery, a read-only VMX readiness
  preflight, resource ownership, and AP-local CPU/control plus private
  host-descriptor preparation on every enabled AP. Processor-state admission
  also validates EPT/unrestricted-guest support, the firmware-derived UC/WB
  policy, populated AP-local EPT storage, and the derived four-level WB EPTP.
  The hypervisor keeps EPT and unrestricted-guest execution enabled through
  AP startup. The stub-payload baseline
  remains `RunLinuxNestedWindowsE2E`.
- Windows runs the native TBS probe or capture tool against the VM's vTPM
  through VMware Guest Variables. Requests and responses use bounded,
  base64-encoded chunks with an authenticated manifest commit marker. No
  Windows account password is stored or passed to VMware Guest Operations.
- `RunWindowsDirectHypervisorE2E` performs the real loader-to-hypervisor-to-
  Windows cycle in the disposable VMware guest with 20 vCPUs. Setup uses the
  authenticated Guest Variables mailbox while VMware Tools is healthy. The
  post-handoff health proof uses a temporary dedicated COM2 named pipe instead,
  so it does not depend on VMware Tools while Windows runs under the monitor.
- `RunWindowsDirectScannerE2E` uses the same boot and authenticated control
  path, invokes the bounded read-only physical scanner, and publishes success
  before detach. It isolates scanner validation from the shutdown gate while
  retaining automatic VM stop, snapshot restore, and topology restoration.
- The installer host gate signs test copies with `signtool`, validates the embedded Authenticode signer against the supplied public MOK certificate without modifying Windows trust stores, and verifies transactional ESP staging with the vendored Ubuntu shim.

The public interface exposes fixed actions only. Live runs use exact per-VM shared leases, bounded timeouts, SHA-256-pinned staging, and sticky quarantine after uncertain failures. A passing run stops the VM; the Windows profile then restores its configured clean snapshot.

The Linux and Windows profiles may run concurrently. Each keeps its own VM lease for the full lifecycle. Power-on is serialized with the short `host-vm-startup` lease so the runner can recheck the known running-VM set and available memory immediately before starting. The checked-in policy allows at most two known VMs and preserves 1024 MiB of free host memory after accounting for the next VM's configured `memsize`. An unknown running VM, a running known VM without its matching lease, or insufficient memory fails closed.

## Machine-local setup

The checked-in host-runner.config.example.json points `ProjectRoot` at this
combined repository and keeps `UefiProjectRoot` and `HypervisorProjectRoot` under
it. Copy it to host-runner.config.json and adjust only values that differ locally.
Keep each profile's `LeaseResource` equal to the shared mailbox identifier for that
physical VM; aliases would defeat concurrency protection. The machine-local config,
credentials, logs, and evidence are ignored by Git.

The repository includes only the example configuration. Create the ignored
machine-local `host-runner.config.json` before invoking live actions.
`VmwareGuestInfo` is the passwordless Windows transport. It uses the stored
32-byte mailbox credential only as an HMAC key; it does not use a shared-folder
host path or a Windows guest credential.

An existing UEFILoader runner can be migrated without displaying or decrypting
its DPAPI credential files. Credential copying requires an explicit switch, and
an existing target configuration is never replaced unless `-Force` is supplied:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\host-runner\Initialize-LocalRunner.ps1 `
  -SourceConfigPath C:\path\to\UEFILoader\tools\host-runner\host-runner.config.json `
  -AuthorizeCredentialImport
~~~

Store credentials once from an interactive PowerShell session:

~~~powershell
$Runner = '.\tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action StoreGuestCredential -Profile Linux
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action StoreVmCredential -Profile Windows
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action CredentialStatus -Profile All
~~~

Credentials are protected for the current Windows user with DPAPI. The Linux
profile uses its guest credential. The Windows mailbox profile uses only the
encrypted-VM password for host-side power and snapshot operations; it does not
need a Windows sign-in password. `vmrun` still receives the encrypted-VM
password as a process argument while it is running, so use a dedicated test-VM
password.

### Passwordless Windows guest preparation

Install `guest\Invoke-UEFILoaderGuestInfo.ps1` with
`guest\Install-UEFILoaderGuestMailbox.ps1` from an elevated session inside the
disposable VM. The installer compiles and registers the fixed
`UEFILoaderGuestMailbox` Windows service as automatic `LocalSystem`, dependent
on VMware Tools. The service supervises only the fixed worker and configuration
paths, so a powered-off snapshot starts without an interactive sign-in,
autologon, or a Windows password. The worker reads and writes only the fixed
`uefiloader.request.*` and `uefiloader.response.*` Guest Variables through
`vmtoolsd.exe`.

The host publishes every request chunk before its base64 JSON manifest. The
manifest is the commit marker; the guest validates the HMAC, chunk sizes/counts,
package SHA-256, and fixed ZIP allowlist before running the fixed probe. The
guest publishes one bounded `evidence.zip` response stream, then its signed
response manifest. Host and guest clear the exact variable names after the run.

The clone can be bootstrapped once through its pre-existing Hypervisor mailbox
by the fixed `guest\gate7-uefiloader-guestinfo-bootstrap.ps1` gate. Reserve both VM
leases during that one-time migration because the legacy mailbox is shared
with the original VM. After the dedicated service is verified, remove the clone's
legacy `HypersinvRunner` share and give its unused serial pipe a unique name
before concurrent testing.

Power off the prepared clone and create a dedicated snapshot with the bounded
runner action. The action refuses an existing name and verifies the new
snapshot after creation:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action CreateWindowsSnapshot -SnapshotName UEFILoader-GuestInfo-Clean-v7 -AuthorizeSnapshotCreate
~~~

The Linux guest requires VMware Tools and the same packages as the positive-path runbook:

~~~bash
sudo apt update
sudo apt install -y git build-essential uuid-dev nasm acpica-tools python3 qemu-system-x86 ovmf swtpm swtpm-tools tpm2-tools dosfstools mtools efi-shell-x64
~~~

The account must have access to /dev/kvm without sudo, and the UEFI Shell must exist at /usr/share/efi-shell-x64/shellx64.efi.

## Local checks

~~~powershell
$Runner = '.\tools\host-runner\Invoke-UEFILoaderHostRunner.ps1'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Doctor -Profile All
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action Build
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\host-runner\tests\Test-GuestInfoMailbox.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\host-runner\tests\Test-HostRunnerContracts.ps1
~~~

## Live tests

Linux positive path:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxPositivePath -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

Combined EFI hypervisor smoke (builds and stages both EFI images, then runs the
existing patched-OVMF/swtpm positive path):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxHypervisorSmoke -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

The single-vCPU smoke validates the BSP-only fallback and therefore still
requires the `BSP ONLY` processor-resource, processor-state, and
AP-virtualization markers. The production two-vCPU path additionally uses UEFI
MP Services to enumerate topology, preflight the AP, allocate independent
runtime processor resources, prepare private host/guest descriptors, build the
AP's 39-bit (512-GiB) identity EPT, and launch the AP before BSP VMX entry.

Post-`ExitBootServices` BSP smoke (requires the resident descriptor package and
expects QEMU's dedicated PASS debug-exit status):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxHypervisorPostEbsSmoke -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

This gate proves one intercepted CPUID after `ExitBootServices`. It does not
claim `SetVirtualAddressMap`, AP virtualization, or a complete Windows boot.

Post-`ExitBootServices` guest-interrupt guard smoke (requires the resident
descriptor/interrupt package and expects QEMU's dedicated PASS debug-exit status):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxHypervisorPostEbsInterruptSmoke -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

This gate proves the dedicated guest-interrupt guard reaches its exact PASS marker
after the production payload launch. It does not claim general guest interrupt
delivery, AP virtualization, `SetVirtualAddressMap`, suspend/resume, or a complete
Windows boot; QEMU exits immediately after the guard marker, so boot-manager and
final loader PASS markers are intentionally not required.

P5 synthetic replay dry-run (executes the H/R/H callback sequence, then runs a
separate EFI PCR checker before shutdown to prove every active PCR 0-7 remains
at its reset value):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxReplayDryRun -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

P5 PE/COFF rehash gate (stages the installed Windows `bootmgfw.efi`, pins its
SHA-256 in the run manifest, then verifies stock OVMF reproduces every recorded
active-bank digest from the pre-relocation ESP bytes):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxPeRehash -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

P5 divergence-manifest gate (performs one real raw PCR extend for a synthetic
firmware-blob event, persists `\EFI\insecure-boot\divergence.ib`, and validates
both the manifest record and PCR mutation):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxDivergenceManifest -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

P5 handoff-mechanics gate (loads a strict `tcglog.ib`, performs replay and
persists divergence evidence, restores a deterministic boot-manager stub by a
verified temporary-file transaction, then starts the payload and restored
boot-manager stubs in order):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxHandoffMechanics -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

P5 production-loader handoff gate (runs the actual `loader.efi` entry point,
including the hard PCR gate, strict reference-log load, UEFI replay adapter,
stale-divergence cleanup, post-replay PCR verification, persisted
divergence/status evidence, verified restore, and ordered handoff):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxProductionHandoff -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

P7 shim one-shot cycle gate (starts the vendored shim from the Microsoft boot
manager path, loads the sibling production loader, proves shim measurements
trigger the non-empty-PCR hard gate, enforces the recorded backup hash,
restores the original boot manager, and completes fail-closed handoff):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxOneShotCycle -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

P11 full replay-to-Windows and production BootNext one-shot gates under
WSL2/KVM. Both use an immutable prepared Windows VMDK plus per-run overlays:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWslNestedWindowsE2E -AuthorizeLiveVmRun -AuthorizeVmStop
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWslBootNextE2E -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

VMware nested Windows hypervisor E2E (always rebuilds current artifacts; do not
pass `-SkipBuild`):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunLinuxNestedWindowsHypervisorE2E -AuthorizeLiveVmRun -AuthorizeVmStop
~~~

Direct VMware Windows hypervisor E2E (requires the disposable snapshot and
explicit permission to stop and restore the VM):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsDirectHypervisorE2E -Profile Windows -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
~~~

Scanner-only runtime gate:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsDirectScannerE2E -Profile Windows -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
~~~

When the Windows profile defines `KernelProfileImage` and
`KernelProfilePdb`, the direct gate requires both files to remain under
`ProjectRoot`, builds the EFI payload with the configured
`ExpectedWindowsBuild`/`ExpectedWindowsUbr`, and proves an authenticated lookup
of the guest `System` process before reporting PASS.

The accepted compatibility topology is 20 vCPUs. `-WindowsDirectProcessorCount`
also accepts 1 and 4 only for bounded regression diagnosis; those settings do
not broaden the compatibility claim.

This gate also builds the standalone HVR1 WDM companion and service. Inside
the disposable VM, setup creates a short-lived test certificate, trusts only
its public half, signs the companion, and enables test signing for the one
boot. The startup verifier first executes an authenticated read-only runtime
scan over allowlisted RAM, rejects a corrupted HMAC, performs authenticated
detach on all 20 processors, rejects replay of the consumed sequence, proves
the hypervisor CPUID contract disappeared, unloads the driver, removes the
temporary certificate, and reports the complete result over the authenticated
COM2 frame. Setup also leaves a deliberate `STARTING` recovery record before
the real reboot. Verification must prove the volatile boot identity changed,
that the stale record was safely recovered, and that power requests remained
held until `OFFLINE`. No certificate private key leaves the VM.

To service the disposable Windows image to the exact host-compatible build,
use the separately bounded workflow:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsExactBuildUpdate -Profile Windows -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
~~~

This action admits only `KB5124008` and verifies build `26200.9445`. It uses an
HMAC-authenticated GuestInfo status record, a fixed `SYSTEM` task with explicit
and boot triggers, at least 20 GiB of guest-system-drive free space, and
per-update HRESULT reporting. Setup publishes an authenticated `QUEUED` state
before starting the asynchronous task; the task must advance it within five
minutes and writes an authenticated local breadcrumb before every VMware Tools
publication. The host first requires a same-run authenticated status handshake
and atomically preserves every observed stage in
`exact-build-status-live.json`. Before installing the LCU, the fixed worker
runs an in-box DISM health check followed by SFC. A captured full DISM online
repair already reaches 100% and returns `0x800F0915` on the pinned
`26200.9168` baseline because matching repair content is unavailable, so the
gate does not repeat that 24-minute non-remediating operation. The health-check
result is preserved but does not mask the LCU test; after SFC the worker stops
only the fixed Windows Update services, rotates only `SoftwareDistribution`
and `catroot2`, then retries the one allowlisted KB. Successful completion
removes those same-run backups. A failure status includes a bounded, fixed
CBS/DISM diagnostic tail. If servicing powers the VM off after an authenticated
`INSTALLING` or `REBOOTING` state, the runner starts the same update delta once
and admits only one signed transition back to `STARTING`; this handles VMware
missing the short-lived pre-reboot record without accepting completion until
the exact post-boot build is authenticated.

If the host runner itself is interrupted while the VM is in an authenticated
`INSTALLING` or `REBOOTING` state, leave the serviced delta intact and resume it
with the explicit quarantine-recovery action:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action ResumeWindowsExactBuildUpdate -Profile Windows -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert -AuthorizeQuarantineClear
~~~

The resume action verifies the quarantined run identity and persisted HMAC
status, acquires the exact VM lease before removing quarantine, and starts only
the retained current delta. Any subsequent failure still stops and reverts the
VM; successful completion records `ResumedFromQuarantine` and preserves the
verified delta for snapshot capture.

The runner temporarily enables VMware NAT only for servicing and
restores only the adapter fields after the VM stops; it never rewrites
VMware's current-disk or snapshot fields. Failure stops and reverts the
disposable VM. Success preserves the serviced delta so it can be captured as a
new named snapshot after exact-build verification.

The runner temporarily applies the fixed 20-vCPU topology, enables nested VMX,
disables Secure Boot, and adds an otherwise unused serial1 pipe. Before reboot,
the authenticated setup request installs a one-shot `LocalSystem` probe. After
Windows boots through `loader.efi` and `uefi_hypervisor.efi`, the probe requires
20 unique per-processor health lines and one aggregate PASS. Its bounded COM2
result is HMAC-authenticated with the existing mailbox key. Success and failure
both stop the VM, restore the clean snapshot, and restore the complete original
VMX, including removal of serial1.

Exact-profile run `uefi-windows-probe-20260914-141057-66674c14fc` passed on
Windows `26200.9445` with all 20 processors active, authenticated all-CPU
detach, replay rejection, all 20 post-detach CPUID checks absent, aggregate
CPUID absence, and clean companion unload. Post-fix diagnostic run
`uefi-windows-probe-20260914-141911-59e0da5be2` repeated the same contract on
four vCPUs after the detach barriers and debug-diagnostic cleanup.

A PASS from this gate proves the real EFI hypervisor remains active across the
Windows handoff on all 20 processors in the exact 20-vCPU profile. Before BSP
launch it requires topology discovery, AP VMX preflight, processor-resource
ownership, processor-local state preparation, and `AP virtualization: PASS`.
Each AP owns an independent context, VMX regions, stacks, page tables, private
descriptors, and a 514-page EPT hierarchy identity-mapping the full 39-bit
(512-GiB) guest-physical space. UEFI-derived UC/WB 2-MiB policy leaves cover
that entire range, and only completely WB 1-GiB ranges are coalesced; uncovered,
reserved, and MMIO ranges remain UC.
Every processor receives a unique nonzero VPID so intercepted CPUID and
control-register exits preserve guest translations across VM resume.
VMX/SMX stay hidden from Windows, while the containing VMware hypervisor identity
and paravirtual CPUID leaves remain visible for virtualization-aware services.

The resident AP path handles architectural INIT reset, wait-for-SIPI and SIPI
activation, duplicate SIPI, CR0/CR4 fixed-bit virtualization, and EFER.LMA
transition. EPT and unrestricted guest remain enabled across reset,
protected-mode, and long-mode transitions. INIT first enters wait-for-SIPI
through a minimal no-debug-I/O resume path; the complete reset VMCS update is
deferred until SIPI is captured so closely spaced nested IPIs are not lost.
The payload also publishes the WB 2-MiB EPT policy-leaf count before
handoff. After Windows starts, the gate requires 20 distinct
`UEFI_HYPERVISOR_WINDOWS_CPU:<group>:<processor>:PASS` lines and one aggregate
`UEFI_HYPERVISOR_WINDOWS_CPUID:PASS` line.
Nested VMX can delay Windows user-mode startup by several minutes on the
validated runner. The workflow retains the bounded 15-minute Windows budget
and 30-second per-vCPU register/LAPIC/IRQ samples. The prepared Windows image
contains `ib-nested-e2e-service.exe`, a statically linked auto-start service
host that connects to the Service Control Manager and launches the bounded
capture script as `LocalSystem`. Its bytes are embedded in the existing
allowlisted mailbox package, copied into the persistent guest directory, and
recorded by SHA-256 in prepared-disk provenance. Conditioned overlays use the
`conditioned-v4` cache generation so older task-only images cannot be reused.

Run `uefi-linux-nested-windows-hypervisor-e2e-20260912-214144-fee1284832`
passed the full gate. Its evidence contains two distinct processor-local CPUID
PASS lines, aggregate CPUID PASS, complete TPM capture/audit output, attributed
PCR divergence, ordered loader/hypervisor launch proof, and a PASS postboot
result. Treat the action as a passing integration gate only for the exact
pinned image and two-vCPU nested-KVM profile.
If Windows Boot Manager returns `0xc000000d`, the control and hypervisor
workflows preserve the first-attempt diagnostics and perform one fresh-overlay
retry. The hypervisor workflow also retries a clean poweroff after every
required loader/hypervisor launch marker but before any guest evidence. VMX
fatal/stall signals, incomplete launch markers, a failed retry, and every other
evidence failure remain terminal.
It does not claim larger processor topologies, general Windows compatibility,
APIC/device emulation, or a production-host-safe execution path. The EFI
runtime now has authenticated all-CPU detach, direct interrupt/device
pass-through, guest-EPT self-protection, and exact-profile physical VT-d
mediation. Suspend is supported as detach-before-suspend policy, not live VMX
resume, and crash recovery is quarantine plus reset. Run it only on the
disposable, nested-virtualization-enabled VM described above; failures retain
the normal lease/quarantine recovery behavior.

P6 installer host gate (uses ephemeral test signing material, performs
trust-independent signer pinning, refuses direct MOK enrollment, and stages a
fresh ESP model with the pinned Ubuntu shim):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunInstallerHostGate
~~~

P8/P9 defensive-audit host gate (runs all six detector seams, including the
strict baseline JSON round-trip and mapped drift fixture, and proves the
production `ibaudit dump` CLI accepts a clean log while naming the exact
EventNum/PCR for a tampered digest):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunAuditHostGate
~~~

P10 generated-fixture gate (generates deterministic clean, tampered,
downgrade, expected-PCR, and baseline-drift artifacts; independently parses
and folds them; then runs the production dump detector and attack/clean
detector seams):

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunFixtureHostGate
~~~

Windows TBS environment probe:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsProbe -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
~~~

Windows TCG-log capture, once the probe reports the expected environment:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action RunWindowsCapture -AuthorizeLiveVmRun -AuthorizeVmStop -AuthorizeSnapshotRevert
~~~

Evidence is written beneath .analysis\host-runner\runs\<RunId>\evidence. When
stop and snapshot-revert authorization are present, a failed live run
hard-stops the exact VM, restores its configured snapshot, verifies it is off,
releases the lease, and writes `failure-recovery.json`. If deterministic
recovery cannot complete, the runner retains the lease and creates
.analysis\host-runner\QUARANTINED-<profile>.json. Inspect and recover the VM
first. Once the VM is off and the retained lease has expired, clear the marker
explicitly; the action verifies the lease identity, takes over that exact
expired lease, releases it, and only then removes quarantine:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Runner -Action ClearQuarantine -Profile Linux -AuthorizeQuarantineClear
~~~

Computer-use automation is not required. The runner controls builds and VMs through PowerShell, CMake/CTest, and VMware's vmrun interface.

## Current boundary

The full WSL2/KVM replay gate now proves the real production loader reaches
Windows and that the Windows startup verifier attributes the post-replay PCR
divergence. The production BootNext gate proves the Windows firmware-variable
API, one-shot loader execution, normal Windows handoff, and owned-entry
cleanup. VMware remains the native negative replay control because its firmware
populates PCR0-7 before the loader. MOK enrollment is intentionally interactive
through shim MokManager and is not automated by the Windows installer.
