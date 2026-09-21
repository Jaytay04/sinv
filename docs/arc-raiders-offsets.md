# ARC Raiders — Offsets & Structs Reference
# Source: https://www.unknowncheats.me/forum/arc-raiders/590414-arc-raiders-structs-offsets.html
# Pages scraped: 200–203 (latest as of 2026-09-21)
# SDK CL: 1341255 / 1372005 (patch 2026-09)

## Static Addresses (latest post-patch)

GWORLD           = 0x10F1C958   (static, was 0x80E9950)
GObjects         = 0x10D853F0
RVA_FNAME_POOL   = 0x10AB5DC0
RVA_FNAME_KEYS   = 0x109533B4
GameWorld        = 0x10967B98   (alternate GWorld / UWorld base RVA CL-1372005)

## UWorld Base

UWORLD_BASE_RVA  = 0x10967B98   (CL-1372005, live-verified 2026-09-08)
                                 (was 0xE782D78 in CL-1341255)
                                 Double-deref: [RVA] -> intermediary -> [+0x00] = UWorld
                                 Vtable: 0x14DD21510

## UWorld Offsets (CL-1372005)

PERSISTENT_LEVEL     = 0x120   (was 0x158, before that 0x110)
STREAMING_LEVELS     = 0x158   (was 0x170)
LEVELS               = 0x328   (was 0x360, before that 0x348)
INSTANCE_TIME        = 0x240   (double, UWorld::TimeSeconds, was 0x8B0)
TIME_SECONDS         = 0x240   (same as above)
GAME_INSTANCE        = 0x478   (OwningGameInstance, was 0x4F0)
LEVEL_COLLECTIONS    = 0x3A8   (was 0x370)
AUTHORITY_GAME_MODE  = 0x2C8   (was 0x2C0, before that 0x230)
PHYSICS_FIELD        = 0x5C0   (was 0x570, before that 0x4E0)
LINETRACE_RVA        = 0x2F6E930   (needs re-scan for 2026-08-11)
DEFAULT_QUERY_PARAMS_RVA = 0xE7A3098 (needs re-scan)

## FLevelCollection (UWorld::LevelCollections element, size=0x78)

GAME_STATE       = 0x08
NET_DRIVER       = 0x10
DEMO_NET_DRIVER  = 0x18
PERSISTENT_LEVEL = 0x20
LEVELS           = 0x28
STRUCT_SIZE      = 0x78
LOADED_LEVEL     = 0x200
LEVEL_TRANSFORM  = 0xF0
LT_ROTATION      = 0x00   (FQuat4d)
LT_TRANSLATION   = 0x20   (FVector3d)
LT_SCALE3D       = 0x40

## GameInstance Chain

UWORLD_GAME_INSTANCE = 0x478   (UWorld -> GameInstance)
GI_LOCAL_PLAYERS     = 0x138   (TArray<ULocalPlayer*>)
PLAYER_CONTROLLER    = 0x0A0   (APlayerController*)

## PlayerController Chain

PC_PLAYER_CAMERA_MANAGER = 0x4E0   (WARNING: maps to CheatClass in SDK dump, NOT real PCM)
PC_CONTROL_ROTATION      = 0x438   (FRotator pitch/yaw/roll)

NOTE: The real PlayerCameraManager offset must be found at runtime by searching
for "cameramanager" string. Once found, cache the offset or pointer.

## PlayerCameraManager (once live-pinned)

PCM_CAMERA_LOCATION = 0xCA0   (FVector, 3x double)
PCM_CAMERA_ROTATION = 0xCC0   (FRotator, 3x float)
PCM_CAMERA_FOV      = 0xCE8   (float, vertical FOV degrees)

## Component / Actor Offsets

ComponentToWorld = 0x2E0   (FTransform, 96 bytes, CL-1372005)
                   (user-reported alternatives: 0x370, 0x390, 0x190)
RootComponent    = 0x250
OwningGameInstance = 0x190

## Bone Decryption (CL-1372005 / patch 2026-09)

