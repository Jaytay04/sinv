# EFI runtime companion (HVR1)

This directory contains the optional, standalone Windows WDM companion for
the EFI-resident hypervisor.  `hvr1_driver.c` builds a separate
`hvr1_efi_runtime_companion.sys`; it does not link or launch the historical
driver.  The parent project must opt in to this directory explicitly.

## Closed boundaries

The companion supplies a bounded transport and a fail-closed native shutdown
rendezvous:

1. A LocalSystem service submits an HVR1 mailbox containing runtime-control
   metadata.  The driver validates the fixed header, bounds, generation,
   strictly increasing sequence, and HMAC-SHA-256 tag before copying it to its
   own nonpaged allocation.  The tag covers a boot-random nonce, the canonical
   header, and the complete bounded payload. The exact EFI ABI then authorizes each processor with
   the root-owned four-word boot token.
2. The mailbox is one 64 KiB physically contiguous nonpaged allocation.  Its
   physical address is retained in kernel state and is never returned by an
   IOCTL.  The final 16 KiB is a 4096-entry acknowledgement table indexed by
   the Windows system processor index.  The root writes `ACK_OK` only after
   native state restoration.
3. `KeIpiGenericCall` is the shutdown rendezvous.  On every active processor,
   the callback performs that processor's QUERY, then one interrupts-disabled
   assembly transaction issues PREPARE and the immediately following COMMIT.
   No C code, diagnostic I/O, or pageable access can run while the one-shot
   host pivot is armed. An all-CPU PREPARE barrier precedes COMMIT, and an
   all-CPU VMXOFF barrier prevents any callback from returning to Windows while
   another processor remains virtualized. PREPARE is deliberately not
   performed once on the
   caller, because the root token, VMCS, host pivot stack, CR shadows, and
   root-local processor identity are CPU-specific. UEFI MP-services numbering
   is not assumed to equal the Windows system processor index; the per-CPU
   token proves same-CPU ownership, while ACK storage uses the Windows index.
4. Root PREPARE programs HOST_RIP/RSP/CR3 to `Hvr1NativeDetachPivot` and the
   per-CPU pivot stack.  COMMIT enters that pivot; it VMCLEARs the VMCS,
   VMXOFFs, clears only CR4.VMXE, restores CR0, writes the unique CPU ACK, and
   jumps to the wrapper continuation.  A normal VMCALL return is not proof of
   success.
5. A missing ACK, malformed response, stale token/generation, duplicate CPU
   slot, or VMX failure enters `QUARANTINED`.  The instance does not retry or
   free pivot resources in that state.  Only `OFFLINE` (or a companion that
   never acquired a session) is unloadable.

## CPL0 EFI ABI

The assembly wrapper uses the existing `RAX=HV_EFI_CALL_MAGIC` contract and
places the opcode in `RCX`.  Values below are 64-bit registers.

### QUERY_SESSION (opcode 3)

| Register | Input | Output |
| --- | --- | --- |
| RAX | `HV_EFI_CALL_MAGIC` | token0 |
| RBX | — | token1 |
| RCX | 3 | token2 |
| RDX | — | token3 |
| R8 | — | ABI version |
| R9 | — | capabilities |
| R10 | — | generation |
| R11 | — | current processor index |

### SHUTDOWN_PREPARE (opcode 4)

| Register | Input | Output |
| --- | --- | --- |
| RAX | `HV_EFI_CALL_MAGIC` | VMCS physical address |
| RBX | token1 | guest CR0 |
| RCX | 4 | guest CR4 |
| RDX | token0 | status |
| R8 | token2 | current processor index |
| R9 | token3 | generation |
| R10 | pivot RIP | ABI version |
| R11 | pivot RSP | capabilities |
| R12 | generation | — |

### SHUTDOWN_COMMIT (opcode 5)

The second VMCALL is issued immediately on the same processor.  The wrapper
sets R8/R9 to its continuation RIP/RSP and R12 to the unique ACK slot.

