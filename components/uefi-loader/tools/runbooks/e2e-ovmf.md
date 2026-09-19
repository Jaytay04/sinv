# Full OVMF + swtpm Replay E2E

This is the acceptance procedure for OpenSpec task 5.4. It is intentionally
separate from the component gates: success requires Windows to reach its
desktop after `loader.efi` replays a log into fresh PCR banks.

## Required snapshot contents

The WSL2 Ubuntu distribution or configured Linux VMware guest must contain:

- KVM access;
- the pinned patched OVMF build documented in
  `nested-linux-positive-path.md`;
- QEMU, swtpm, mtools, and tpm2-tools;
- a bootable, disposable Windows UEFI disk image;
- a writable FAT evidence disk shared between the Linux host and nested
  Windows guest;
- a Windows startup task that runs only the pinned static recorder/auditor
  package and writes its exit codes and evidence to that FAT disk.

The host runner exports the prepared Windows VMDK to
`%LOCALAPPDATA%\UEFILoader\PreparedDisks` and uses a per-run qcow2 overlay so
the prepared base remains immutable.

## Gate sequence

1. Acquire the exact WSL or Linux-VM shared lease and verify no conflicting
   QEMU/VMware process exists.
2. Build the current static Windows tools and `loader.efi`; record SHA-256 for
   every staged artifact.
3. Boot a fresh overlay once under QEMU to condition Windows for its virtual
   NVMe/TPM hardware. The startup task writes `condition-result.json`, remains
   installed, and shuts Windows down cleanly.

   ```text
   ib-tcg-dump capture --out tcglog.ib --full tcglog-full.bin
   ```

4. Discard the conditioning TPM state. Provision a SHA-256-only swtpm state,
   prove PCR0–7 are zero, then discard and recreate that state for the
   authoritative run.
5. Run the signing and staging preflight. Stage shim, signed `loader.efi`, the
   captured log, and the Windows boot manager/payload. Verify the staging
   manifest by reading every file back.
6. Attach the conditioned overlay, fresh OVMF variables, evidence disk, and
   fresh swtpm state to patched OVMF.
7. Boot the same Windows disk with the pinned measurement-disabled OVMF and the
   one-shot chain. Required loader evidence, in order:

   - TCG2 protocol located and all active PCR0–7 at reset;
   - strict `tcglog.ib` accepted;
   - reproducible events rehashed and raw-only events extended;
   - replayed PCR0–7 equal the stored expected states;
   - `divergence.ib` persisted with exactly the raw-extended event indexes;
   - boot-manager backup restored to its recorded SHA-256;
   - payload and restored Windows boot manager started in order.

8. Wait for the Windows desktop/startup task. Capture a new full log and run:

   ```text
   ibaudit dump --log tcglog.ib
   ibaudit verify-pcrs --log tcglog.ib --divergence divergence.ib
   ibaudit tcg-version
   ibaudit bootmgr-integrity --expected-hash <recorded-bootmgfw-sha256>
   ibaudit audit-vars --expected-mok expected-mok.txt
   ibaudit baseline --log tcglog.ib --output post-replay-baseline.json
   ```

9. Compare the post-boot PCR0–7 bytes with the expected states captured in
   step 4. Compare the manifest record count and indexes with the raw-event
   classification generated from the reference log. Both comparisons must be
   exact; presence of a manifest alone is not evidence of correct replay.
10. Shut down nested Windows, collect all serial/debugcon/Windows evidence,
    terminate only the exact run-owned process set on failure, verify it is
    absent, and release the lease.

## Pass/fail contract

Pass only when Windows reaches the desktop, all active-bank PCR0–7 values match
the expected replay state byte-for-byte, the divergence manifest names exactly
the raw-extended events, `ibaudit` reports the attributed divergence without a
structural-log failure, and no boot-manager transaction residue remains.

On any timeout or uncertain state, hard-stop only the exact configured Linux
VM, restore its disposable snapshot, verify it is off, and retain/quarantine
the lease if deterministic recovery cannot be proven.

## Current status

PASS on 2026-09-07: `uefi-linux-nested-windows-e2e-20260907-224932-87dcc274c8`.
The run conditioned a disposable Windows overlay, replayed a genuine 25-event
reference log from zero SHA-256 PCR0–7, reached Windows, captured a wrapped
Event03 log, and attributed all 11 raw-only extensions. See
`tools/probe/evidence/p11-e2e-ovmf-20260907.txt`.
