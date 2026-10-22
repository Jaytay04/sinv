# ARC Raiders — Offsets & Structs Reference
# Source: https://www.unknowncheats.me/forum/arc-raiders/590414-arc-raiders-structs-offsets.html
# Pages scraped: 205-217 (page 217 = thread tail; qwe900 #4325 v1020 dump + DennisNedry #4321 fname pipeline)
# Newest: v1020 / buildid 25979580 (2026-10-20) | CL-1426848 (2026-10-13) | CL-1424961 (2026-10-09) | 1341255 | 1372005
#
# GAME PATCHED DAILY 2026-10-08 -> 10-21. Newest = v1020 buildid 25979580 (GWORLD 0x10B18370, image 0x14236000).
# Older sets (CL-1426848 / CL-1424961 / 09-22 / 09-09) kept for cross-check. Everything below v1020 is stale unless marked.
# NOTE: page 217 posts are the 10-20 build; several fields carry 2026-10-21 retargets in comments — the game patched again that day.


## ============================================================================
## NEWEST - v1020 / buildid 25979580, 2026-10-20 (page 217: qwe900 #4325 + DennisNedry #4321 fname)
## ESP-relevant offsets only. Live-verified by qwe900 via /proc/mem where marked.
## Image base 0x14236000, BASE = 0x140000000. Supersedes the CL-1426848 set below.
## ============================================================================

### Static Addresses (RVAs from game base)
GWORLD      = 0x10B18370   // double-deref: [RVA] -> intermediary -> [+0x00] = UWorld (was 0x10D59B18)
GNAMEPOOL   = 0x10BF81C0   // FNamePool
FNAME_KEYS  = 0x10A99324   // string keystream (64 x u16)
GOBLOB      = 0x10EC7540   // GUObjectArray CM-SIMD encrypted blob
  CM keys   @ +0xD5BCA80 (shufflelo key) / +0xD5BCA90 (PSHUFB mask)
  pipeline  = PSHUFB(ROL32(shufflelo(enc,0xB1) ^ K1, 20), mask).lo64
  num       = bswap32(@FUO+0x0C ^ 0x88EFAA24)
  chunks    = bswap64(@FUO+0x20 ^ 0xCC9E96F200000000)
  FUObjectItem.Object @ +0x8, stride 0x18, 65536/chunk
GAMESTATE_GLOBAL = 0xDCA7C88
OBJECT_XOR_KEY_RVA = 0xD91F885

### Player / Character / World chain
World               = 0x10B18370
ActorArray          = 0x110   // ULevel (was 0x108); count @ 0x118, max @ 0x11C
LocalPlayers        = 0x130   // UGameInstance (was 0x108); elements are ENCRYPTED pointers
PlayerController    = 0x98    // UPlayer
LocalPawn           = 0x428   // AController::Pawn
PlayerState         = 0x3F0   // APawn
PersistentLevel     = 0x148   // UWorld (unchanged)
RootComponent       = 0x250   // AActor (was 0x240, +0x10)
MESH                = 0x450   // ACharacter/APawn (was 0x440, +0x10)
HealthComponent     = 0xD88   // UPioneerPlayerCharacter (was 0xD78; 0xD78 -> ItemContainerComponent)
InventoryComponent  = 0xC90   // UPioneerPlayerCharacter (live-pinned 2026-10-09)
TEAM_ID             = 0x802   // AEmbarkCharacterBase (live-verified; 0x7F2 reads all-zeros)

### Camera
PlayerCameraManager @ ctrl+0x4F0 is NULL - it is ENCRYPTED; use ResolveCameraManager fallback.
ViewTarget          = 0x438   // APlayerCameraManager (was 0x4C0); camera loc at VT+0x18
FMinimalViewInfo (POV @ ViewTarget+0x10):
  Location = 0x08
  Rotation = 0x30   // was 0x28
  FOV      = 0x50   // was 0x48; live 80.0f
  DesiredFOV = 0x5C / FirstPersonFOV = 0x60 / FirstPersonScale = 0x64 / OrthoWidth = 0x68
