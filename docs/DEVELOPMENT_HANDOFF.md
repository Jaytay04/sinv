# Hypervisor development handoff

This document is the technical entry point for an agent continuing development
of `hsinv`. It describes the implementation as it exists in source, the
interfaces between components, the validated scope, and the remaining work.

## Current 26200.9457 migration checkpoint (2026-09-23)

Work in this checkpoint is in `C:\Users\jayli\OneDrive\Documents\Projects\hsinv`.
The host reports Windows 11 25H2 build `26200.9457`; the existing disposable
VM and its only snapshot are still `26200.9445`. Do not run the direct EFI gate
against that snapshot while the runner expects `.9457`.

Changes made in `hsinv` for this checkpoint:

- `components/hypervisor/src/efi/efi_hypervisor.h` increases
  `HV_EFI_RESIDENT_CODE_PAGES` from 2 to 3; the matching assertion in
  `src/efi/tests/test_efi_hypervisor_math.c` now expects 3 and checks the
  current `HV_EFI_CONTEXT` size of 1176 bytes. The linked 8,356-byte resident
  image exceeded the old 8,192-byte allocation. The header's
  `offsetof(UserCommandState) == 1168` assertion remains unchanged.
- `tools/host-runner/host-runner.config.example.json` now uses `hsinv` paths,
  targets build `26200.9457`, and keeps the Windows profile disabled with no
  snapshot name until a matching snapshot is verified. Its source contract
  test reflects that staged configuration and still checks invalid transport
  and build metadata with the test fixture explicitly enabled.
- The ignored local `tools/host-runner/host-runner.config.json` was initialized
  with three existing DPAPI-protected credential files. Its Windows profile
  points at the host-derived `.9457` image/PDB, but remains disabled; its old
  `.9445` snapshot name is retained only to identify the currently available
  rollback point. Do not enable this profile until replacing that snapshot
  reference with a verified `.9457` snapshot.
- The host `C:\Windows\System32\ntoskrnl.exe` was copied to ignored
  `.analysis/pinned-kernel-profile/ntoskrnl.exe`. Microsoft's matching
  `ntkrnlmp.pdb` was fetched and verified by `symchk` (CodeView GUID
  `C29EBFB0-6B78-B3C0-20DC-A66D99713F9E`, age 1), then copied beside it.
  The existing `kernel_profile_generator` produced a `26200.9457` header and
  JSON manifest under `.analysis/kernel-profile-generated/`; no profile bytes
  were hand-edited.

Static validation: the `.9457`-pinned EFI image builds under
`.analysis/efi-9457-build`, including its generated profile. CTest passes
20 of 20 tests after correcting the stale structure-size expectation.
The direct VM gate has **not** run on `.9457`, and no VM was started during
this checkpoint. The source-only `Test-HostRunnerContracts.ps1` suite also
passes after the example change.

