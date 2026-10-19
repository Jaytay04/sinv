# EFI variable + SMBIOS inventory — handoff for the identifier-spoofing work

Status of the shared hardware-identifier inventory step, written for the next
agent. Everything here is source; no VM or firmware state is assumed.

## What exists now

1.  `tools/efi_inventory/` — read-only identifier inventory (adapted into
    this tree by the earlier handoff):

    -   `efi/collector.c` — freestanding UEFI application (`EfiMain`).
        Boot-services stage only; no runtime code, no `gST`/`gBS` globals,
        no CRT memory calls. Enumerates every visible EFI variable
        (`GetVariable`/`GetNextVariableName`) and writes a report to the
        **root of the volume the image was loaded from**, refusing to
        overwrite an existing report file.
    -   `src/`, `include/`, `tests/` — host (Windows) side: report parser,
        SMBIOS reader (`RSMB` via `GetSystemFirmwareTable`), encrypted
        variable snapshot, authenticated read-only local pipe.

2.  The project UEFI loader (`components/uefi-loader`) now carries the
    collector as its optional second payload:

    -   `src/loader/efi_entry.c`: after the main payload launch and right
        before the Windows boot-manager launch, if
        `\EFI\insecure-boot\inventory.efi` exists on the ESP, the loader
        loads and runs it via `image_launch_start` (bounded, unloaded
        afterwards). A collector failure is console-only and never changes
        the handoff verdict.
    -   Console lines to look for at boot:
        `inventory collector: PASS` / `inventory collector: FAIL (non-fatal; ...`.
    -   The tool target `efi_inventory_collector` (build output
        `efi/inventory.efi`) links the repo's own vendored
        `src/vendor/Uefi.h` + `src/vendor/efi_crt.c`, same link contract as
        `loader.efi` (`/NODEFAULTLIB`, `EFI_APPLICATION`, entry `EfiMain`).
      The file name is `inventory.efi`; only the internal target symbol
      keeps the old name so nothing else had to move.

3.  `tools/hwid_spoofer/` — pre-existing stub (`HWID_SPOOF_CONFIG`:
    disk serial, MAC, SMBIOS serial/manufacturer/product, GPU serial,
    motherboard serial/manufacturer/product, CPU serial; `Apply`/`Remove`
    are placeholders). The reports below feed those fields one-to-one; the
    spoofing itself is not yet implemented in this tree.

## Where the identifiers come from

Two complementary captures, same machine:

-   Pre-boot EFI variables (names, vendor GUIDs, attributes, sizes, read
    statuses): ESP file `\EFI\insecure-boot\HypersinvEfiInventory.txt` after
    one boot with `inventory.efi` staged. Format: `HVEFI001` header, one
    `VAR` line per variable, `END` footer with the record count.
-   SMBIOS: `efi_inventory.exe scan-windows <out.txt>` (BIOS vendor; system
    manufacturer/product/UUID/serial/SKU/family; board manufacturer/
    product/serial; chassis manufacturer/serial; CPU vendor/model/serial;
    memory manufacturer/serial/part number) — read from Windows, no
    elevation needed.

`scan <raw.efi.report> <out.txt>` merges both; `scan-efi <raw> <out.txt>`
parses only the ESP report (for captures made on another machine).

## How to arm one boot

Build the standalone tool (x64 Native Tools prompt):

```powershell
$b = '.codex-build\efi_inventory'
cmake -S tools/efi_inventory -B $b -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Release
cmake --build $b --target efi_inventory_collector
```

Then place `$b\efi\inventory.efi` next to `loader.efi` in the existing
`EFI\insecure-boot\` ESP staging directory (e.g. `ib-install.exe stage ...`
with an added file, or a manual `mountvol X: /S` copy for a single armed
boot). Next boot the console shows `inventory collector: PASS`, the report
file appears at the ESP staging root, and the normal Windows chain runs
unchanged. Remove `inventory.efi` afterwards for normal boots — absence
is skipped silently, so it never breaks the chain.

Existing docs still valid: `tools/efi_inventory/README.md` (host tooling +
report format + VM procedure), `tools/efi_inventory/BARE_METAL_COLLECTOR_TEST.md`
(USB one-time-boot path, unchanged by the loader hook).

## Notes for the spoofing agent

-   The report file is metadata only — no variable payloads are written.
    Variable values (including any that carry device-specific data) are
    read transiently in boot-services memory and scrubbed before free.
-   EFI variable names are UTF-16 hex code units in the file; the host
    parser decodes them. GUIDs are canonical text; attributes are the raw
    EFI attribute bits (nonvolatile/boot-service/runtime/auth).
-   A name matching `UUID/SERIAL/BOARD/SYSTEM/PRODUCT/MACHINE/HARDWARE/
    PLATFORM/CHASSIS` is only flagged `candidate` in host output; it is a
    lead, not proof, that the variable identifies hardware.
-   VM captures describe VM firmware, never the host NVRAM; only a
    capture made on this machine may be paired with host SMBIOS.