APlayerCameraManager: PCOwner = 0x3E0 (== Controller) / DefaultFOV = 0x3F8 (90.0f)
  LOCKED_FOV = 0x3FC (DefaultFOV+4) / DefaultOrthoWidth = 0x400
ControlRotation = 0x458 (AController, Pitch/Yaw/Roll doubles; was 0x420)

### USceneComponent (v1020 = -0x40 shift from CL-1426848)
COMPONENT_TO_WORLD  = 0x2E0   // live-verified: Rotation@0x2E0 / Translation@0x300 / Scale@0x320
  WORLD_LOCATION_DOUBLE = 0x300
RelativeLocation    = 0x240   // inferred -0x40 (was 0x280); RE-VERIFY
RelativeRotation    = 0x258 / RELATIVE_SCALE3D = 0x270 / ComponentVelocity = 0x288
AttachParent = 0x1B0 / AttachChildren = 0x1D0 (inferred -0x40; RE-VERIFY)
Visibility flags: B_VISIBLE_BYTE = 0x2A0 (mask 0x20), B_HIDDEN_IN_GAME = 0x2A1 (mask 0x10),
  MOBILITY_BYTE = 0x2A4 (MOVABLE = 0x02), DETAIL_MODE_BITMASK = 0x2A5

### Bone / Skeleton ESP (v1020 CM SIMD chain - same cipher as chunks_manager, IDA sub_143DAE370)
seed (xmm)  @ mesh + 0x7B0   // was 0x7E0 on CL-1426848
selector     @ mesh + 0x800  // &1 (was 0x7F4 on CL-1426848)
descriptor   @ base + 0x58, second buffer @ base + 0x68, stride 0x10 (was 0xA0 on CL-1426848)
bone stride  = 0x60 (FTransform3d doubles: quat +0x00 / trans +0x20 / scale +0x40)
CachedBoneSpaceTransforms = 0xBB8 / CachedComponentSpaceTransforms = 0xBC8 (SDK, RE-VERIFY)
AnimScriptInstance = 0xB08 (v1020 actor_inspect confirmed, was 0xB48)
LastPoseTickFrame  = 0x1118 (SDK CL-1424961; stale byte froze the pose scanner)
bNoSkeletonUpdate  = 0xC90 bit 0x08 / bPauseAnims = 0xC90 bit 0x10 / bForceRefpose = 0xC91 bit 0x01
  (0xCD0 is CachedAnimCurveUidVersion - writing there kills the mesh)
// 22-bone table (kBoneIdx / kBoneParent) is build-stable; compose parent->child then CTW->world.
//   kBoneIdx    = {0,1,2,3,4,5,6,7,8,9,10,11,42,43,44,45,65,66,67,69,70,71}
//   kBoneParent = {-1,0,1,2,3,4,5,6,4,8,9,10,4,12,13,14,1,16,17,1,19,20}
//   DO NOT draw slot0(root)->pelvis (third-leg artifact).

### Health / Shield (UHealthComponent, v1020 -0x58 shift)
Armor           = 0x168   // 40.0 live; 0x1C0 -> 1.0 garbage
CURRENT_ARMOR   = 0x160   // 0x168 broke shield
MAX_ARMOR       = 0x170
MaxHealth       = 0x320   // 175.0 live; 0x378 -> garbage
CURRENT_HEALTH  = 0x6A0   // real HP; 0x6A8 is MaxHealth (always 100.0)
CachedMaxHealth = 0x6A8   // reads 100.0 (base max HP)
HEALTH_DOWN     = 0x720   // DBNO health
HC_SHIELD = 0x6B0 / HC_SHIELD_MAX = 0x6B8   // alternative high-offset shield mirror (yemmy #4324)
// marioferreira #4328 cross-check: Armor = 0x160 / MaxArmor = 0x168.
FPlayerHealthInfo (inline at APioneerPlayerState + HEALTH_INFO_BASE 0x588, estimated):
  HEALTH 0x00 / MAX_HEALTH 0x08 / ARMOR 0x10 / MAX_ARMOR 0x18 / B_HAS_BROKEN_ARMOR 0x20 / B_IS_DBNO 0x21