| Register | Input |
| --- | --- |
| RAX | `HV_EFI_CALL_MAGIC` |
| RCX | 5 |
| RDX | pointer to VMCS physical-address operand |
| R10 | actual guest CR0 |
| R11 | actual guest CR4 |
| R8 | native continuation RIP |
| R9 | native continuation RSP |
| R12 | per-CPU ACK address |

The pivot treats VMCLEAR or VMXOFF failure as fatal: it writes
`ACK_FAILURE`, disables interrupts, and halts.  It never returns to guest
code with a partially restored host state.

### RUNTIME_SCAN (opcode 6)

The companion pins the submitting thread to one processor, queries that
processor's token, and immediately issues the scan VMCALL on the same CPU.
RDX/RBX/R8/R9 carry token words 0-3, R10 carries the page-aligned physical
address of the driver-owned mailbox, R12 carries the runtime generation, and
R13 carries the monotonic request sequence. RAX returns the bounded scanner
status. VMCALL already exits unconditionally, so no MSR/I/O intercept or new
VMCS execution control is enabled.

### PROCESS_LOCATE (opcode 7)

The LocalSystem client submits only a bounded 1-15 byte image name. After
mailbox HMAC, generation, sequence, and framing validation, the kernel
companion uses `AuxKlibQueryModuleInformation` to locate `ntoskrnl.exe`,
validates its mapped PE headers, and appends its base plus the running Windows
build and UBR. It pins the thread, queries the processor-local session, and
immediately issues opcode 7 on that same processor.

VMX root captures `VMCS_GUEST_CR3` and `VMCS_GUEST_CR4`, verifies the generated
kernel profile against the live PE/RSDS identity, translates guest virtual
addresses with the read-only page-table walker, and performs the bounded
`ActiveProcessLinks` traversal. The response containing PID and directory
table base is covered by the same boot-scoped HMAC. No physical mapping or raw
CR3 field was added to the generic IOCTL descriptor/status ABI.

### SHADOW_CONTROL (opcode 8)

The companion performs this operation on every active logical processor with
`KeIpiGenericCall`. Each processor receives a separate physically contiguous
mailbox slot, queries its processor-local token, and invokes opcode 8 against its
private shadow EPT. Root enables CR3-load exiting only while bound, switches to
the alternate EPTP only for the cached target CR3, and executes single-context
INVEPT plus INVVPID after a switch. Any EPT violation in the alternate view
reverts that processor to identity EPT before retrying the guest instruction.

`BIND`, `UNBIND`, and `QUERY_STATUS` are HMAC authenticated and monotonic. Bind
requires a PID previously cached by `PROCESS_LOCATE`; PID 4, addresses outside
the boot RAM allowlist, and ranges intersecting hypervisor-private allocations
fail closed. A partial all-CPU bind triggers a second all-CPU unbind rendezvous.
The alternate view never changes an HPA mapping or grants permissions absent
from identity EPT. In particular, it is not a root-memory read path: root reads
continue through `HvEfiScannerMapReadOnly`, and removing guest write permission
is observable as a VM exit.

### ANALYSIS_CONTROL (opcode 9)

The companion pins the caller to one processor, verifies the processor-local
analysis capability, and issues opcode 9 with the same token, generation, and
sequence register contract as scan and process lookup. The fixed HVA1 payload
supports `START`, `CONFIGURE`, `SCAN`, `TRAVERSE`, `EXTRACT`, `RESULTS`, and
`STOP`. Results remain in hypervisor-private storage and only bounded,
authenticated pages are copied into the response mailbox.

`START` requires a non-System PID previously cached by `PROCESS_LOCATE`.
`CONFIGURE` carries up to 16 masked signatures and 256 optional region-evidence
records. Root derives committed mappings and effective access permissions from
the target CR3. Region labels such as heap, stack, mapped, private, guard, and
reserved are evidence fields because hardware PTEs alone cannot prove those
Windows allocation semantics.

## IOCTL ABI and device security

`hvr1_protocol.h` is the versioned, packed, WDK-free wire definition.

