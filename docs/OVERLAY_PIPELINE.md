# Authenticated analysis-to-overlay pipeline

## Architecture

The pipeline deliberately crosses three trust boundaries:

```text
VMX root HVA1 session
    -> authenticated, paginated ANALYSIS_RESULTS
LocalSystem companion CLI (`analysis-publish`)
    -> typed-field adapter and bounded transforms
    -> HVO2 named mapping (writer owns read-only client ACL)
Interactive `hypersinv_overlay.exe`
    -> stable snapshot, world-to-screen projection, D3D11/Direct2D draw
```

VMX root does not create a Windows named object. The companion is the bridge
because it already owns the exclusive authenticated device handle. The overlay
receives neither that handle nor the boot-scoped HMAC key/nonce.

## HVO2 publication

`overlay_protocol.h` defines `AnalysisResult`: a fixed header followed by at
most 256 `EntityData` records. The header contains entity count, source
generation/sequence, target window, producer QPC timestamp, scene flags, and an
optional row-major 4x4 view-projection matrix.

`SharedMemoryPublisher` serializes writers with a named mutex and uses a
seqlock-style update:

1. acquire the writer mutex;
2. publish an odd `Sequence` with `InterlockedExchange64`;
3. copy the validated header and fixed-size entries;
4. issue a full memory barrier;
5. publish the next even sequence and release the mutex.

The overlay maps with `FILE_MAP_READ`, copies only when the same even sequence
is observed before and after the payload, and drops invalid/busy frames. The
publisher rejects invalid headers and caps successful writes at 60 Hz. For a
LocalSystem session-zero producer and an interactive overlay, use a `Global\`
mapping with `PublisherAccess::AuthenticatedUsersRead`; its DACL grants SYSTEM
full access and Authenticated Users read access only.

## Analysis field mapping

The adapter recognizes these HVA1 extracted-field names:

| Field | HVA1 type | HVO2 meaning |
| --- | --- | --- |
| `view_projection` or `camera.matrix` | `mat4` | Row-major view-projection matrix. |
| `entity.<id>.position` | `vec3` | World coordinate; creates the entity. |
| `entity.<id>.name` | string | Display name after the configured transform. |
| `entity.<id>.box` | `vec4` | Width and height in elements 0 and 1. |
| `entity.<id>.color` | `i32` | `0xAARRGGBB`. |
| `entity.<id>.visible` | `i32` | Zero hides the entity. |

A bare `vec3` field uses its field name as the entity ID. Entities without a
position are omitted. A scan-only session falls back to screen-space diagnostic
rows named after matched signatures. All string copies, result counts, and
field counts are bounded by their HVA1/HVO2 ABI capacities.

The overlay multiplies `[x y z 1]` by the row-major matrix, rejects non-finite
or behind-camera values (`clipW <= 0.0001`), divides by `clipW`, and converts
normalized device coordinates to the current target-client dimensions.

## Configurable transformations

Transforms are pure user-mode functions over the bounded extracted byte array.
They do not write guest memory or execute guest code.

| CLI form | Algorithm |
| --- | --- |
| `none` | Format the bytes directly. |
| `xor:HEXKEY:SEED:STEP:ROTATE` | For byte `i`, XOR with `key[i mod n] XOR low8(state)`, then update `state = rol32(state + step + keyByte, rotate)`. Applying it twice with the same parameters restores the input. |
| `fnv32:OFFSET:PRIME` | Configurable FNV-1a 32-bit hash. |
| `fnv64:OFFSET:PRIME` | Configurable FNV-1a 64-bit hash. |
| `rol:BITS` / `ror:BITS` | Rotate each byte by 0-7 bits. |
| `sub:MULTIPLIER:ADDEND` | Affine substitution `(byte * multiplier + addend) mod 256`; multiplier must be odd. |
| `unsub:MULTIPLIER:ADDEND` | Inverse of the same generated substitution table. |

FNV is a one-way identifier transform, not decryption. The default constants
are:

| Variant | Offset basis | Prime |
| --- | ---: | ---: |
| FNV-1a 32 | `2166136261` (`0x811C9DC5`) | `16777619` (`0x01000193`) |
| FNV-1a 64 | `14695981039346656037` (`0xCBF29CE484222325`) | `1099511628211` (`0x00000100000001B3`) |

Output formatting is either escaped ASCII (`\\xNN` for non-printable bytes) or
UTF-16LE with surrogate validation and replacement characters for malformed
pairs.

Verified test vectors:

| Transform | Input | Output |
| --- | --- | --- |
| FNV-1a 32 defaults | ASCII `Hello` | `0xF55C314B` |
| FNV-1a 64 defaults | ASCII `Hello` | `0x63F0BFACF2C00F6B` |
| Rolling XOR, key `10 20 30`, seed `0x12345678`, step `0x01020304`, rotate 5 | ASCII `Alpha` | `29 CE 8C 67 2B` |
| Rotate-left 1 | `81 12` | `03 24` |
| Affine substitution, multiplier 197, addend 123 | `00 01 02 FF` | `7B 40 05 B6` |

## Running the bridge

First create and populate an authenticated analysis session. Name extracted
fields using the table above, then start the publisher as LocalSystem:

```powershell
hvr1_runtime_service.exe analysis-publish `
  "Global\HypersinvOverlayDataV2" `
  0x0000000000011234 `
  10000 `
  xor:102030:0x12345678:0x01020304:5 `
  ascii
```

Arguments are mapping name, target HWND, optional duration in milliseconds
(`0` means until Ctrl+C), optional transform, and output encoding. The command
retrieves offsets 0/32/64 as needed so all 64 fields and 128 scan results are
represented consistently. A generation, PID, or total-count change between
pages fails the frame rather than mixing snapshots.

Launch the overlay in the interactive session with the same mapping:

```powershell
hypersinv_overlay.exe --target-pid 1234 `
  --mapping "Global\HypersinvOverlayDataV2"
```

## Validation and latency claim

The automated gates cover:

- 1,000 concurrent publisher/reader frames with per-entry consistency checks;
- rejection of publication above 60 Hz;
- HVA1 typed-field to HVO2 mapping, transform, mapping publication, readback,
  and projection in under 50 ms on the build host;
- an integrated publisher, read-only snapshot, 3D projection, box/line/text
  render, and successful D3D presentation in 4.932 ms;
- all transform vectors in this document.

This proves the local software path and tear-free protocol. The production CLI
also reports maximum extraction-to-publication latency and the count at or
above 50 ms. Pinned disposable-VM run
`uefi-windows-probe-20260920-170822-67fa005610` published 20 live HVA1-derived
frames with a maximum extraction-to-publication latency of 0.451 ms and zero
samples at or above 50 ms. Together these close the extraction/publication and
publication/presentation timing gates. The claim ends at successful D3D
`Present`; it does not include physical display scan-out.