### Visibility / render-time (UPrimitiveComponent, read off ACharacter::Mesh not root)
LAST_RENDER_TIME           = 0x490   // plain float, live-verified 2026-10-10
LAST_RENDER_TIME_ON_SCREEN = 0x494   // Visible := UWorld::TimeSeconds - this < 0.5
AActor: LAST_RENDER_TIME_HINT = 0x228 / LAST_SUBMIT_ON_SCREEN = 0x22C (user-verified, was 0x480/0x484)
Chams stencil (SDK CL-1424961): CustomDepthStencilWriteMask = 0x3FA / CustomDepthStencilValue = 0x404
  B_RENDER_CUSTOM_DEPTH = 0x3D6 bit 0x40
BoundsScale = 0x438 / BodyInstance = 0x4A0 / WORLD_PRIVATE = 0x140 / BLUEPRINT_CREATED_COMPONENTS = 0x380

### Inventory / weapons
CurrentItemActors = 0x4C0 (was 0x4E0; 2026-10-21 was 0x520); LocalCurrentItemActors = 0x4E0 (+0x20)
ItemActor stride = 0x20, DataAsset @ +0x10
Loadout = 0x260 / STOWED_WEAPON_0 = 0x340 / STOWED_WEAPON_1 = 0x380 (stride 0x40)
StowedWeaponLayout (0x40): STOWED_ACTOR +0x10 / ITEM_DATA_ASSET +0x18 (SDK said 0x08, NULL live)
  QUALITY_VISUAL +0x20 / WEAPON_QUALITY +0x38
WeaponActor: ClipSize = 0x4A0 (uint16) / WeaponQuality = 0x4A2 (int8)
EquippedArmor = 0x528 (RE-VERIFY) / EQUIPPED_PRIMARY_ITEM = 0x580 (RE-VERIFY)
Backpack = 0x440 / Belt = 0x448 / SafePouch = 0x450 / Auxiliary = 0x458 / StowedToolActor = 0x3C0

### PlayerState / status
PawnPrivate = 0x448 / PlayerNamePrivate = 0x468 (encrypted FString) / PLAYER_NAME_PRIVATE_2 = 0x478
PLAYER_NAME_FORMATTED = 0x4E0 / PLAYER_STATUS = 0x550 / PIONEER_CHARACTER = 0x568 / CURRENT_PAWN = 0x570
B_FINISHED_ROUND = 0x5B8 / ACHIEVEMENT_COMPONENT = 0x5C0 / SQUAD = 0x488 / PLATFORM_ID_COMPONENT = 0x480
B_IS_A_BOT = 0x3DA bit 0x08 (bIsSpectator bit 0x02) / StartTime = 0x3DC
PlayerStatusVar (UPioneerPlayerCharacter) = 0x12D8 (uint8; 0x1358 mislabels dead players as DBNO)
DBNO_COMPONENT = 0xCE8 / FollowCamera = 0xC80 / FlashlightComponent = 0xDA8 / TEAM_COMPONENT = 0x450
BaseInteractionComponent::CurrentInteractionState = 0x3E5 (SDK CL-1424961; 0x415 stuck ESP on DBNO)
EDbnoInteractionState: Alive 0 / DBNO 1 / KnockoutInit 2 / NonSquadRevive 3 / SquadRevive 4 / Defib 5 / Dead 6 / Despawned 7
Character: B_IS_CROUCHED = 0x570 bit 0x02 / ReplicatedMovementMode = 0x572 (0 None,1 Walking,3 Falling,4 Swim,5 Fly)

