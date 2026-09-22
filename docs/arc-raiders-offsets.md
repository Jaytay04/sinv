# ARC Raiders — Offsets & Structs Reference
# Source: https://www.unknowncheats.me/forum/arc-raiders/590414-arc-raiders-structs-offsets.html
# Pages scraped: 200–204 (latest as of 2026-09-22)
# SDK CL: 1341255 / 1372005 / post-2026-09-22 patch
#
# ⚠️ GAME UPDATED 2026-09-22 — many offsets changed. See "LATEST (2026-09-22 patch)" sections.

## ============================================================================
## LATEST STATIC ADDRESSES (2026-09-22 patch)
## ============================================================================

GWORLD           = 0x10971338   (reported by qwe900)
GWORLD_ALT       = 0x10839A98   (reported by another user — verify against your build)
GNAMES           = 0x10987D40
KEYTABLE         = 0x1082B26C
FString_Verification = 0xD3C29A0

## Previous (CL-1372005, pre-update)
# GWORLD           = 0x10F1C958
# GObjects         = 0x10D853F0
# RVA_FNAME_POOL   = 0x10AB5DC0
# RVA_FNAME_KEYS   = 0x109533B4
# GameWorld        = 0x10967B98

## ============================================================================
## LATEST STRUCT OFFSETS (2026-09-22 patch)
## ============================================================================

UStruct_SuperStruct       = 0xB0
UStruct_PropertyLink      = 0x118
FProperty_PropertyLinkNext = 0xD8
FField_ClassPrivate       = 0x60
FObjectPropertyBase_PropertyClass = 0x118
FFieldClass_SuperClass    = 0x40

ustruct_super   = 0xB0
ffield_next     = 0x68
child_properties = 0xE0
pe_index        = 0x4C

## ============================================================================
## LATEST FNAME / FSTRING DECRYPTION (2026-09-22 patch)
## ============================================================================

### FField Name Decrypt
source = Read<u64>(field_buf + 0x90)
value = ROL64(source + 0x44CB31F912F95FFF, 17)
value ^= Read<u64>(field_buf + 0x98)
value = ROL64(value + 0xBB34B0C01E5AA000, 47)

### FName Decrypt (from IDA)
v15 = ROL8(
    Read<u64>(v19 + 0x90)
        ^ (Read<u64>(v19 + 0x98) - ROL8(Read<u64>(v19 + 0x90) + 0x44CB31F912F95FFF, 17))
        - 0x44CB4F3FE1A56000,
    17
)

### FField Offset Decrypt (FFProperty)
encrypted = Read<u32>(property_buf + 0xBC)
decrypted = BSWAP32(encrypted ^ 0xDE28E18D)

### FField::get_offset() (inline)
encrypted = Read<u32>(pointer + 0x2F * sizeof(DWORD))  // 0xBC
decrypted = encrypted ^ 0xDE28E18D
page = (decrypted >> 16) & 0xFF
offset_in_page = (decrypted >> 24) & 0xFF
return (page * 0x100) + offset_in_page

### FName Pool Decoding
fname_pool_selector(address):
    v = ROL32(u32(address), 17) * 0x01000193 + 0xD69AD929
    v = ROL32(v, 27) * 0x01000193 + u32(address >> 32) + 0xD69AD929
    v = ROL32(v, 17) * 0x01000193 + 0xD69AD929
    v = (v >> 5) * 0x01000193 + 0xD69AD929
    return v

decode_fname_pool_first(slot):
    an = {0x0A,0x9B,0x74,0x95,0x58,0xDD,0x72,0x8D}
    aa = {0xF5,0x64,0x8B,0x6A,0xA7,0x22,0x8D,0x72}
    xx = {0x90,0xA6,0x69,0x03,0xE6,0x1D,0x73,0x79}

decode_fname_pool_second(slot):
    xx = {0x9A,0x3D,0x1D,0x96,0xBE,0xC0,0x01,0xF4}

decode_fname_pool_address(gnames_address, fname_pools_buf, chunk_offset):
    selector = fname_pool_selector(gnames_address + chunk_offset + 0x40)
    slots = fname_pools_buf + chunk_offset + 0x50
    first = decode_fname_pool_first(slots + (selector & 7) * 0x20)
    second = decode_fname_pool_second(slots + ((selector + 1) & 7) * 0x20)
    mixed = ROL64(first, 40) * 0x00000100000001B3 + 0x6C5FD4827126D389
    mixed = ROL64(mixed, 50) * 0x00000100000001B3 + 0x6C5FD4827126D389
    return first | (second << 32)  // or combined per implementation

decode_fname_header_len(hdr):
    return ((u32(hdr) >> 3) & 0x3F8) + (u32(hdr) >> 13)

decode_fname(hdr, buf, key_table_base):
    length = decode_fname_header_len(hdr)
    table = key_table_base + 0x18 (as u16*)
    state = length + 0xADF
    for each byte:
        state = state * 0x00762E41 + 0xFE5AE580

decode_fstring_byte(encrypted, state):
    value = u32(int8_t(encrypted))
    state = ROL32(state * 0x01000193 + 0xD351FEEC, 14) + state) * 0x01000193
    // ... character class remapping logic

### UObject Slot Decode
indices = {6, 5, 1, 2, 7, 3, 4, 0}
address = game_address + 0x10
v = ROL32(u32(address), 28) * 0x01000193 + 0x2306CC41
v = ROL32(v, 21) * 0x01000193 + u32(address >> 32) + 0x2306CC41
v = ROL32(v, 28) * 0x01000193 + 0x2306CC41
v = (v >> 11) * 0x01000193 + 0x2306CC41
first = 0x1ADA36649C975181
second = 0x8E9484400ADF26C1