SEED_OFFSET           = 0x800   (16-byte XMM seed, was 0x7B0)
SELECTOR_OFFSET       = 0x854   (u32 selector dword)
SELECTOR_MASK         = 0x1     (& 1)
DESCRIPTOR_BASE       = 0xA8    (base offset into decrypted pointer, was 0xB0)
DESCRIPTOR_STRIDE     = 0x10    (16 bytes per descriptor slot)
BONE_STRIDE           = 0x60    (sizeof FTransform, unchanged)
LOD_OFFSET            = 0x7F8   (was 0x7D0)
DESCRIPTOR_OFFSET     = 0x48    (was 0xB0, added to decoded_base+lod_index before deref)

### SIMD Constants (CL-1372005)

XOR_KEY_LO            = 0xD4A8B60E86179E4E
ROL16_AMOUNT          = 13       (psllw 13 | psrlw 3)
PSHUFLW_IMM           = 0x1B     (reverse low 4 words)
ROL64_AMOUNT          = 50       (psllq 0x32 + psrlq 0x0E)
ROL32_AMOUNT          = 22       (pslld 0x16 + psrld 0x0A, was 5)
XOR_KEY               = 0        (v818 removed XOR)
PSHUFLW_IMM_OLD       = 0        (v818 removed PSHUFLW, uses PSHUFB only)

### Bone Decrypt Pipeline (v818 / CL-1372005)

Seed @ 0x800 (16B)
Pipeline = ROL64(50) + PSHUFB{5,0,4,6,7,2,3,1} + ROL32(22)
NO XOR in v818
LOD @ 0x7F8 (SHR 27 & 0xFFFFFFF0)
Descriptor read at (decoded + lod) + 0x48

### Bone Array Resolution

Selector = Read<u32>(MeshComponent + 0x854) & 1
Descriptor = BasePtr + 0xA8 + 0x10 * Selector
BoneArrayPtr = Read<u64>(Descriptor + 0)
BoneCount    = Read<i32>(Descriptor + 8)

### Bone Transform Layout (per bone at BoneArray + Index * 0x60)

+0x00: FQuat Rotation     (4 doubles: X, Y, Z, W)
+0x20: FVector Translation (3 doubles: X, Y, Z + padding)
+0x40: FVector Scale3D     (3 doubles: X, Y, Z + padding)

### Fallback

CACHED_COMPONENT_SPACE_TRANSFORMS = 0xB80   (TArray<FTransform>, unencrypted path)

## GameInstance Decrypt

XOR_KEY_RVA      = 0xB06C380
XOR_KEY_RVA_2    = 0xB06C390
SHUFFLE_MASK_RVA = 0xB09C350
WORLD_OFFSET     = 0x2F0   (needs RE for new build)
MIN_VALID_POINTER = 0x1000
MAX_VALID_POINTER = 0x7FFFFFFFFFFF

## PlayerName Decrypt

SIMD_MASK_RVA = 0xAD2FC50   (SDK CL-1341255 XOR key, was PSHUFB mask at 0xAB977C0)
XMM_XOR_VAL   = 0xA738DD8241D227C2

## Viewport Notes

0x348 = GameViewportClientClass (class definition only, NOT active instance)
For active viewport: use GameViewport from within UEngine, or get through LocalPlayer.

## FOV / Brightness Notes

LockedFOV offset may have changed from +0x4 relative to DefaultFOV.
Users report +4, +8, +12 no longer work for Change FOV.
Brightness offsets also need updating.

## Full Chain Summary

UWorld + 0x478           -> GameInstance
GameInstance + 0x138     -> LocalPlayers (TArray)
ULocalPlayer + 0xA0      -> PlayerController
PlayerController + 0x4E0 -> PlayerCameraManager (NEEDS LIVE-PIN via "cameramanager")
PlayerCameraManager + 0xCA0 -> CameraLocation (double x,y,z)
PlayerCameraManager + 0xCC0 -> CameraRotation (float pitch,yaw,roll)
PlayerCameraManager + 0xCE8 -> FOV (float, degrees)

## Important Caveats

1. PC_PLAYER_CAMERA_MANAGER (0x4E0) maps to CheatClass in SDK dump.
   Real PCM offset must be found at runtime.
2. Bone decryption XOR key changed in CL-1372005. Old keys will NOT work.
3. GWORLD static address changed significantly (0x80E9950 -> 0x10F1C958).
4. All offsets are patch-dependent. Verify against your game build.
5. Never run experimental payloads on primary hosts. Use disposable VMs.
