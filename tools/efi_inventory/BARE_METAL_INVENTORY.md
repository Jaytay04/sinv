# Bare-metal EFI variable inventory (read-only)

This procedure inventories EFI variables on the **physical machine**, not a
VM. It uses a trusted Linux live USB and the kernel's `efivarfs` interface.
Do **not** launch this project's experimental EFI collector or driver on a
primary host; the repository's safety boundary prohibits that.

## Before rebooting

1. If Windows device encryption or BitLocker is enabled, confirm that you can
   retrieve the recovery key. A changed boot path can prompt for it.
2. Prepare a trusted, UEFI-bootable Linux live USB and a second USB drive for
   the results. Use the firmware's **one-time boot menu** rather than changing
   the permanent boot order. Do not disable Secure Boot or clear the TPM.
3. Boot the live environment in UEFI mode and choose its live/“Try” session,
   **not** Install. Do not mount or modify the Windows drive.

Booting a different OS is not a guarantee that firmware or its bootloader
makes no state changes. The commands below themselves perform no EFI-variable
writes. [Microsoft explains why boot changes may trigger BitLocker recovery.](https://learn.microsoft.com/en-us/windows/security/operating-system-security/data-protection/bitlocker/faq)

## Mount EFI variables read-only

Open a terminal in the live session:

```bash
test -d /sys/firmware/efi/efivars || { echo "Not booted in UEFI mode"; exit 1; }

if [ "$(findmnt -rn -o FSTYPE --mountpoint /sys/firmware/efi/efivars)" = efivarfs ]; then
    sudo mount -o remount,ro /sys/firmware/efi/efivars
else
    sudo mount -t efivarfs -o ro efivarfs /sys/firmware/efi/efivars
fi

findmnt -rn -o OPTIONS --mountpoint /sys/firmware/efi/efivars |
    tr ',' '\n' | grep -qx ro || { echo "Not read-only; stop here"; exit 1; }
```

If any command fails, stop rather than proceeding with an unverified mount.
The [Linux kernel documentation](https://docs.kernel.org/6.0/filesystems/efivarfs.html)
describes `efivarfs` and its variable-file format.

## Collect metadata

This lists each available variable's name, vendor GUID, and file size without
exporting the value:

```bash
sudo find /sys/firmware/efi/efivars -maxdepth 1 -type f \
    -printf '%f\t%s\n' | LC_ALL=C sort > "$HOME/efi-variables.tsv"
wc -l "$HOME/efi-variables.tsv"
```

Copy `efi-variables.tsv` to the second USB drive using the live desktop's file
manager. The size includes the four-byte EFI attributes prefix; it is not the
payload size alone. Treat names and GUIDs as potentially sensitive.

## Optional: copy the variable bytes

Only if the metadata is insufficient, create a local archive while `efivarfs`
is still verified read-only:

```bash
sudo tar -C /sys/firmware/efi/efivars -cf - . > "$HOME/efi-variable-bytes.tar"
echo "tar exit status: $?"
```

Check the exit status and any errors before regarding the archive as complete.
Copy it to the second USB drive and protect it: variable contents may contain
device-specific or sensitive data. Each file starts with four little-endian
attribute bytes, followed by its variable data. Do not extract the archive
back into `efivarfs`, delete files there, or use `efibootmgr`/`SetVariable`.

## Scope of the result

This is an inventory of variables accessible **after the OS has booted**.
Boot-service-only variables may not appear: the UEFI specification says that
variables without `EFI_VARIABLE_RUNTIME_ACCESS` are no longer visible after
`ExitBootServices`. A complete preboot inventory would require a firmware-stage
collector, which this project does not authorize on a primary host.
[UEFI variable services specification](https://uefi.org/specs/UEFI/2.10/08_Services_Runtime_Services.html)

The Windows tool's `scan-windows` command can separately read this host's
SMBIOS, but it does not enumerate EFI variables. Do not combine a report from
one machine with SMBIOS collected on another.
