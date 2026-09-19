# Combined repository guidance

This repository is a static-first composition of two independently maintained
components:

- `components/uefi-loader` builds the UEFI loader and its host-side tests.
- `components/hypervisor` builds the EFI hypervisor MVP and its pure-model tests.

The root `CMakeLists.txt` is an ExternalProject superbuild. Each component is
configured in its own binary directory under `build/components`; do not turn
the components into one `add_subdirectory()` graph. This keeps compiler,
runtime, CTest, and optional WDK state isolated.

## Combined contract

The root build passes `components/uefi-loader/src/vendor` to the Hypervisor EFI
configuration as `HYPERSINV_UEFI_SUPPORT_DIR` (with the component source path as
`HYPERSINV_UEFI_SOURCE_DIR`). The hypervisor EFI target is named
`uefi_hypervisor`. `combined_hypervisor_efi` is the root target that requests it.

The combined defaults build static CRT artifacts, disable
`HYPERSINV_BUILD_DRIVER`, and disable the historical user-mode SCM loader.
There is no driver live-load step in this repository or its build scripts.

The component-owned EFI enable option is `HYPERSINV_BUILD_UEFI`; the root
target `combined_hypervisor_efi` builds `uefi_hypervisor` explicitly. Keep the
toggle in the component's own CMake contract; the root should not invent
duplicate option names.

## Safety boundary

All VMX execution and end-to-end boot tests are VM-only activities. Use a
disposable VM with nested virtualization enabled, snapshots, and an offline
or otherwise controlled test network. Never run an experimental EFI payload or
driver on a primary host. Do not add signing private keys, credentials, VM
disks, or generated build state to source control.

## Change discipline

Keep public ABI names and the EFI handoff contract stable. Add tests in the
component that owns the behavior, and keep root tests source-only unless they
validate orchestration. Use conventional commits without AI attribution when
commits are requested.
