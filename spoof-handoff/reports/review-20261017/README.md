# Storage recovery and test preparation

The storage extension's demand-start configuration reproduced
`INACCESSIBLE_BOOT_DEVICE` (`0x7B`) in an isolated Windows 11 25H2 VM. Its
transparent boot-start variant, with identical executable code, booted and
appeared in the actual disk stack. The replacement boot-start package then
passed native storage IOCTL and fresh WMI original/replacement/restored checks,
file I/O checks, and supported package removal followed by reboot.

This establishes a start-policy failure in that VM. It does not establish the
exact cause of the restored physical machine's failure: no surviving host crash
dump was available. The VM used build 26200.9445 and virtual NVMe; the physical
host uses build 26200.9711 and an Intel NVMe device. Physical controller
compatibility and live NIC behavior remain unproven.

The change sets the storage INF and package contract to boot-start, retains the
old activation quarantine, and adds a separate user-run storage-only workflow.
That workflow verifies the transparent package and its removal before allowing
replacement. It journals mutation intent, requires a recovery copy on a separate
physical disk before installation, and restores only policy/trust entries owned
by its run. It does not automate firmware changes or reboot the PC.

Start with [the runbook](../../docs/STORAGE_BAREMETAL_RUNBOOK.md) and keep
[the boot recovery guide](../../docs/STORAGE_BOOT_RECOVERY.md) available outside
the Windows disk. The offline removal script has model/source coverage; it has
not been executed in physical WinRE. A snapshot and BCD export are not a system
image. Recovery after an image restore cannot resume the runner if that restore
removed its exact run state or signed release artifacts.

The publication also consolidates the recovered EFI loader, inventory, SMBIOS,
and identifier tools that previously existed only as uncommitted source. It
excludes the unrelated local documentation edit, generated packages, private
keys, VM disks, raw recovery snapshots, and machine-local build directories.

Evidence in this directory:

- `prepare-parser-fix.json`: subsequent correction for blank BCDEdit lines at the
  production parser boundary, 66 passing workflow assertions, and explicit
  prerequisites by stage. The original validation and source manifest below
  retain their scope at commit `e5c184d`.
- `validation.json`: scoped VM, pure-test, read-only host, and signature verdicts.
- `vm-bugcheck-excerpt.txt`: the recorded VM bug-check and parameters.
- `vm-binary-comparison.json`: the transparent code comparison.
- `storage-host-tests.txt`: the final pure workflow suite result.
- `changed-files.json`: source paths, sizes, and SHA-256 values; excludes itself
  and `diff-stat.txt` to avoid a recursive manifest.
- `diff-stat.txt`: staged change summary at publication time.

Full local evidence remains under `spoof-handoff/reports/device-vm-20261017/`
and `spoof-handoff/reports/storage-host-prep-20261017/`. The former's
`SHA256SUMS.txt` seals the historical VM evidence with SHA-256
`2518066D932F4DE492D7DC5632B5B4CE52AF6EE20B889399109C5093D17D3AC2`.
Fresh signed physical-target packages are local-only under
`spoof-handoff/artifacts/storage-host-20261017/`; the build command regenerates
them using the existing user-held signing key. No physical-host driver
installation, load, policy/trust change, firmware write, or reboot was performed
while preparing this workflow.
