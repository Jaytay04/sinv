# UEFILoader host runner

This runner provides repeatable host-controlled tests against the two VMware
guests and the WSL2/KVM environment used by this project:

- Linux builds OVMF and boots a staged EFI target under nested QEMU with swtpm. It verifies the positive empty-PCR-bank gate and the P5 replay, PE/COFF rehash, divergence-manifest, and handoff-mechanics gates.
- Windows runs the native TBS probe or capture tool against the VM's vTPM
  through VMware Guest Variables. Requests and responses use bounded,
  base64-encoded chunks with an authenticated manifest commit marker. No
  Windows account password is stored or passed to VMware Guest Operations.
- The installer host gate signs test copies with `signtool`, validates the embedded Authenticode signer against the supplied public MOK certificate without modifying Windows trust stores, and verifies transactional ESP staging with the vendored Ubuntu shim.

The public interface exposes fixed actions only. Live runs use exact per-VM shared leases, bounded timeouts, SHA-256-pinned staging, and sticky quarantine after uncertain failures. A passing run stops the VM; the Windows profile then restores its configured clean snapshot.

The Linux and Windows profiles may run concurrently. Each keeps its own VM lease for the full lifecycle. Power-on is serialized with the short `host-vm-startup` lease so the runner can recheck the known running-VM set and available memory immediately before starting. The checked-in policy allows at most two known VMs and preserves 1024 MiB of free host memory after accounting for the next VM's configured `memsize`. An unknown running VM, a running known VM without its matching lease, or insufficient memory fails closed.

## Machine-local setup

The checked-in host-runner.config.example.json contains the detected paths for this workstation. Copy it to host-runner.config.json and adjust only values that differ locally. Keep each profile's `LeaseResource` equal to the shared mailbox identifier for that physical VM; aliases would defeat concurrency protection. The machine-local config, credentials, logs, and evidence are ignored by Git.

The current local config is already present. `VmwareGuestInfo` is the
passwordless Windows transport. It uses the stored 32-byte mailbox credential
only as an HMAC key; it does not use a shared-folder host path or a Windows
guest credential.

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