The historical `Invoke-WindowsExactBuildUpdate.ps1` and its host action remain
unchanged and pinned to `26200.9445` / `KB5124008`; they cannot provision
`.9457`. [Microsoft's `KB5129195`](https://support.microsoft.com/en-us/servicing/os/windows-11/2026/09/kb5129195-windows-11-24h2-25h2-security-update)
is the cumulative update for `.9457`.
Provision and verify a disposable `.9457` Windows guest through a separate
controlled servicing route, retain a clean snapshot, set that snapshot name
in the local config, and only then re-enable its Windows profile and run the
leased direct gate. Preserve the `.9445` snapshot as rollback evidence.

## 1. Project identity and safety model

`hsinv` combines two independently buildable components:

- `components/uefi-loader` owns the UEFI boot/loading path and the disposable-VM
  host runner.
- `components/hypervisor` owns the EFI-resident Intel VMX monitor, its pure-model
  tests, the optional Windows companion, and kernel-profile generation.
- `tools/overlay` is an independent Win32/D3D11 visualization executable. It is
  not linked into the EFI image or companion driver.

The root `CMakeLists.txt` is an `ExternalProject` superbuild. Do not merge the
components with `add_subdirectory()`: they intentionally use separate compiler,
runtime, test, and optional WDK configurations.

The hypervisor is an EFI-resident, Type-1-style monitor: it enters VMX root
before Windows starts and runs the booted Windows environment in VMX non-root.
It is not a general multi-VM platform. There is one Windows guest, identity EPT
for its normal physical view, no virtual device model, and no scheduler for
multiple guests.

All privileged execution is VM-only. Never make the physical development PC the
first target for an EFI, VMX, VT-d, detach, or companion-driver change. Use the
pinned disposable VMware image, nested virtualization, a snapshot, and a
controlled network.

## 2. Architecture

### 2.1 Component and data flow

```text
                         build time
  ntoskrnl.exe + PDB ------------------------------+
       |                                            |
       v                                            v
  kernel_profile_generator                  kernel_profile_gen.h
  (DbgHelp, DIA fallback)                    + JSON audit manifest
                                                    |
                                                    v
  UEFI loader -----------------------> EFI hypervisor image
                                            |
                                            | VMXON/VMCS per CPU
                                            | identity EPT + private host PT
                                            v
                                  Windows in VMX non-root
                                            |
               +----------------------------+--------------------------+
               | Windows kernel                                        |
               v                                                       |
  LocalSystem control client                                           |
               | authenticated METHOD_BUFFERED IOCTL                    |
               v                                                       |
  HVR1 companion driver -- AuxKlib kernel base/build ------------------+
               |
               | pinned contiguous mailbox + same-CPU VMCALL
               v
  VMX-root dispatch
       | opcode 6       physical signature scanner
       | opcode 7       process lookup / EPROCESS walk
       | opcode 8       per-CPU alternate EPT control
       | opcode 9       process-bound analysis session
       v
  hypervisor-private, EPT-denied state
               |
               | bounded authenticated response mailbox
               v
  LocalSystem control client

  LocalSystem control-client publisher
               | HVA1-to-HVO2 adapter + bounded transforms
               | HVO2 Global/Local named mapping, read-only client ACL
               v
  D3D11/Direct2D overlay -- world-to-screen projection and rendering
```

There are two different objects called a mailbox/mapping and they must not be
confused:

1. The HVR1 control mailbox is a physically contiguous, driver-owned nonpaged
   allocation used for authenticated kernel-to-VMX-root requests and responses.
2. The HVO2 overlay mapping is a Windows named shared-memory object consumed
   read-only by `hypersinv_overlay.exe`.

VMX root does **not** create or write a Windows named mapping. It cannot safely
depend on Windows object-manager services. The LocalSystem `analysis-publish`
command receives authenticated HVR1 results and publishes visualization records
to HVO2. The overlay never receives the device handle or authentication
material.

### 2.2 Boot and VMX ownership

The combined build passes the loader's UEFI support directory into the
hypervisor component and builds `uefi_hypervisor.efi`. Before Windows handoff,
the EFI payload:

- validates the admitted Intel VMX/processor/topology profile;
- allocates per-CPU VMXON, VMCS, host stack, context, descriptor, identity EPT,
  alternate EPT, and private host page-table resources;
- removes every hypervisor-owned physical range from guest EPT;
- installs root-owned host CR3, GDT/IDT/TSS state and the resident VM-exit entry;
- launches every admitted processor and then continues Windows boot in
  non-root mode.

The resident assembly handler owns VMCS reads/writes, CR3-load interception,
EPTP switching, EPT-violation fallback, VMCALL dispatch, health probes, and
native detach pivots. C++ modules implement bounded policy and parsing logic;
the copied resident section is audited to ensure it has no external references
that would become invalid after EFI boot services are gone.

### 2.3 Physical memory access

VMX root does not dereference arbitrary guest physical or virtual addresses.
All analysis reads follow this path:

```text
guest virtual address
    -> GuestPtWalker (guest CR3, CR4, MAXPHYADDR)
    -> guest physical address
    -> boot-derived RAM allowlist minus hypervisor-private ranges
    -> HvEfiScannerMapReadOnly
    -> temporary root page-table mapping, read-only and NX
    -> bounded copy
    -> unmap
```

Identity EPT is not VA-to-PA translation. EPT translates guest physical to host
physical addresses for non-root execution; the guest page tables translate
Windows virtual addresses. Today the normal EPT is identity mapped, so a
translated guest physical address has the same numeric host physical address.
Code must not bake that equivalence into the page-table walker.

### 2.4 Guest page-table walker

`guest_pt_walker.cpp` implements an IA-32e four-level walk:

- strips CR3 PCID bits;
- masks entries according to CPUID `MAXPHYADDR`;
- validates canonical virtual addresses and reserved bits;
- supports 4-KiB PTE, 2-MiB PDE, and 1-GiB PDPTE leaves;
- accumulates effective user/write/execute permissions across the walk;
- rejects `CR4.LA57` fail-safe;
- reads table entries only through the scanner's read-only mapping primitive;
- rereads parent entries during user-range enumeration and reports
  `TABLE_CHANGED` on concurrent mutation;
- performs page-by-page reads, never assuming adjacent virtual pages are
  physically contiguous.

The walker is the mandatory VA-to-PA layer for process enumeration, structured
traversal, typed extraction, and analysis scans.

### 2.5 Build-pinned Windows kernel profile

`tools/kernel_profile_generator` consumes an exact `ntoskrnl.exe` and matching
PDB. The normal backend uses DbgHelp (`SymLoadModuleEx`, `SymGetTypeFromName`,
`SymGetTypeInfo`, and `SymFromName`). If DbgHelp extraction fails and the Visual
Studio DIA SDK is available, the tool retries through DIA.

The generator:

- parses the PE CodeView/RSDS record;
- verifies PDB GUID and age;
- extracts `_EPROCESS` size and the `Pcb`, `ActiveProcessLinks`,
  `UniqueProcessId`, and `ImageFileName` offsets;
- extracts `_KPROCESS.DirectoryTableBase` and calculates the corresponding
  `_EPROCESS` offset;
- resolves the `PsActiveProcessHead` RVA;
- records build, UBR, image timestamp, and image size;
- emits `kernel_profile_gen.h` and `kernel_profile_manifest.json`;
- fails the build on missing symbols, mismatched identities, inconsistent
  offsets, or malformed image metadata.

The generated header is included by the EFI target and is not checked into
source control. At runtime, process lookup verifies the live kernel's build,
UBR, PE timestamp/size, and RSDS identity before using any generated offset.

### 2.6 Process enumeration

The authenticated process operation uses VMCALL opcode 7:

1. The LocalSystem client asks the companion to locate a 1-15 byte image name.
2. The companion calls `AuxKlibInitialize` and
   `AuxKlibQueryModuleInformation`, identifies `ntoskrnl.exe`, validates its PE
   headers, and adds its live base, Windows build, and UBR to the request.
3. The calling thread is pinned to one CPU, obtains that CPU's session token,
   and issues the VMCALL.
4. Root captures `VMCS_GUEST_CR3` and `VMCS_GUEST_CR4`, validates the pinned
   kernel profile, and calculates `kernel_base + PsActiveProcessHeadRva`.
5. The page-table walker reads `PsActiveProcessHead` and follows
   `_EPROCESS.ActiveProcessLinks` with canonical-address, physical-range,
   cycle, backlink, and 10,000-iteration limits.
6. For each process, root reads PID, 15-byte image name, and the build-specific
   directory-table base. Records are retained in EPT-denied private memory.
7. The authenticated response returns the matching PID, EPROCESS VA, and CR3.

The cached process record is subsequently required by shadow-EPT bind and
analysis start. PID 4 (`System`) is rejected for those process-bound features.

### 2.7 Signature scanners

There are two related scanner entry points:

- Opcode 6 is a direct physical-RAM scanner. It accepts up to 64 bytes, a
  wildcard bitmask, `[start,end)`, a per-exit byte budget, and first/single/all
  behavior. Long work continues with `MORE_DATA`.
- Opcode 9's analysis scanner is process/VA oriented. It accepts up to 16
  64-byte pattern-plus-mask signatures, alignment/access flags, signed data
  offsets, region filters, result limits, scan-byte limits, and TSC timeouts.

Both reuse the Boyer-Moore-Horspool engine in `scanner.cpp`. Analysis scans
walk present user mappings, translate and read page by page, retain at most 63
overlap bytes across pages, and return guest VA plus translated physical
address. Wildcards are mask bytes in HVA1 and `?`/`??` tokens in the CLI.

### 2.8 Process-bound alternate EPT

Each logical processor owns two EPT views:

- **Identity EPT:** the normal 512-GiB guest-physical-to-host-physical view.
- **Alternate EPT:** a cloned hierarchy with the same leaf frame mappings.

The alternate view never grants an R/W/X bit absent from identity EPT. Selected
allowlisted physical ranges may have write permission removed. If the identity
leaf is already readable and executable, this creates an R-X leaf. This is a
write-restricted guest view, not a root-memory access mechanism.

Authenticated opcode 8 binds the alternate view to a previously cached process
CR3 on every active CPU. While bound, MOV-to-CR3 exiting is enabled. A matching
CR3 selects the alternate EPTP; every other CR3 selects identity EPT. CR3 PCID
bits are ignored. Each transition performs single-context INVEPT and INVVPID.

An alternate-view EPT violation immediately fails safe to identity EPT. Writes
are counted and retried using the original identity permissions. Read/execute
violations attempt one synchronization and otherwise revert to identity. No
guest exception is injected by this path. Removing write permission is
observable because the write causes a VM exit; “transparent” means the guest
instruction retains its original semantics after fallback, not that timing is
undetectable.

Both EPTs map the same backing page-table frames, so Windows page-table content
changes are naturally shared. `HvEfiShadowEptSynchronize` is for hypervisor EPT
policy changes: it reclones identity EPT and reapplies configured permissions.

### 2.9 Authenticated runtime analysis

Opcode 9 provides one serialized, process-bound HVA1 session. Its state is a
32-page EPT-denied allocation containing signatures, region records, results,
traversal nodes, and extracted fields.

Operations are:

| Operation | Purpose |
| --- | --- |
| `START` | Bind a fresh session to a cached non-System PID and discover present user mappings. |
| `CONFIGURE` | Replace signatures and optional authenticated region evidence. |
| `SCAN` | Run first/single/all masked scanning under byte/time/result bounds. |
| `TRAVERSE` | Walk a supported linked, array, tree, or graph structure. |
| `EXTRACT` | Decode one typed value at a validated user VA. |
| `RESULTS` | Return paginated regions, matches, nodes, and fields. |
| `STOP` | Zero and release logical session state. |

The session supports:

- singly linked lists;
- doubly linked lists in either direction with backlink checks;
- contiguous arrays, pointer arrays, and 2-D arrays;
- binary trees and two-child graph enumeration;
- cycle detection and node/iteration limits;
- `float`, `double`, signed 32/64-bit integers;
- `vec3`, `vec4`, and 4x4 float matrices;
- aligned, canonical user pointers;
- NUL-terminated ASCII and validated UTF-8;
- 8-, 16-, and 32-bit length-prefixed byte strings.

Fatal translation, mapping, timeout, or structural-integrity errors terminate
and wipe the session. Results never become generally mapped Windows memory;
only bounded pages are copied to the authenticated response mailbox.

Present PTEs can prove mapping and effective R/W/X/user access. They cannot
prove Windows VAD ownership, heap membership, thread-stack ownership, guard
state, or reserved state. Those labels are accepted only as authenticated
region evidence in `CONFIGURE`. Up to 2,048 coalesced mappings are retained;
truncation is explicit and does not silently widen scan scope.

### 2.10 Authenticated control plane

The optional HVR1 WDM companion exposes an exclusive LocalSystem-only device
with security descriptor `D:P(A;;GA;;;SY)`. The user/kernel transport uses
versioned, packed, `METHOD_BUFFERED` IOCTL structures.

At driver creation, Windows RNG produces a boot-scoped HMAC-SHA-256 key and
nonce. The LocalSystem client obtains the descriptor, signs the canonical
header and bounded payload, submits a strictly increasing sequence, and clears
its local descriptor. The driver validates framing, bounds, generation, HMAC,
and replay state before making any privileged call.

The EFI runtime independently validates its per-boot 256-bit CPU-local token.
The companion pins single-CPU operations to the queried processor. All-CPU
shadow and detach operations use `KeIpiGenericCall` with separate per-CPU
mailbox/acknowledgement slots.

| VMCALL opcode | Capability |
| --- | --- |
| 3 | Query processor-local session/token/capabilities. |
| 4/5 | Two-stage authenticated native detach. |
| 6 | Bounded physical signature scan. |
| 7 | Build-pinned process lookup. |
| 8 | All-CPU alternate-EPT bind/status/unbind. |
| 9 | Process-bound dynamic analysis. |

Authentication prevents unprivileged use, malformed payloads, unauthenticated
commands, and replay. It is not a boundary against malicious kernel code or a
compromised LocalSystem process that already owns the exclusive service.

### 2.11 Overlay

`tools/overlay` builds `hypersinv_overlay.exe` using only Win32, D3D11, DXGI,
Direct2D, and DirectWrite. It provides:

- a borderless topmost `WS_POPUP` window;
- `WS_EX_LAYERED`, `WS_EX_TRANSPARENT`, `WS_EX_TOOLWINDOW`, and
  `WS_EX_NOACTIVATE` styles;
- `HTTRANSPARENT` click-through behavior;
- a cryptographically randomized class name per instance;
- client-area tracking for a PID, HWND, title substring, or initial foreground
  window;
- per-monitor DPI awareness and resize handling;
- hardware D3D11 with WARP fallback;
- premultiplied BGRA Direct2D rendering and DirectWrite text;
- boxes, lines, and text with `0xAARRGGBB` colors;
- explicit 60 FPS pacing;
- read-only HVO2 shared-memory consumption;
- world-space records projected through an optional row-major 4x4 matrix.

The styles remove the overlay from Alt-Tab and the taskbar and prevent normal
activation. They do not hide it from diagnostic APIs such as `EnumWindows`.
There are no hooks, DLL injection, remote process reads, or overlay kernel
driver.

HVO2 uses `Local\HypersinvOverlayDataV2` by default, a fixed maximum of 256
entities, and
an odd/even sequence protocol. The reader accepts only matching magic, version,
header size, entity stride, capacity, and reserved fields. It copies a snapshot
only when the same even sequence is observed before and after the copy. Torn or
invalid frames render empty rather than stale data.

## 3. Completion status

### 3.1 Implemented and tested

- EFI boot integration and Intel VT-x VMX-root operation.
- Per-CPU VMXON/VMCS, host state, root stacks, and resident exception guard.
- 512-GiB identity EPT with guest denial of hypervisor-owned memory.
- Four-level guest page-table walker with 4-KiB/2-MiB/1-GiB support.
- Build-pinned kernel-profile generation with DbgHelp and DIA fallback.
- Live kernel identity validation and bounded EPROCESS process enumeration.
- PID, EPROCESS VA, and process CR3 extraction over authenticated control.
- Read-only physical Boyer-Moore-Horspool scanning with wildcard support.
- Process virtual-memory scanning with pattern masks and region/access filters.
- Linked-list, array, pointer-array, 2-D array, tree, and graph traversal.
- Scalar, vector, matrix, pointer, ASCII/UTF-8, and length-prefixed extraction.
- HVA1 `START`, `CONFIGURE`, `SCAN`, `TRAVERSE`, `EXTRACT`, `RESULTS`, and
  `STOP` operations.
- Per-CPU alternate EPT with CR3-triggered switching and fail-safe violation
  fallback.
- HMAC-authenticated companion transport, replay protection, all-CPU detach,
  lifecycle quarantine, and clean unload gate.
- AuxKlib live-kernel-base discovery in the companion.
- LocalSystem HVA1-to-HVO2 publisher with authenticated pagination, explicit
  cross-session read-only ACL, 60 Hz cap, and generation consistency checks.
- Rolling XOR, configurable FNV-1a 32/64, per-byte rotation, affine
  substitution, and ASCII/UTF-16LE formatting.
- Standalone D3D11/Direct2D overlay with tested HVO2 read-only IPC,
  world-to-screen projection, and rendering.
- Parameterized target transforms (rolling XOR, configurable FNV-1a 32/64,
  byte rotation, affine substitution/inverse, ASCII, and UTF-16LE) without
  target code executing in VMX root or the guest.
- Publisher-to-read-only-consumer-to-projection-to-D3D-present validation in
  4.932 ms on the build host, plus live pinned-VM extraction-to-publication in
  0.451 ms with zero 50 ms violations.
- Disposable Windows VM validation on build `26200.9445`, including a live
  process-bound signature match, translated physical address, alternate EPT,
  authenticated shutdown, replay rejection, and clean companion unload.

### 3.2 Remaining limitations and deliberate non-goals

- **Arbitrary transform execution:** target constants are configurable for the
  implemented bounded algorithms. Uploaded code, arbitrary bytecode, guest
  execution, and a root-side `ANALYSIS_DECRYPT` command remain prohibited by
  design rather than missing integration work.
- **Physical display scan-out instrumentation:** the pipeline is measured
  through successful D3D `Present`; physical panel scan-out is outside this
  software boundary and is not included in the latency claim.
- **Automatic VAD/heap/stack discovery:** PTE mappings are discovered. Semantic
  region labels require authenticated external evidence.
- **Five-level paging:** `CR4.LA57` is rejected.
- **Arbitrary Windows builds/hardware:** the validation claim is pinned to the
  documented Windows/VMware profile. A new kernel profile is necessary but not
  sufficient for arbitrary hardware compatibility.
- **General interrupt/device virtualization:** interrupts and devices use
  direct 1:1 pass-through; there is no APIC or device emulation framework.
- **Undetectable EPT timing:** write-restricted alternate ranges cause VM exits.
- **General multi-VM isolation:** current isolation protects hypervisor-owned
  memory; it is not a tenant isolation platform.

## 4. Build integration

### 4.1 Combined build

```powershell
cmake --preset combined-windows
cmake --build --preset combined-windows
cmake --build --preset combined-windows-tests
```

The combined defaults build the EFI path, disable the historical SCM loader,
and do not install/load a driver. `combined_hypervisor_efi` explicitly requests
the component target `uefi_hypervisor`.

### 4.2 Build with a pinned kernel profile

The kernel image must come from the target VM, and the PDB must match exactly:

```powershell
cmake --preset combined-windows --fresh `
  -DCOMBINED_KERNEL_IMAGE=C:\profiles\26200.9445\ntoskrnl.exe `
  -DCOMBINED_KERNEL_PDB=C:\profiles\26200.9445\ntkrnlmp.pdb `
  -DCOMBINED_KERNEL_BUILD=26200 `
  -DCOMBINED_KERNEL_UBR=9445 `
  -DCOMBINED_REQUIRE_KERNEL_PROFILE=ON
```

The component-level equivalents use the `HYPERSINV_` prefix. Generated output
is under the hypervisor build tree:

```text
src/efi/generated/kernel_profile_gen.h
src/efi/generated/kernel_profile_manifest.json
```

### 4.3 Companion

The companion is opt-in and requires the WDK:

```text
HYPERSINV_BUILD_EFI_RUNTIME_COMPANION=ON
HYPERSINV_BUILD_EFI_RUNTIME_SERVICE=ON
HYPERSINV_TEST_EFI_RUNTIME_COMPANION=ON
```

Building does not install, sign, or load the driver. Driver deployment belongs
only in the disposable-VM host-runner workflow.

### 4.4 Overlay

From an x64 Visual Studio developer prompt:

```powershell
cmake -S tools/overlay -B build/overlay -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/overlay
ctest --test-dir build/overlay --output-on-failure
```

## 5. Usage workflows

The CLI requires the companion device and therefore normally runs as
LocalSystem inside the disposable guest. Numeric arguments accept decimal or
`0x`-prefixed hexadecimal values.

### 5.1 Locate and start analysis for a process

```powershell
hvr1_runtime_service.exe process explorer.exe
# Record the authenticated PID from the response.

hvr1_runtime_service.exe analysis-start 1234
```

`analysis-start` succeeds only after `process` cached the same non-System PID.
The start response includes the discovered/paginated user mapping inventory.
`MORE_DATA` with `RegionsTruncated=1` is a bounded success state, not permission
to scan outside the returned/retained mapping set.

### 5.2 Configure and run signatures

```powershell
# IMAGE-only: REGION_FILTER(IMAGE) == 1 << 1 == 0x2
hvr1_runtime_service.exe analysis-configure image_anchor `
  "48 8B 05 ?? ?? ?? ?? 48 85 C0" 0x2

# Stop after the first match, scanning at most 16 MiB this call.
hvr1_runtime_service.exe analysis-scan first 0x01000000

# Retrieve paginated regions and findings.
hvr1_runtime_service.exe analysis-results 0
hvr1_runtime_service.exe analysis-results 64
```

Current CLI configuration loads one signature per `analysis-configure` call;
the underlying ABI supports 16. Extending the CLI to submit a set must preserve
the fixed HVA1 bounds and replace/append semantics chosen by root.

Region-filter bits are `UNKNOWN=0x1`, `IMAGE=0x2`, `HEAP=0x4`, `STACK=0x8`,
`MAPPED=0x10`, and `PRIVATE=0x20`; all is `0x3f`.

### 5.3 Traverse structures

```powershell
# Singly linked: start VA, next-pointer offset.
hvr1_runtime_service.exe analysis-traverse 0x000001ABC0001000 slist 0x18

# Doubly linked: start VA, forward offset, backward offset.
hvr1_runtime_service.exe analysis-traverse 0x000001ABC0001000 dlist-fwd 0x10 0x18

# Contiguous array: base VA, element count, byte stride.
hvr1_runtime_service.exe analysis-traverse 0x000001ABC1000000 array 128 0x30

# Pointer array: table VA, entry count, pointer stride.
hvr1_runtime_service.exe analysis-traverse 0x000001ABC2000000 ptr-array 64 8

# 2-D array: base VA, rows, columns, element stride.
hvr1_runtime_service.exe analysis-traverse 0x000001ABC3000000 array2d 16 16 4

# Binary tree/graph: root VA and two child-pointer offsets.
hvr1_runtime_service.exe analysis-traverse 0x000001ABC4000000 tree 0x10 0x18
hvr1_runtime_service.exe analysis-traverse 0x000001ABC4000000 graph 0x10 0x18
```

Returned nodes are guest virtual addresses. Every dereference is canonical,
user-range, alignment, translation, and allowlist checked.

### 5.4 Extract typed values

```powershell
hvr1_runtime_service.exe analysis-extract 0x000001ABC5000010 vec3 position
hvr1_runtime_service.exe analysis-extract 0x000001ABC5000040 mat4 transform
hvr1_runtime_service.exe analysis-extract 0x000001ABC5000090 ptr next_object
hvr1_runtime_service.exe analysis-extract 0x000001ABC5000100 utf8 label 64
```

Accepted types are `f32`, `f64`, `i32`, `i64`, `vec3`, `vec4`, `mat4`, `ptr`,
`ascii`, `utf8`, `lp8`, `lp16`, and `lp32`.

There is no root-side decryption command. The implemented workflow is:

1. Extract the minimum required bytes through authenticated, read-only HVA1.
2. Copy the bounded response into a trusted user-mode analysis process.
3. Apply one bounded LocalSystem transform (`xor`, configurable FNV-1a,
   rotate, affine substitute/inverse, or none).
4. Publish only visualization-ready values to HVO2.

If root-side decryption is ever justified, it should be a fixed, audited decoder
with explicit input/output bounds—not arbitrary uploaded code or a guest-code
execution mechanism.

### 5.5 Bind the alternate EPT view

```powershell
hvr1_runtime_service.exe process explorer.exe
hvr1_runtime_service.exe shadow-bind 1234 0x0000000123400000 0x2000
hvr1_runtime_service.exe shadow-status
hvr1_runtime_service.exe shadow-unbind
```

Ranges are guest-physical, page-aligned, within the boot RAM allowlist, outside
all hypervisor-private ranges, and capped by `HV_EFI_SHADOW_EPT_MAX_RANGES`.
Bind is all-CPU; partial failure triggers an all-CPU rollback attempt.

### 5.6 Render visualization

The overlay itself can be validated immediately:

```powershell
build\overlay\hypersinv_overlay.exe --demo
build\overlay\hypersinv_overlay.exe --target-pid 1234
```

Production visualization uses this implemented flow:

```text
HVR1 process/analysis response
    -> validate authenticated response and target generation
    -> Hvr1BuildOverlayFrame and bounded name transformation
    -> SharedMemoryPublisher(Global\HypersinvOverlayDataV2)
    -> publish SharedOverlayHeader + EntityData[] under odd/even sequence
    -> overlay OpenFileMapping(FILE_MAP_READ)
    -> world-to-screen projection and draw visible entities
```

Example, run as LocalSystem after populating the HVA1 session:

```powershell
hvr1_runtime_service.exe analysis-publish `
  "Global\HypersinvOverlayDataV2" 0x11234 10000 none ascii
```

The bridge applies a SYSTEM-full/Authenticated-Users-read mapping ACL, caps
entity count at 256, sets `TargetWindow`, caps publication at 60 Hz, and fails
instead of mixing pages when generation/PID/counts change. Field conventions,
transform grammar, algorithms, and vectors are in `docs/OVERLAY_PIPELINE.md`.

### 5.7 Stop cleanly

```powershell
hvr1_runtime_service.exe analysis-stop
hvr1_runtime_service.exe shadow-unbind
hvr1_runtime_service.exe shutdown
```

Shutdown is an all-CPU QUERY/PREPARE/COMMIT rendezvous. The companion may unload
only after every CPU reports `OFFLINE`. A missing acknowledgement or partial
detach enters `QUARANTINED` and requires a reboot; never retry an uncertain
same-boot detach.

## 6. Constraints and invariants

### 6.1 Read-only analysis boundary

- No API writes arbitrary guest memory.
- No guest code is patched, hooked, injected, or executed.
- All target reads use `GuestPtWalker` and `HvEfiScannerMapReadOnly`.
- Temporary root mappings are read-only and non-executable.
- Hypervisor-private pages are subtracted from scan ranges and denied in EPT.
- Bounded response mailboxes are the only intentional root-to-Windows writes.
- Alternate EPT may remove guest write permission but never adds permission or
  changes the mapped frame.

### 6.2 Timing and concurrency

- Analysis is single-session and serialized across vCPUs.
- Large scans must use byte/TSC budgets and continuation, not one unbounded VM
  exit.
- CR3-load exiting exists only while alternate EPT is bound.
- Alternate-view writes cause VM exits and therefore observable latency.
- INVEPT/INVVPID are mandatory after EPTP/policy changes.
- Page-table walks can race Windows updates and must fail with `TABLE_CHANGED`
  rather than accepting a mixed hierarchy.
- Fatal analysis errors wipe session state; callers must restart explicitly.

### 6.3 Windows build specificity

- Never copy offsets from another build or infer them from internet tables.
- The PE and PDB GUID/age must match.
- Runtime build, UBR, timestamp, image size, and RSDS identity must match the
  compiled profile.
- A profile enables structure interpretation only; it does not prove the rest
  of the OS/hardware combination is VMX-compatible.

### 6.4 Overlay synchronization

- The producer writes an odd sequence before payload mutation and an even
  sequence only after a full memory barrier.
- The reader maps with `FILE_MAP_READ` and never repairs producer state.
- ABI fields use fixed-width types; `isVisible` is a `uint32_t`, not C++ `bool`.
- Coordinates are physical pixels relative to the target client area.
- Colors are `0xAARRGGBB`.
- Invalid/torn frames render empty. Do not keep stale entity data indefinitely.
- `Local\` mappings are session-local. A service/session-zero producer needs a
  deliberate broker or `Global\` mapping plus an audited ACL; do not casually
  widen mapping access.

### 6.5 Lifecycle and recovery

- Suspend preparation detaches; resume does not silently re-enable VMX.
- Crash recovery is evidence plus reboot, not in-crash VMX teardown.
- The service holds power requests while root mode is active, but Windows does
  not guarantee a veto for every manual suspend path.
- WDM unload has no status return, so the unload routine enforces `OFFLINE`.
- No signing keys, generated profile headers, VM disks, or build trees belong
  in Git.

## 7. Source map and extension points

| Area | Primary files | Extension guidance |
| --- | --- | --- |
| EFI/VMCS resident core | `components/hypervisor/src/efi/efi_hypervisor.c`, `.asm`, `.h`, `.inc` | Keep C/ASM context offsets synchronized and extend copied-section audits for new resident calls. |
| Physical scanner | `scanner.h/.cpp`, `runtime_scanner.h/.cpp` | Add matching behavior to the reusable engine; preserve no-write/no-allocation guarantees in the resident path. |
| Guest translation | `guest_pt_walker.h/.cpp` | Add LA57 as a separate validated walk depth; add reserved-bit and large-page tests first. |
| Process lookup | `guest_process.cpp`, `runtime_process_abi.h` | New Windows fields belong in generated profiles, never hardcoded offsets. Preserve iteration/cycle/page bounds. |
| Alternate EPT | `shadow_ept.h/.cpp`, `runtime_shadow.*` | Never change HPA frames or grant identity-denied permission. Update INVEPT/INVVPID and all-CPU rollback tests. |
| Analysis | `runtime_analysis_abi.h`, `runtime_analysis.h/.cpp` | Add fixed enum/union cases, version or statically assert ABI layouts, and retain request/result caps. |
| Kernel profiles | `tools/kernel_profile_generator` and hypervisor CMake | Add fields to both backends, header/JSON emitters, validation, and runtime profile checks. |
| Companion/control | `src/driver/efi_runtime_companion` | Validate in user mode, driver, and root. Keep LocalSystem ACL, HMAC, generation, and sequence checks. |
| Overlay | `tools/overlay` | New primitives belong in `D3dRenderer`; keep the overlay read-only and independent of the companion device. |
| VM automation | `tools/host-runner` and `components/uefi-loader/tools/host-runner` | Preserve snapshot restoration, leases, evidence capture, exact build/topology admission, and free-space checks. |

### 7.1 Adding signatures

For configuration-only signatures, use HVA1 `MemorySignature`: a printable
identifier, pattern, byte mask, length, signed data offset, and access/alignment
flags. No engine modification is required. To add a new matching primitive,
extend `scanner.cpp`, preserve Boyer-Moore-Horspool skip correctness with masked
anchors, and add cross-page plus all-wildcard tests.

### 7.2 Adding decoders or decryption

Prefer the existing user-mode pure functions after authenticated extraction.
Add a new parameterized algorithm in `tools/overlay/src/data_transform.cpp`,
then expose it through `ParseTransformSpec` and add a fixed test vector. If a
universal fixed-format decoder truly belongs in root:

1. add a versioned `HV_EFI_ANALYSIS_FIELD_TYPE`;
2. define a fixed-size result representation;
3. validate byte count, alignment, address range, and numeric overflow;
4. read once through `HvEfiGuestPtRead`;
5. never accept function pointers, bytecode, variable code, or guest callbacks;
6. add unit tests and a companion source-contract test;
7. rerun the disposable VM analysis gate.

### 7.3 Adding rendering primitives

Extend `D3dRenderer` and keep coordinates client-relative. Add a source/runtime
test that initializes the D3D11/Direct2D path and presents at least one frame.
Do not add global hooks, injection, remote process access, or a new driver. More
complex scenes should be produced by the publisher adapter, leaving the overlay
as a bounded renderer.

### 7.4 Supporting another Windows build

For one additional build, generate a separate header/manifest from that build's
kernel and PDB and validate it in a separate build tree and VM snapshot. For
multiple profiles in one EFI binary, introduce an explicit registry keyed by
build/UBR/timestamp/image-size/PDB GUID/age, then select only after full live
identity validation. Never fall back to a “closest” build.

### 7.5 Extending overlay integration

The publisher is implemented beside the existing control client. Extensions
must preserve these properties:

- consume only authenticated HVR1 responses;
- own no raw physical-memory access;
- keep the HMAC descriptor out of the overlay process;
- create and ACL the HVO2 mapping;
- transform results into screen-space `EntityData`;
- publish with the sequence protocol;
- clear the mapping on session stop, generation change, target exit, or error;
- include tests using the existing overlay protocol reader.

The publisher is a host-side adapter. Do not put Win32 named-mapping calls in
VMX root or the WDM VMCALL path.

## 8. Validation and evidence

The normal local regression set is:

```powershell
ctest --test-dir build\pinned-shadow -C Debug --output-on-failure
ctest --test-dir build\hvr1-verify -C Release --output-on-failure
ctest --test-dir build\overlay --output-on-failure
ctest --test-dir .analysis\root-contract -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools\host-runner\tests\Test-HostRunnerContracts.ps1
git diff --check
```

At this handoff the configured suites contain 16 EFI/hypervisor tests, 4 HVR1
companion tests, 5 overlay tests, and 1 root contract test, plus the host-runner
contract script.

The direct runner explicitly builds `hvr1_overlay_adapter_tests` before CTest.
Credential readiness imports the DPAPI payload instead of checking only for a
file, and `StoreMailboxCredential` can safely refresh the existing provisioned
32-byte mailbox key when the runner is executed under the owning Windows user.

Relevant disposable-VM evidence recorded in the root README includes:

- the accepted 20-vCPU Windows `26200.9445` boot/health/detach run;
- four-vCPU diagnostic regressions;
- dynamic-analysis run
  `uefi-windows-probe-20260920-170822-67fa005610`, which found an `MZ` match at
  guest VA `0x00007FF7EEE80000`, published 20 HVO2 frames with a 0.451 ms
  maximum extraction-to-publication latency and no 50 ms violations, stopped
  analysis, exercised alternate EPT, and detached all four CPUs cleanly.

Only the disposable-VM gate proves privileged behavior. Pure-model and source
contracts are necessary but do not prove that VM entry, EPT switching, Windows
boot, or all-CPU detach works on a new profile.

## 9. Recommended first steps for the next agent

1. Read this document, the root `README.md`, and
   `components/hypervisor/src/driver/efi_runtime_companion/README.md`.
2. Run the local regression commands and confirm a clean Git worktree.
3. Use `docs/OVERLAY_PIPELINE.md` before changing the HVA1-to-HVO2 adapter,
   transforms, mapping ACL, or projection contract.
4. For root changes, update ABI layout assertions, C/ASM offsets, capability
   masks, source contracts, and pure-model tests together.
5. Run privileged changes only through the leased disposable-VM host runner,
   preserve evidence, restore the snapshot, and stop the VM afterward.
6. Keep at least 20 GiB free on the system drive; delete obsolete generated
   build/evidence files or move inactive VM artifacts to the data drive before
   starting a long gate.