### FName / UObject decode (v1020, DennisNedry #4321 - needed for name/class ESP)
UObject slot selector: FNV32 on (actor+0x10), ADD = 0xD1C10503, ROL32 24/15/24/15, sel = (b0^b2)&3
  slot @ actor+0x20+0x20*sel; name slot = base^2 (CI = hi32), class slot = base, outer slot = (base+1)&3
  decode = double-CLMUL PK1 = 0x5F75E41E6B350E21 / PK2 = 0xAB7D6FD0528CCF31
FField NamePrivate @ field+0xA0: inv_PSHUFB{4,3,7,0,2,1,5,6} -> ROL64(14) -> XOR 0xBB17372171600F55 -> inv_shufflelo(0x72); CI = hi32
FFieldClass name @ ffclass+0x40: plaintext CI in hi32 of lo64 (no cipher)
FProperty Offset_Internal @ field+0xF8: bswap32(stored) ^ 0x2E0C65AE
FNamePool @ 0x10BF81C0: chunk stride 0x100, seed @ +0x7090, slots @ +0x70A0 stride 0x20 x8
  shard FNV32 ADD = 0x8BDCE09D, ROL32 22/19/22, h>>13, sel = (h^(h>>16))&7
  slot dual-CLMUL K1 = 0xF8E0CC10528CCF31 / K2 = 0x166283C08F756DE1
  FNV64 mix ROL41 then ROL29, P = 0x100000001B3, A = 0x11B3103DDF13427D
  entry = first + (second ^ mix(first)) + 2*slot_idx
FName header @ entry: wide = bit15; len = hdr & 0x3FF (10-bit direct)
String keystream @ 0x10A99324 (64 u16): seed = (len+12), step = 2, pair_off = 1
  narrow: XOR ks[idx]>>3; wide: XOR full ks[idx]
PlayerName decrypt: ADD = 0x87400B27, ROL = 14 (IDA sub_1400518B0)
XMM_XOR_VAL = 0xA738DD8241D227C2 (pointer decryption key)

### UWorld / level chain (live-verified, ESP traversal support)
PERSISTENT_LEVEL = 0x148 / STREAMING_LEVELS = 0x198 / LEVELS = 0x488 / INSTANCE_TIME = 0xB68 (ticking double)
GAME_INSTANCE = 0x4A8 / LEVEL_COLLECTIONS = 0x380 / AUTHORITY_GAME_MODE = 0x298
FLevelCollection (size 0x78): GameState 0x08 / NetDriver 0x10 / PersistentLevel 0x20 / Levels 0x28
ULevel: ACTORS 0x110 / count 0x118 / max 0x11C / OwningWorld 0x138 / WorldSettings 0x328
LevelActorContainer::Actors = 0x98 (count @ 0xA0)
GameStateBase: PLAYER_ARRAY = 0x410 (live TArray, 4 PlayerStates in PracticeRange)
PioneerGameState (v1020 = -0x100 from SDK): STAGE_INFO 0x878 / CURRENT_STAGE_TIMER 0x920 / START_STAGE_TIMER 0x918
  PLAYER_STATES 0x900 / GAME_PHASE 0x910 / ENEMY_COUNT 0x940 / PICKUP_COUNT 0x944
  REPLICATED_WORLD_TIME_SECONDS_DOUBLE = 0x548
EmbarkGameStateBase: SQUADS_ARRAY = 0x4B8 (live TArray Count=10 in Salvage) / REPLICATED_WORLD_TIME_SECONDS_DOUBLE = 0x3C8

## ============================================================================
## SUPERSEDED - CL-1426848, 2026-10-13 (source: kingman11 p209/210 + danielkccn p213/214 + ametu p213)
## ============================================================================

