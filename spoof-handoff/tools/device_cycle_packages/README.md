# Restart-prepared device package variant

This folder builds a separate storage INF variant for the captured disk 0
device and rebuilds the paired NDIS package from the already signed binaries.
It does not modify the prior extension INF or either driver image.

The storage service uses `StartType=0` (`SERVICE_BOOT_START`). The isolated
Windows 11 26200.9445 VM reproduced `INACCESSIBLE_BOOT_DEVICE` (`0x7B`) with
the transparent demand-start package. The boot-start comparison, containing
identical executable code, booted with the filter running in the disk stack
and unchanged native and WMI serial values. A registered boot-disk filter can
be required for the disk stack to start even when its identifier query feature
is optional. The prior demand-start rationale did not account for that dependency.

Evidence is under `spoof-handoff/reports/device-vm-20261017/`, including the
crash log, binary code comparison, and authenticated guest read-back. These
VM results do not establish compatibility with the physical Intel disk stack.
Physical-host activation remains disabled. Substitution, removal, and restoration
need their own read-back evidence; a successful boot alone is insufficient.

The storage INF keeps the captured model hardware ID and uses the declarative
`AddFilter` upper-filter directive. It does not set an `UpperFilters`
registry value and does not replace the inbox disk driver. The INF omits
`LoadOrderGroup`; the live service-group order list lacks a `PnP Filter`
group, and declarative filter ordering controls its device-stack position.

Run the build from the repository root with the existing signing certificate
in `CurrentUser\My`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\device_cycle_packages\build-packages.ps1
```

Outputs are limited to `spoof-handoff/artifacts/device-reboot-cycle-20261016/packages`
and `spoof-handoff/reports/device-reboot-cycle-20261016/packaging`. The build
runs InfVerif, Inf2Cat, catalog signing, and Authenticode verification. It
does not import trust, stage or install packages, bind filters, alter services,
change BCD/firmware/ESP, or reboot.

Microsoft references:

- [Specifying driver load order](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/specifying-driver-load-order)
- [INF AddFilter directive](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addfilter-directive)
- [Device filter driver ordering](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/device-filter-driver-ordering)
- [Device installations and system restarts](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/device-installations-and-system-restarts)
- [Adding a PnP device to a running system](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/adding-a-pnp-device-to-a-running-system)