- `HVR1_IOCTL_GET_MAILBOX` returns mailbox capacity, header size, the next
  generation and sequence, and a boot-scoped shared HMAC session key/nonce. It never
  returns a physical address. The key is available only through the
  exclusive LocalSystem-only handle and is never persisted.
- `HVR1_IOCTL_SUBMIT` accepts `HVR1_IOCTL_REQUEST` followed immediately by a
  complete HVR1 mailbox. Shutdown accepts an empty payload; scan, process lookup,
  shadow control, and analysis accept their fixed mailbox payloads and return authenticated
  responses through the same buffered IOCTL.
- `HVR1_IOCTL_QUERY_STATUS` returns state, rendezvous counts, and the crash
  marker.
- All three controls use `METHOD_BUFFERED` and read/write access.

The standalone WDM device is exclusive and uses the security descriptor
`D:P(A;;GA;;;SY)`: only LocalSystem receives access.  Dispatch forwards the
single buffered system buffer to `Hvr1CompanionHandleIoctl`; no user-mode
physical mapping is exposed.

## LocalSystem service and CLI

`hvr1_runtime_service.exe` is a separate standard Win32 executable.  It does
not install or start itself.  An installer must register it to run as
LocalSystem, which is also enforced by the device ACL.  It supports:

- `hvr1_runtime_service.exe service` — enters the SCM service dispatcher,
  opens the device once, and holds the exclusive handle for its lifetime.
- `hvr1_runtime_service.exe status` — reads the persisted recovery record and
  queries `HVR1_IOCTL_QUERY_STATUS` for test diagnostics.
- `hvr1_runtime_service.exe shutdown` — obtains the bounded descriptor,
  submits a header-only HVR1 request, then requires `OFFLINE`, matching
  expected/acknowledged CPU counts, and zero root status before success.
- `hvr1_runtime_service.exe scan "48 8B 05 ?? ?? ?? ??" 0 0 first` scans
  allowlisted physical RAM in bounded continuations and prints matching
  physical addresses. Modes are `first`, `all`, and `single`; start/end are
  physical addresses and an end of zero selects the scanner's physical limit.
- `hvr1_runtime_service.exe process explorer.exe` performs build-pinned,
  read-only process enumeration and prints the authenticated PID, process CR3,
  and EPROCESS address. It fails closed if the EFI image has no generated
  profile or the running kernel build/UBR/PE/PDB identity differs.
- `hvr1_runtime_service.exe shadow-bind PID [GPA_START SIZE]...` binds the
  cached process on every processor. Each optional range is page-aligned,
  allowlisted guest physical RAM that becomes non-writable in the alternate
  view. A bind with no ranges creates an identity-equivalent alternate view.
- `hvr1_runtime_service.exe shadow-status` returns authenticated state, active
  flag, table/range counts, and violation/synchronization counters.
- `hvr1_runtime_service.exe shadow-unbind` disables CR3-load exits and restores
  identity EPT on every processor.
- `hvr1_runtime_service.exe analysis-start PID` starts a process-bound session.
  `analysis-configure ID "PATTERN" [REGION_MASK]` loads a wildcard signature;
  `analysis-scan first|single|all [MAX_BYTES]` performs the bounded scan.
  `analysis-traverse`, `analysis-extract`, and `analysis-results [OFFSET]`
  expose safe traversal, typed reads, and paginated findings. `analysis-stop`
  wipes the private session state.
- `hvr1_runtime_service.exe selftest-shutdown` is a VM-validation command that
  consumes a prior-boot recovery marker, acquires the active-session power
  requests, proves a corrupted HMAC is rejected without changing state,
  performs the authenticated all-processor detach, proves reuse of the
  consumed sequence is rejected, and releases the power requests only after
  `OFFLINE`. `arm-recovery-test` creates the deliberate same-boot `STARTING`
  record consumed after the gate's real reboot. Both commands are intended
  only for the disposable direct-boot gate; `selftest-shutdown` destroys the
  active VMX session.

