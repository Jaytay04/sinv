# Simultaneous identifier proof and restoration

**FINAL_VERIFY_PASS on DESKTOP-5852FUT.** All nine confirmed categories were
active together: seven SMBIOS fields, the Disk 0 serial, and four adapter MACs,
including VMnet1 and VMnet8. This is 12 bindings. The original, active, and
restored phases each passed two polls with 90 matching reader observations.

The reader matrix was native RSMB and fresh WMI for SMBIOS; storage IOCTL and
fresh WMI for Disk 0; and NDIS current-address OID, GetAdaptersAddresses,
GetAdaptersInfo, GetIfTable2, Win32_NetworkAdapter, MSFT_NetAdapter.NetworkAddresses,
and Get-NetAdapter for each NIC. The Intel Wi-Fi adapter additionally passed
the DOT11 current-address OID. A separate file I/O integrity probe passed.
Only these recorded readers and devices are proven. USB disks are excluded;
CPU/GPU serial members remain disabled.

Final restoration verified original values, removed storage packages and filter,
absent NIC address overrides and project NIC filter service, removed owned
certificate trust, test-signing OFF, and Secure Boot ON with variable byte 01.
BootNext is absent; BootOrder and all 21 recorded firmware variables match their
original snapshots. The four supported BCD views match exactly. BCD export
serialization is not claimed byte-identical. The immutable ESP files and paths
match; BOOTSTAT.DAT and BCD logs retain the documented mutable exceptions.

The authenticated R16 VM gate preceded the physical run. It recorded 22 actions
and 352 observations across its four phases; the VM was snapshot-restored and
powered off. Its fixture repair changed only the SMBIOS2 advertised maximum
structure size from 111 to 112. The production guard was retained.

Finalization required a short BCD export destination and corrections to storage
imports, PnP ID casing, and null handling for absent firmware variables. The
supplementary finalizer preserves the frozen runner and original evidence,
reuses the completed storage proof, checks fresh restoration state, and records
the final proof before updating the combined journal to Complete.

Twelve selected regression suites passed **555 checks** at closeout. The
prepared stock build had also passed 28 loader and two inventory tests.
Regression tests involving this host's proof require the retained machine-local
evidence and documented worktree layout; managed firmware fixtures are explicitly
marked as test data and do not claim live firmware reads.

Files in this review:

- [combined-host-proof.json](combined-host-proof.json): exact per-reader values
  from all three phases, restoration results, source hashes, and scope.
- [evidence-index.json](evidence-index.json): paths and hashes for 19 primary
  machine-local proof, VM, snapshot, test, and external recovery files.
- [source-files.sha256.json](source-files.sha256.json): changed source, command,
  and documentation files with their working-tree byte hashes.

The primary final proof SHA-256 is
`9906451490146A155C16F270167A11ED16673ED606D8AD2D7F15183C23A291CD`.
The Complete journal SHA-256 is
`23B5F5F38D5A87B0BC2381F42ADF7AFD77001AD8A55E13A18D44679FB6B45390`.

Raw recovery state, generated packages, signing keys, and VM disks remain local.
Small SMBIOS entrypoint/table fixtures under `tools/combined_vm/smbios_fixture/`
are captured disposable-VM parser test inputs with provenance; they are not disk
images or deployable firmware. The main checkout, its index, the pinned production
loader/collector, and `src/hardened/` were preserved.

No further activation or reboot is required to close this run. The machine is
restored; this proof does not claim the replacements remain active.