### Static Addresses
GWORLD     = 0x10D59B18   // UWorld (live, Steam + SteamSDK)
GNAMES     = 0x10EB1180   // key table base (64 x u16, pairs @ idx+88)
GOBJECTS   = 0x111794F0   // GUObjectArray blob
KEYTABLE   = 0x10D4B30C   // string keystream (64 x u16)

### Player / Character / World chain (danielkccn, posted TWICE p213 + p214 — consistent)
World             = 0x10D59B18
ActorArray        = 0x108   // ULevel
LocalPlayers      = 0x108   // UGameInstance
PlayerController  = 0x98    // UPlayer
LocalPawn         = 0x418   // AController
RootComponent     = 0x240   // AActor
PlayerState       = 0x3E0   // APawn
PersistentLevel   = 0x148   // UWorld
RelativeLocation  = 0x280   // USceneComponent
PlayerNamePrivate = 0x458   // APlayerState
HealthComponent   = 0xD78   // UPioneerPlayerCharacter
CachedHealth      = 0x700   // UHealthComponent
Armor             = 0x1C0   // UActorHealthComponent
TeamID            = 0x7F2   // AEmbarkCharacterBase
ViewTarget        = 0x4C0   // APlayerCameraManager, POV at +0x10
InventoryComponent = 0xC90   // UPioneerPlayerCharacter
CurrentItemActors  = 0x520   // UInventoryComponent
ItemStride         = 0x20
ItemData         = 0x10

// Mesh candidates seen across the thread: 0x4A0 / 0x440 / 0x450 / 0x458.
//   10-09 build (CL-1424961) uses 0x450; later posts report 0x440. VERIFY against your build.

### FMinimalViewInfo (camera POV, at +0x10 off ViewTarget)
Location = 0x08
Rotation = 0x28
FOV      = 0x48
LockedFOV= 0x454   // reported by yemmy (p214)

// Old (2026-09-22) was: Rotation 0x30, FOV 0x50.

### USceneComponent (CTW)
rotation  = 0x320   // FTransform: quat+0x00 / trans+0x20 / scale+0x40
translation= 0x340
scale     = 0x360
RelativeLocation = 0x280
RelativeRotation = 0x298
RelativeScale    = 0x2B0
Velocity       = 0x2C8

// NOTE: Mesh + 0x320 is ALSO where Mesh ComponentToWorld is read (lordzax skeleton, below).

### GUObjectArray (ametu p213 — pure XOR, replaces old SIMD XOR+ROL32+ADD)
blob @ module + 0x111794F0
FUObjectArray = enc ^ 0x4993C8DE308F8828
num     = bswap32(@FUO+0x14 ^ 0x2A2B288D)
chunks  = bswap64(@FUO+0x28 ^ 0xAB82E5D500000000)
FUObjectItem.Object @ +0x8

### UObject slot select + decode (new FNV32)
// selector: seed = actor+0x10; p = FNV*(lo>>3)+ADD; p = FNV*ROL32(p,26);
//            p = (p+hi+ADD)>>3; p = FNV*p+ADD; p = FNV*(p>>6)+ADD; sel = ((p&0xFF)^((p>>16)&0xFF))&3
//   FNV prime = 0x01000193, ADD = 0xAA679BF3
// slot @ actor+0x20+0x20*sel  (16B cipher)
//   XOR 0xE8B09504E6B96A82 -> ROL64(x,27) -> PSHUFLW 0x4B -> XOR 0xF73AF93C1907D6F9
//   name slot = base^2 (result = ROL64(dec,32), CI=lo32); class slot = base; outer slot = (base+1)&3

### FField / FProperty (ametu p213)
FField NamePrivate    @ field+0x80   // blend 0x67B4DC19 ^ 0xFCDB9DA3F1423BAC ^ ... ROL64(32)
FFieldClass name      @ ffclass+0x40 // CI = ROL32(stored^0x29941481,1)^0x018A6E39
FProperty offset    @ field+0xC0   // bswap32(stored ^ 0xBE0967DD)

