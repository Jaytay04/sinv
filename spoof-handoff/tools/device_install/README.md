# Device filter package construction

This folder contains packaging metadata for the two existing signed native
filters. `client_storage_filter.inf` targets only the captured Intel NVMe disk
hardware ID and adds the existing `client_storage_filter` service as a
declarative upper filter. It does not replace the inbox disk driver.
`client_nic_filter.inf` follows the NDIS lightweight-filter `NetService`
package model. Its component ID and service name are both
`HypersinvNicAddressFilter`; its `NetCfgInstanceId` matches the GUID in
`nic_filter.c`.

Build output is copied to
`spoof-handoff/artifacts/device-runtime-package-20261016/{storage,nic}` and
command evidence is written to
`spoof-handoff/reports/device-runtime-package-20261016/packaging`. Run from
the repository root in an account that can use the already-existing signing
certificate and private key:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\device_install\build-packages.ps1
```

The script copies, but does not modify, the pinned `.sys` files from
`spoof-handoff/artifacts/device-filters-20261014/signed-final/enabled`. It
generates each catalog with Inf2Cat for Windows 11 24H2+ x64, signs with the
existing certificate in `CurrentUser\My`, and records InfVerif, Inf2Cat,
catalog signature, and catalog-member verification output separately. It
does not import certificates, install packages, bind filters, or change
services, network settings, BCD, firmware, ESP files, or reboot state.

## Windows installation and removal commands

The storage extension package can be installed with:

```powershell
pnputil /add-driver .\spoof-handoff\artifacts\device-runtime-package-20261016\storage\client_storage_filter.inf /install
```

After installation, use the published `oem#.inf` name reported by PnPUtil to
remove the package and uninstall it from matching devices:

```powershell
$storagePackages = @(Get-WindowsDriver -Online | Where-Object { (Split-Path $_.OriginalFileName -Leaf) -ieq 'client_storage_filter.inf' -and $_.ProviderName -eq 'Hypersinv' })
if ($storagePackages.Count -ne 1) { throw 'Expected exactly one Hypersinv storage package; review the installation snapshot before removal.' }
pnputil /delete-driver $storagePackages[0].Driver /uninstall
```

PnP can require a device restart before an extension filter is attached or
removed from a running disk stack. Do not treat successful Driver Store
staging as proof of attachment or detachment.

The NDIS filter package commands are:

```powershell
netcfg /l .\spoof-handoff\artifacts\device-runtime-package-20261016\nic\client_nic_filter.inf /c s /i HypersinvNicAddressFilter
netcfg /u HypersinvNicAddressFilter
```

`netcfg` installs or uninstalls the network service component and its binding
state. Network adapters may be briefly rebound during either operation. These
commands are documentation only; this build script does not execute them.

## Evidence limits

InfVerif and Inf2Cat validate package syntax and catalog applicability for the
selected OS target. Catalog signing and catalog-member verification establish
the recorded cryptographic package checks under the current trust context.
They do not prove that Windows will admit or load the drivers, that PnP/NDIS
will bind them to the intended devices, or that runtime native readers return
any particular identifier. An elevated PnPUtil staging check changes the live
Driver Store and is deliberately outside this packaging step.

Microsoft references:

- [Device-specific filter installation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/installing-a-filter-driver)
- [Declarative AddFilter directive](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addfilter-directive)
- [Extension INF targeting and service rules](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/using-an-extension-inf-file)
- [NDIS network filter package requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/installation-requirements-for-network-filter-drivers)
- [Modifying NDIS LWF INF requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/configuring-an-inf-file-for-a-modifying-filter-driver)
- [Inf2Cat](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/inf2cat)