## ============================================================================
## LATEST BONE ARRAY DECRYPTION (2026-09-22 patch)
## ============================================================================

### NEW: XOR + ADD pipeline (replaces CL-1372005 ROL/PSHUFB)

Enc = Read<__m128i>(MeshComponent + 0x7B0)     // 16-byte seed (BACK to 0x7B0 from 0x800)
V = _mm_xor_si128(Enc, 0xC05F21012B46E100)     // XOR key (combined low64)
V = _mm_add_epi32(V, 0x3FA0DEFFD4B91F00)       // ADD key (combined low64)
Selector = (Read<u32>(MeshComponent + 0x848) >> 15) & 1   // NEW: was 0x854 >> 0

### decode_bonearray_table_address
lo = ROL32(Read<u32>(mesh + 0x7B0) ^ 0x2B46E100, 3) + 0xD4B91F00
hi = ROL32(Read<u32>(mesh + 0x7B4) ^ 0xC05F2101, 3) + 0x3FA0DEFF
return (hi << 32) | lo

### Previous (CL-1372005, pre-update) — NO LONGER WORKS
# SEED_OFFSET = 0x800
# XOR_KEY_LO = 0xD4A8B60E86179E4E
# ROL16_AMOUNT = 13
# PSHUFLW_IMM = 0x1B
# SELECTOR_OFFSET = 0x854

## ============================================================================
## LATEST COMPONENT OFFSETS (2026-09-22 patch)
## ============================================================================

ComponentToWorld_Rotation    = 0x310
ComponentToWorld_Translation = 0x330

## Previous (CL-1372005)
# ComponentToWorld = 0x2E0 (full FTransform)

## ============================================================================
## PREVIOUS OFFSETS (CL-1372005 / pre-2026-09-22) — MAY BE STALE
## ============================================================================

### UWorld Base
UWORLD_BASE_RVA  = 0x10967B98   (CL-1372005, live-verified 2026-09-08)
                                 (was 0xE782D78 in CL-1341255)

### UWorld Offsets
PERSISTENT_LEVEL     = 0x120
STREAMING_LEVELS     = 0x158
LEVELS               = 0x328
INSTANCE_TIME        = 0x240
GAME_INSTANCE        = 0x478
LEVEL_COLLECTIONS    = 0x3A8
AUTHORITY_GAME_MODE  = 0x2C8
PHYSICS_FIELD        = 0x5C0

### FLevelCollection (size=0x78)
GAME_STATE       = 0x08
NET_DRIVER       = 0x10
DEMO_NET_DRIVER  = 0x18
PERSISTENT_LEVEL = 0x20
LEVELS           = 0x28
LOADED_LEVEL     = 0x200
LEVEL_TRANSFORM  = 0xF0

### GameInstance Chain
UWORLD_GAME_INSTANCE = 0x478
GI_LOCAL_PLAYERS     = 0x138
PLAYER_CONTROLLER    = 0x0A0

### PlayerController Chain
PC_PLAYER_CAMERA_MANAGER = 0x4E0   (WARNING: maps to CheatClass, needs live-pin)
PC_CONTROL_ROTATION      = 0x438

### PlayerCameraManager (once live-pinned)
PCM_CAMERA_LOCATION = 0xCA0
PCM_CAMERA_ROTATION = 0xCC0
PCM_CAMERA_FOV      = 0xCE8

### Component / Actor
RootComponent      = 0x250
OwningGameInstance = 0x190

### GameInstance Decrypt (CL-1372005)
XOR_KEY_RVA      = 0xB06C380
XOR_KEY_RVA_2    = 0xB06C390
SHUFFLE_MASK_RVA = 0xB09C350
WORLD_OFFSET     = 0x2F0

### PlayerName Decrypt (CL-1372005)
SIMD_MASK_RVA = 0xAD2FC50
XMM_XOR_VAL   = 0xA738DD8241D227C2

### Viewport Notes
0x348 = GameViewportClientClass (class def only, not active instance)

### FOV / Brightness Notes
LockedFOV offset may have changed. Users report +4, +8, +12 no longer work.

## ============================================================================
## FULL CHAIN SUMMARY (latest known)
## ============================================================================

UWorld + 0x478           -> GameInstance
GameInstance + 0x138     -> LocalPlayers (TArray)
ULocalPlayer + 0xA0      -> PlayerController
PlayerController + 0x4E0 -> PlayerCameraManager (NEEDS LIVE-PIN)
PlayerCameraManager + 0xCA0 -> CameraLocation
PlayerCameraManager + 0xCC0 -> CameraRotation
PlayerCameraManager + 0xCE8 -> FOV

## ============================================================================
## IMPORTANT CAVEATS
## ============================================================================

1. GWORLD changed significantly in 2026-09-22 patch. Two values reported:
   0x10971338 and 0x10839A98. VERIFY against your build.
2. Bone decrypt pipeline COMPLETELY changed: XOR+ADD replaces ROL/PSHUFB.
   Seed location moved back to 0x7B0 (was 0x800 in CL-1372005).
3. Selector offset changed to 0x848 (was 0x854), shift changed to >> 15.
4. ComponentToWorld split into Rotation@0x310 and Translation@0x330.
5. FName/FString decryption is now fully encrypted with ROL/XOR chains.
6. FField offset decrypt uses XOR 0xDE28E18D + BSWAP32 at +0xBC.
7. All offsets are patch-dependent. VERIFY against your game build.
8. Never run experimental payloads on primary hosts. Use disposable VMs.