### FName pool / strings (ametu p213)
pool @ module+0x10EAA140, chunk stride 0x100, slots @ +0x7060, stride 0x20, 8 slots
dual-CLMUL  K1 = 0x85FCE63D13122635 / K2 = 0xE973108E0AED0639
FNV64 mix   ROL43/ROL33, P = 0x100000001B3, A = 0x9EC944098CCFFE8C
entry = first + (second ^ mix(first)) + 2*slot_idx
hdr: wide = bit15; len = ((hdr>>3)&0x3FC)|((hdr>>13)&3)
string keystream @ module+0x10D4B30C (64 u16); narrow seed=(len+18)&0xFF, step=106, pair_off=53; XOR with ks[idx]>>3

### Skeleton / Bone ESP (lordzax p212 — cross-patch, plain transforms, still current)
ACharacter -> Mesh : pawn + 0x450 (ACharacter::Mesh)
mesh + 0x320        // ComponentToWorld (inline FTransform, size 0x60)
mesh + 0xB68        // TArray<FTransform>: ptr @ +0x0, count(int32) @ +0x8 (~97, need >=72)
transform stride = 0x60 (UE5 LWC double: quat+0x00 / trans+0x20 / scale+0x40)
// old encrypted path: enc ptr @ mesh+0x7D0 | lod flag @ mesh+0x85C (&1) | keys @ mesh+0xAF8
//                    CachedBoneSpaceTransforms @ mesh+0xBB8
// 22-bone table (kBoneIdx / kBoneParent) is build-stable; compose parent->child then CTW->world.
//   kBoneIdx    = {0,1,2,3,4,5,6,7,8,9,10,11,42,43,44,45,65,66,67,69,70,71}
//   kBoneParent = {-1,0,1,2,3,4,5,6,4,8,9,10,4,12,13,14,1,16,17,1,19,20}
//   DO NOT draw slot0(root)->pelvis (third-leg artifact).

// PlayerName FString @ PlayerState+0x458 (was +0x468). Per-char FNV PRNG sub-table cosets 47/5/13/13/47.

// TLS: validity = GameInstance+TLS[0]+0xEC0==1; blocks @ +0xED0, 8 x stride 0x70, xmm @ +0x60;
//   group2 @ +0x1290 (stride 0x90) -> block5 vt+0x20 has UWorld. TLS_IDX RVA = 0x0EAF5D78.
//   PlayerController camera cam mgr enc @ ctrl+0x4B0.

## =========================================================================
## SECOND-LATEST — CL-1424961, 2026-10-09 (GWORLD 0x10A22A98, kingman11 p209/210 + DragonNight p210 + choirsoupy p210/211)
## Kept for cross-check — superseded by the v1020 set at the top.
## =========================================================================

GWORLD = 0x10A22A98 (live, Steam + SteamSDK, p209 kingman11)
GNAMES = 0x10B779F0
KEYTABLE = 0x10A1425C
GOBJECTS = 0x10E423D0 (GUObjectArray blob); key @ 0xD538AA0 / 0xD538AB0

World = 0x10A22A98
ActorArray        = 0x110   // ULevel
ActorArray count  = 0x118 // max @ 0x11C
LocalPlayers      = 0xF8    // UGameInstance
PlayerController  = 0xA0    // UPlayer
LocalPawn         = 0x428   // AController
RootComponent     = 0x248   // AActor
PlayerState       = 0x3F0   // APawn
PersistentLevel   = 0x128   // UWorld
RelativeLocation  = 0x280   // USceneComponent
PlayerNamePrivate = 0x468   // APlayerState
HealthComponent   = 0xDC8   // UPioneerPlayerCharacter
CachedHealth      = 0x6B0   // UHealthComponent
Armor             = 0x170   // UActorHealthComponent
TeamID            = 0x802   // AEmbarkCharacterBase
ViewTarget        = 0xE10   // POV @ +0x10
Mesh              = 0x450   // UPioneerPlayerCharacter
InventoryComponent = 0xC90
CurrentItemActors  = 0x4E0   // UInventoryComponent
ItemStride         = 0x20; ItemData = 0x10

