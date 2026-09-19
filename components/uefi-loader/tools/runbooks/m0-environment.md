# M0 Environment Runbook — Bring-Up, Snapshot Discipline, Rollback

Runbook for provisioning the OVMF + QEMU + swtpm environment and the VMware
guest, with snapshot discipline and rollback-on-unbootable.

## 1. Provisioning (one-time per host)

See `tools/PROVISIONING.md` for exact sources and versions.

```powershell
# 1. QEMU (winget)
.\tools\provision-qemu.ps1

# 2. OVMF firmware (pinned release)
.\tools\provision-ovmf.ps1

# 3. swtpm — no native Windows binary; use WSL2 (or MSYS2 build).
#    Detect + print exact start command:
.\tools\provision-swtpm.ps1
```

Open a new shell after step 1 so `qemu-system-x86_64` is on PATH.

## 2. Starting the TPM

From WSL2 (default distro), before launching the VM:

```bash
swtpm socket --tpm2 --tpmstate dir=/tmp/swtpm-state \
  --server type=tcp,port=2321 --ctrl type=tcp,port=2322 \
  --flags not-need-init,startup-clear --log level=5
```

## 3. Booting the guest

```powershell
# Snapshot mode: all disk writes are temporary (recommended for every boot test).
.\tools\launch-vm.ps1 -DiskPath C:\vms\win-guest.qcow2 -Snapshot
```

## 4. Snapshot discipline

- ALWAYS boot from a snapshot (use `-Snapshot`, or a hypervisor snapshot).
- The pristine OVMF `vars.fd` is never mutated — `launch-vm.ps1` copies it per
  launch.
- Take a fresh VMware snapshot before any firmware reconfiguration (TPM version
  toggle, bank disable).
- One snapshot per test stage; do not reuse a dirty snapshot across experiments.

## 5. Rollback on unbootable state

| Failure | Recovery |
|---|---|
| Guest will not boot after firmware change | Revert to the pre-change snapshot; re-enter firmware and restore TPM defaults. |
| OVMF variables corrupted | Delete the per-launch `tools/ovmf/vars-*.fd` working copy; pristine `vars.fd` is untouched. |
| swtpm state corrupted | Stop swtpm, delete `/tmp/swtpm-state`, restart (TPM is re-initialized). |
| Host toolchain broken | Re-run `provision-qemu.ps1` / `provision-ovmf.ps1` (idempotent). |

No persistent firmware/NV mutation happens outside a snapshot-protected VM.

## 6. Next

Once both environments boot and the M0 probe (see `tools/probe/README.md`)
records `verdict: MET` for empty PCR[0–7] + `Spec ID Event03`, the M0 hard gate
passes and M2 replay work may begin. M1 (shared `tcglog` library + `ib-tcg-dump`)
may proceed in parallel with M0 verification.