The service registers `SERVICE_ACCEPT_POWEREVENT`,
`SERVICE_ACCEPT_PRESHUTDOWN`, and `SERVICE_ACCEPT_SHUTDOWN`.  It synchronously
attempts the same shutdown on `PBT_APMSUSPEND`, `PBT_APMSTANDBY`, preshutdown,
and shutdown controls.  Partial or failed detach is recorded as
`QUARANTINED`, returns failure, and is never retried automatically.

Modern Windows offers no vetoable query-suspend callback to an ordinary
service.  These notifications are therefore best-effort preparation, not a
guaranteed manual-sleep veto. While root mode is active, the service owns both
system-required and execution-required power requests to prevent unattended
sleep. It releases them only after every processor reports `OFFLINE`.

Before opening the device, the service reads
`HKLM\SOFTWARE\Hypersinv\Hvr1RuntimeCompanion\Lifecycle`.  The normal-context
registry record stores lifecycle state, generation, sequence, CPU ACK counts,
and a volatile boot identity. A restart during the same boot after `STARTING`,
`DETACHING`, or `QUARANTINED` refuses unsafe reuse. After a machine reboot,
the volatile identity changes, proving that stale VMX state cannot have
survived; the service records recovery and starts a fresh boot-scoped session.
Only a successful per-CPU `OFFLINE` verification writes the next `OFFLINE`
record. Bugcheck callbacks never write this record.

Power query/set requests delivered to this device call
`Hvr1CompanionPrepareForPower`.  A registered nonpageable integration callback
must quiesce devices, submit the shutdown mailbox, and return only after the
state is `OFFLINE`.  No callback or an incomplete callback denies that
request and quarantines a failed attempt.

This standalone control device is not a PnP power-stack filter and Windows
does not guarantee that it receives every system sleep transition.  Therefore
the `IRP_MJ_POWER` handler is a fail-closed local guard, not a claim that the
driver can veto system suspend by itself.  A production integration must
wire a supported system power-notification/service path that invokes
`Hvr1CompanionPrepareForPower` before requesting suspend, hibernate, or
shutdown; that orchestration is outside this standalone `.sys`.

The bugcheck reason callback records evidence only.  It does not take locks,
allocate, send an IPI, issue VMCALL, issue VMXOFF, or attempt recovery.  A
crash therefore requires reboot. On the next boot, the volatile boot identity
proves reset and the service replaces the stale lifecycle record; a same-boot
restart remains quarantined. WDM unload has no status return, so the unload
routine contains an explicit OFFLINE gate; service code must complete the
shutdown before requesting driver unload.

## Authentication boundary

The companion generates a fresh key and nonce with the Windows system RNG at
driver creation. The LocalSystem service obtains that boot-scoped session over
the exclusive secured handle, signs the canonical header plus any payload, and
immediately clears its returned descriptor. The driver verifies `AuthTag` in
constant time and rejects stale sequences before any IPI or VM call. The EFI
root independently validates its 256-bit per-boot token for PREPARE and scan.

This is an authenticated control API inside the trusted Windows kernel TCB.
It protects against unprivileged callers, malformed requests, tag forgery,
and replay; it does not claim resistance to hostile kernel code or another
LocalSystem process that has already compromised the service.

## Build and validation

`CMakeLists.txt` is intentionally local.  It creates the WDM target with a
`DriverEntry` linker entry point and never uses the parent's KMDF
`FxDriverEntry` helper.  The target is skipped when the WDK is unavailable.

`test_hvr1_contract.ps1` and `hvr1_abi_contract.c` validate the wire layout,
source safety, per-CPU sequencing, and security boundaries without a WDK.
`test_hvr1_service_contract.ps1` validates the service source, lifecycle
record, notification controls, and CLI bounds; it also compiles the Win32
source when MSVC is available.  `hvr1_vmcall.asm` is assembled by the
optional WDK build.  No signing keys, VM disks, registry exports, or
generated driver/service artifacts belong in source control.