// FMinimalViewInfo: Loc 0x08 / Rot 0x30 / FOV 0x50
// USceneComponent C2W: rot 0x320 / trans 0x340 / scale 0x360; RelLoc 0x280 / RelRot 0x298 / RelScale 0x2B0 / Vel 0x2C8

GUObjectArray (kingman11 p209 — SIMD XOR+ROL32+ADD, old style)
enc = xmm @ +0x10E423D0; K1 @ +0xD538AA0; K2 @ +0xD538AB0 (hi lane zeroed)
FUObjectArray = ADD32(ROL32(XOR(enc,K1),29),K2).lo64
num = bswap32(@FUO+0x14 ^ 0xA4E88268); chunks = bswap64(@FUO+0x20 ^ 0x26D0446400000000)
FUObjectItem.Object @ +0x0

FField NamePrivate @ field+0xA0 (dual PCLMULQDQ, CLMUL K1/K2 = 0x7DB8A1AB8CB5A63B / 0x1D29FDFC743060F1)

## =========================================================================
## PREVIOUS — 2026-09-22 patch (GWORLD 0x10971338 / 0x10839A98, qwe900 + anon) — MAY BE STALE
## =========================================================================

GWORLD = 0x10971338 / GWORLD_ALT = 0x10839A98 / GNAMES = 0x10987D40 / KEYTABLE = 0x1082B26C / FString_Verification = 0xD3C29A0

// Old struct offsets:
## Previous (CL-1372005, pre-update)
# GObjects         = 0x10D853F0
# RVA_FNAME_POOL   = 0x10AB5DC0
# RVA_FNAME_KEYS   = 0x109533B4
# GameWorld        = 0x10967B98

// Old struct offsets:
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

// Old FNAME / FSTRING decrypt:
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
encrypted = Read<u32>(pointer + 0x2F * sizeof(DWORD))  # 0xBC
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
    return first | (second << 32)  # or combined per implementation
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
    # ... character class remapping logic
### UObject Slot Decode
indices = {6, 5, 1, 2, 7, 3, 4, 0}
address = game_address + 0x10
v = ROL32(u32(address), 28) * 0x01000193 + 0x2306CC41
v = ROL32(v, 21) * 0x01000193 + u32(address >> 32) + 0x2306CC41
v = ROL32(v, 28) * 0x01000193 + 0x2306CC41
v = (v >> 11) * 0x01000193 + 0x2306CC41
first = 0x1ADA36649C975181
second = 0x8E9484400ADF26C1

// Old bone decrypt (XOR+ADD, seed @ 0x7B0):
### NEW: XOR + ADD pipeline (replaces CL-1372005 ROL/PSHUFB)
Enc = Read<__m128i>(MeshComponent + 0x7B0)     # 16-byte seed (BACK to 0x7B0 from 0x800)
V = _mm_xor_si128(Enc, 0xC05F21012B46E100)     # XOR key (combined low64)
V = _mm_add_epi32(V, 0x3FA0DEFFD4B91F00)       # ADD key (combined low64)
Selector = (Read<u32>(MeshComponent + 0x848) >> 15) & 1   # NEW: was 0x854 >> 0
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

// Old component:
ComponentToWorld_Rotation    = 0x310
ComponentToWorld_Translation = 0x330
## Previous (CL-1372005)
# ComponentToWorld = 0x2E0 (full FTransform)

## =========================================================================
## OLDER — CL-1372005, 2026-09-09 (live 09-08, pre-2026-09-22) — MAY BE STALE
## =========================================================================


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