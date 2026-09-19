# Runtime V22 - Fail-Closed VMX Substrate Admission

Status: complete. Gate 60 accepted on 2026-09-05.

## Problem

The persistent LOAD path validates VMX CPUID support, FEATURE_CONTROL, VMX
capabilities, CET state, and every target processor before entry. It does not
classify the software substrate that currently presents those capabilities.
When the Windows Hyper-V/VBS hypervisor owns VT-x, LOAD can therefore advance
to VMX entry before discovering the ownership conflict.

## Decision

Add one shared, WDK-free substrate policy that admits only native execution
with no hypervisor present or the project's accepted VMware nested-VMX
environment. Microsoft Hyper-V and every unrecognized or malformed hypervisor
identity fail closed before runtime configuration or any VMX mutation.

The loader performs the same policy check before SCM mutation for clear
operator feedback. The kernel independently repeats it at the authenticated
LOAD boundary so a direct IOCTL caller cannot bypass the guard. CPUID vendor
identity is compatibility evidence, not an authentication or security claim.
No public IOCTL, protocol, capability manifest, or existing wire layout changes.

## Checkpoints

1. **V22A - Pure substrate classification**
   - exact native, VMware, Microsoft Hyper-V, unsupported, and malformed
     classifications;
   - admit only native and VMware nested VMX;
   - reject inconsistent hypervisor-present inputs and reserved state.
2. **V22B - Independent loader and kernel enforcement**
   - loader checks before opening SCM or creating the service;
   - kernel checks after detached-owner proof and before runtime configuration;
   - both consume the same pure policy and report an exact classification.
3. **V22C - Static mutation authorization**
   - prove Hyper-V and unknown identities cannot become admitted;
   - prove neither loader nor kernel check can move after its mutation boundary;
   - preserve all public IOCTL values, capability bits, and response layouts.
4. **V22D - Gate 60 qualification**
   - prove the accepted VMware nested substrate remains admitted;
   - preserve exact manifest verification, persistent/EPT readiness, canonical
     unload, service absence, clean events, and no dump;
   - require the kernel admission marker in the closed KD log.

## Acceptance criteria

1. Native execution is admitted only when the hypervisor-present bit is clear
   and all hypervisor-leaf fields supplied to the policy are zero.
2. VMware nested execution is admitted only for the exact `VMwareVMware`
   twelve-byte vendor identity and a valid hypervisor-leaf range.
3. Exact `Microsoft Hv`, every unknown vendor, malformed presence values,
   short leaf ranges, and nonzero reserved state are rejected.
4. The loader rejects an unsupported substrate before SCM mutation.
5. The kernel independently rejects an unsupported substrate before runtime
   configuration, allocation, CR mutation, VMXON, or lifecycle publication.
6. No existing IOCTL, protocol, capability manifest, or wire layout changes.
7. A fresh WDK/MSVC/MASM build and full CTest pass before live execution.

## Live-test boundary

Gate 60 is one deterministic disposable-VM lifecycle. It is necessary because
the new guard sits directly before LOAD on both sides of the device boundary
and must not falsely reject the accepted VMware nested-VMX environment. Pure
tests provide the negative Microsoft Hyper-V and unknown-vendor coverage; the
clean VM is not reconfigured into those unsupported environments. Repetition
is not required. A true live failure is preserved and is not automatically
retried.

## Accepted evidence

Gate 60 passed for source commit
`722073bd3d06c99c646f37a41e8da90b690b100e`. The first execution exposed an
evidence defect: the kernel admission message used filtered `KdPrint` output
and was absent from the closed KD log. The message was promoted to the
error-level IHV driver channel, statically reverified, and the corrected
immutable package was run once.

- Fresh WDK/MSVC/MASM build: `449/449` targets.
- Full CTest: `277/277` tests.
- Package SHA-256:
  `D49020C6EF09907B23DC904F547979883EF11AB912371FC1C947F04F19CC0BD1`.
- Evidence archive SHA-256:
  `173F014CA706CEC124F4644FE4EEEECDAE754E87CFE4B20ABDC929DA40117887`.
- Closed KD log SHA-256:
  `A0FB3FB6A7AEAE5561D3433F2D77D9399BDE68C4CFB057172613A7009AA1BEBA`.
- Loader admission: exact `VMWARE_NESTED` classification before SCM mutation.
- Kernel admission: exact class `2` marker before runtime configuration.
- Runtime: persistent `LOADED`, EPT `READY`, combined `READY`.
- Teardown: canonical unload, service absent, empty stderr, no failure event,
  no new dump, and no KD fatal signature.
- Host runner: VM powered off and
  `Gate6-HostRunner-ReducedMemory-Clean` restored after PASS.

Runtime V22 is closed. Gate 60 must not be repeated.
