# Independent native device reader

This read-only Windows utility queries the four selected network adapter
interfaces with `IOCTL_NDIS_QUERY_GLOBAL_STATS` and
`OID_802_3_CURRENT_ADDRESS`. It opens each NDIS LAN device-interface path through
SetupAPI, matches both the pinned PnP instance ID and its `NetCfgInstanceId`,
then issues the OID directly with `DeviceIoControl`. It does not use
`GetIfTable2`, `GetAdaptersAddresses`, WMI values, or a wrapper result as the
returned MAC. Identity enumeration is used only to select the device handle.

The disk read opens `\\.\PHYSICALDRIVE0` and issues
`IOCTL_STORAGE_QUERY_PROPERTY` for `StorageDeviceProperty`, then reads the
serial at `STORAGE_DEVICE_DESCRIPTOR.SerialNumberOffset`. This path is separate
from the earlier cached-interface inventory output.

The selected PnP IDs, interface GUIDs, device path, originals, and replacements
are copied from the repository's recorded native/WMI capture and
`reports/client-originals.tsv`. The replacement fields are included as reference
metadata only; this executable has no write or substitution path.

## Build and run

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\device_native_io\build.ps1
```

The script writes the static-runtime executable under
`spoof-handoff/.build/device-native-io-20261016/`, runs the pure-format tests,
and attempts all five native reads. Each target emits a JSON record even when
selection, handle opening, or the device query fails. The exit code is nonzero
if any selected query fails; it never turns a WMI or cached address into a
successful native result.

To query again without rebuilding, select a new output filename:

```powershell
& .\spoof-handoff\.build\device-native-io-20261016\native_device_io.exe --capture .\spoof-handoff\reports\device-runtime-package-20261016\native\manual-native-capture.json
```

The output path must not already exist. The utility requires no elevation and
does not install, load, bind, or remove drivers or change machine settings.
If Windows does not expose an adapter interface or the OID request fails, the
JSON contains the exact stage, Win32 error code, and `FormatMessageW` text.

Microsoft API references:

- [IOCTL_NDIS_QUERY_GLOBAL_STATS](https://learn.microsoft.com/en-us/previous-versions/windows/hardware/network/ff548975%28v%3Dvs.85%29)
- [STORAGE_DEVICE_DESCRIPTOR](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-storage_device_descriptor)
- [DeviceIoControl](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-deviceiocontrol)

An independent direct OID read demonstrates what this reader receives through
that adapter path. It does not establish that every arbitrary application uses
the same API or that a Windows driver is correctly installed or detached.
