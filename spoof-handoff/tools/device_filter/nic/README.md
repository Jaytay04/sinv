# Current MAC query filter

This directory contains an NDIS 6 lightweight filter source module and a
portable policy module. The filter binds only the four exact PnP instance IDs
recorded in `spoof-handoff/reports/client-originals.tsv` and considers only
successful `OID_802_3_CURRENT_ADDRESS` and `OID_DOT11_CURRENT_ADDRESS` query
responses. It clones and forwards regular OID requests, then rewrites at most
the first six returned bytes when the value exactly matches that adapter's
captured original. A mismatch, unknown adapter, failed query, short buffer, or
unrecognized request is passed through unchanged.

The four policy mappings are:

| PnP instance ID | Captured current address | Replacement |
| --- | --- | --- |
| `ROOT\VMWARE\0001` | `00:50:56:C0:00:08` | `02:90:10:20:30:40` |
| `ROOT\VMWARE\0000` | `00:50:56:C0:00:01` | `02:90:10:20:30:41` |
| `PCI\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\3&11583659&1&A3` | `C4:BD:E5:32:06:D1` | `02:90:10:20:30:42` |
| `PCI\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\4&3FA6681&0&00E2` | `D8:BB:C1:D8:57:20` | `02:90:10:20:30:43` |

The policy table validates both original and replacement values as unicast
addresses. The response handler never processes `OID_802_3_PERMANENT_ADDRESS`
or `OID_DOT11_PERMANENT_ADDRESS`, changes a packet, sends a configuration OID,
or modifies adapter settings. It uses the base miniport PDO's documented
`DevicePropertyInstanceId`; an absent PDO or non-ASCII/unmatched instance ID
leaves that module in pass-through mode.

## Enable gate

`NIC_FILTER_SPOOF_ENABLED` defaults to `0`. The driver therefore loads in
pass-through mode unless its build explicitly defines
`NIC_FILTER_SPOOF_ENABLED=1`. This is a compile-time gate; the source does not
add a runtime control device or use adapter registry settings. When enabled,
the exact-original guard applies. When disabled, the pure transform restores
an exact replacement value to the captured original and otherwise leaves the
response alone. A normal lower-driver response already contains the original.

## Coverage boundary

This filter changes only regular NDIS OID query responses that pass through its
LWF instance. It does not prove that `GetIfTable2`, `GetAdaptersAddresses`, the
network interface MIB provider, WMI, or an application reading a cached
interface row sees the replacement. A separate OS cache/provider mechanism
would need its own documented implementation and native read-back proof. This
module makes no such coverage claim.

It registers no direct-OID or synchronous-OID callbacks, so those paths are
outside the implementation. It also has no install INF or service installer;
the parent integration must provide a verified NDIS LWF package and build it
with a WDK that exposes the NDIS 6.20 attach PDO fields. Signing, installation,
loading, and live adapter tests are not performed by this source-only change.

## Host policy test

The policy unit test has no WDK, device, or elevation dependency. From this
directory:

```text
clang -std=c11 -Wall -Wextra -Werror nic_policy.c host_test.c -o host_test.exe
./host_test.exe
```

It checks all four original-to-replacement-to-original round trips, exact TSV
values, trailing-byte preservation, valid unicast formatting, and rejection
of an unknown adapter, invalid mode, short buffer, and unexpected native value.

The filter follows Microsoft's documented regular OID clone/forward/complete
pattern and cancellation forwarding APIs:

- <https://learn.microsoft.com/en-us/windows-hardware/drivers/network/filtering-oid-requests-in-an-ndis-filter-driver>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nf-ndis-ndisallocatecloneoidrequest>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nf-ndis-ndiscanceloidrequest>
- <https://learn.microsoft.com/en-us/windows-hardware/drivers/network/pausing-a-filter-module>
