#ifndef HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H
#define HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H
#ifdef MODULE_ESP
#include <cstdint>
namespace hypersinv::esp::arc_raiders {

/* ── Static / RVA anchors (CL-1426848, 2026-10-13) ── */
inline constexpr uint64_t kWORLD_RVA          = 0x10D59B18u; /* GWORLD -> UWorld (CL-1426848; old 0x10971338) */
inline constexpr uint64_t kWORLD_ALT          = 0x10839A98u; /* alternate GWORLD report, needs verify (unchanged) */
inline constexpr uint64_t kG_OBJECTS          = 0x111794F0u; /* GUObjectArray blob (CL-1426848; old 0x10D853F0) */
inline constexpr uint64_t kGNAMES_RVA         = 0x10EB1180u; /* GNames base (CL-1426848; old 0x10987D40) */
inline constexpr uint64_t kKEYTABLE_RVA       = 0x10D4B30Cu; /* string keystream (CL-1426848; old 0x1082B26C) */
inline constexpr uint64_t kFSTRING_VERIFY_RVA = 0x0D3C29A0u; /* FString verification routine (unchanged) */

/* ── World / Actor chain (CL-1426848, danielkccn p213/214) ── */
inline constexpr uint32_t kUWORLD_GAME_INSTANCE    = 0x00000478u; /* UWorld +0x478 (unchanged) */
inline constexpr uint32_t kGI_LOCAL_PLAYERS        = 0x00000108u; /* UGameInstance (CL-1426848; old 0x138) */
inline constexpr uint32_t kLP_PLAYER_CONTROLLER    = 0x00000098u; /* UPlayer (CL-1426848; old 0x0A0) */
inline constexpr uint32_t kPC_CONTROL_ROTATION     = 0x00000438u; /* FRotator (unchanged) */
inline constexpr uint32_t kPC_PLAYER_CAMERA_MGR    = 0x000004E0u; /* PC -> PCM, live-pinned via class scan (unchanged) */
inline constexpr uint32_t kPCM_CAMERA_LOCATION     = 0x00000CA0u; /* FVector doubles (unchanged) */
inline constexpr uint32_t kPCM_CAMERA_ROTATION     = 0x00000CC0u; /* FRotator floats (unchanged) */
inline constexpr uint32_t kPCM_CAMERA_FOV          = 0x00000CE8u; /* vertical degrees (unchanged) */
inline constexpr uint32_t kPERSISTENT_LEVEL        = 0x00000148u; /* UWorld (CL-1426848; old 0x120) */
inline constexpr uint32_t kACTOR_ARRAY             = 0x00000108u; /* ULevel (CL-1426848; old 0x98) */
inline constexpr uint32_t kACTOR_ROOT_COMPONENT    = 0x00000240u; /* AActor (CL-1426848; old 0x250) */
inline constexpr uint32_t kOWNING_GAME_INSTANCE    = 0x00000190u; /* unchanged */
inline constexpr uint32_t kPLAYER_STATE            = 0x000003E0u; /* APawn (CL-1426848) */
inline constexpr uint32_t kPLAYER_NAME_PRIVATE     = 0x00000458u; /* APlayerState (CL-1426848; old 0x468) */
inline constexpr uint32_t kHEALTH_COMPONENT        = 0x00000D78u; /* UPioneerPlayerCharacter (CL-1426848) */
inline constexpr uint32_t kCACHED_HEALTH           = 0x00000700u; /* UHealthComponent (CL-1426848) */
inline constexpr uint32_t kARMOR                   = 0x000001C0u; /* UActorHealthComponent (CL-1426848) */
inline constexpr uint32_t kTEAM_ID                 = 0x000007F2u; /* AEmbarkCharacterBase (CL-1426848) */
inline constexpr uint32_t kVIEW_TARGET             = 0x000004C0u; /* APlayerCameraManager (CL-1426848) */
inline constexpr uint32_t kINVENTORY_COMPONENT     = 0x00000C90u; /* UPioneerPlayerCharacter (unchanged) */
inline constexpr uint32_t kCURRENT_ITEM_ACTORS     = 0x00000520u; /* UInventoryComponent (CL-1426848; old 0x4E0) */
inline constexpr uint32_t kITEM_STRIDE             = 0x00000020u; /* unchanged */
inline constexpr uint32_t kITEM_DATA               = 0x00000010u; /* unchanged */
inline constexpr uint32_t kLOCAL_PAWN              = 0x00000418u; /* AController (CL-1426848) */

/* ── Camera POV (FMinimalViewInfo at ViewTarget+0x10, CL-1426848) ── */
inline constexpr uint32_t kPOV_LOCATION            = 0x00000008u; /* FMinimalViewInfo+0x08 (CL-1426848; old 0xCA0 from PCM) */
inline constexpr uint32_t kPOV_ROTATION            = 0x00000028u; /* FMinimalViewInfo+0x28 (CL-1426848; old 0x30) */
inline constexpr uint32_t kPOV_FOV                 = 0x00000048u; /* FMinimalViewInfo+0x48 (CL-1426848; old 0x50) */
inline constexpr uint32_t kPOV_LOCKED_FOV          = 0x00000454u; /* FMinimalViewInfo+0x454 (CL-1426848; new) */

/* ── USceneComponent (CL-1426848) ── */
inline constexpr uint32_t kCOMPONENT_C2W_ROTATION  = 0x00000320u; /* quat, 4 doubles (CL-1426848; old 0x310) */
inline constexpr uint32_t kCOMPONENT_C2W_TRANSLATION = 0x00000340u; /* FVector, 3 doubles (CL-1426848; old 0x330) */
inline constexpr uint32_t kCOMPONENT_C2W_SCALE     = 0x00000360u; /* (CL-1426848; new) */
inline constexpr uint32_t kCOMPONENT_REL_LOCATION  = 0x00000280u; /* (CL-1426848; unchanged) */
inline constexpr uint32_t kCOMPONENT_REL_ROTATION  = 0x00000298u; /* (CL-1426848; new) */
inline constexpr uint32_t kCOMPONENT_REL_SCALE     = 0x000002B0u; /* (CL-1426848; new) */
inline constexpr uint32_t kCOMPONENT_VELOCITY      = 0x000002C8u; /* (CL-1426848; new) */

/* ── Bone / Skeleton (CL-1426848, lordzax p212) ── */
inline constexpr uint32_t kBONE_MESH               = 0x00000450u; /* ACharacter::Mesh (CL-1426848; 0x450 adopted, 0x440 candidate) */
inline constexpr uint32_t kBONE_C2W                = 0x00000320u; /* ComponentToWorld (CL-1426848; old 0x320, same as scene) */
inline constexpr uint32_t kBONE_TARRAY             = 0x00000B68u; /* TArray<FTransform> (CL-1426848; new) */
inline constexpr uint32_t kBONE_STRIDE             = 0x00000060u; /* UE5 LWC double (unchanged) */
inline constexpr uint32_t kBONE_SEED               = 0x000007B0u; /* 16B seed (unchanged from 2026-09-22) */
inline constexpr uint32_t kBONE_SELECTOR           = 0x00000848u; /* (u32 >> 15) & 1 (unchanged) */
inline constexpr uint64_t kBONE_SEED_XOR           = 0xC05F21012B46E100ull; /* xmm XOR (unchanged) */
inline constexpr uint64_t kBONE_SEED_ADD           = 0x3FA0DEFFD4B91F00ull; /* xmm ADD (unchanged) */

/* ── GUObjectArray (CL-1426848, ametu p213 — pure XOR) ── */
inline constexpr uint64_t kGUOBJECTARRAY_ENC       = 0x4993C8DE308F8828ull; /* FUObjectArray enc (CL-1426848; old SIMD) */
inline constexpr uint32_t kGUOBJECTARRAY_NUM_XOR   = 0x2A2B288Du; /* @FUO+0x14 (CL-1426848) */
inline constexpr uint64_t kGUOBJECTARRAY_CHUNKS_XOR = 0xAB82E5D500000000ull; /* @FUO+0x28 (CL-1426848) */
inline constexpr uint32_t kGUOBJECTARRAY_ITEM_OBJECT = 0x00000008u; /* FUObjectItem.Object (CL-1426848; old +0x10) */

/* ── UObject slot select (CL-1426848, new FNV32) ── */
inline constexpr uint32_t kSLOT_SEED_OFFSET        = 0x00000010u; /* actor+0x10 (CL-1426848) */
inline constexpr uint32_t kSLOT_FNV_PRIME          = 0x01000193u; /* FNV prime (CL-1426848) */
inline constexpr uint32_t kSLOT_FNV_ADD            = 0xAA679BF3u; /* FNV ADD (CL-1426848) */
inline constexpr uint32_t kSLOT_OFFSET             = 0x00000020u; /* base slot offset (CL-1426848) */
inline constexpr uint32_t kSLOT_STRIDE             = 0x00000020u; /* 16B cipher stride (CL-1426848) */
inline constexpr uint64_t kSLOT_CIPHER_XOR         = 0xE8B09504E6B96A82ull; /* first XOR (CL-1426848) */
inline constexpr uint32_t kSLOT_CIPHER_ROL64       = 27u; /* ROL64 amount (CL-1426848) */
inline constexpr uint32_t kSLOT_CIPHER_PSHUFLW     = 0x4Bu; /* PSHUFLW imm (CL-1426848) */
inline constexpr uint64_t kSLOT_CIPHER_XOR2        = 0xF73AF93C1907D6F9ull; /* second XOR (CL-1426848) */

/* ── FField / FFieldClass / FProperty (CL-1426848, ametu p213) ── */
inline constexpr uint32_t kFFIELD_NAME_PRIVATE     = 0x00000080u; /* field+0x80 (CL-1426848; old +0x90) */
inline constexpr uint32_t kFFIELD_CLASS_NAME       = 0x00000040u; /* ffclass+0x40 (CL-1426848) */
inline constexpr uint32_t kFPROPERTY_OFFSET        = 0x000000C0u; /* field+0xC0 (CL-1426848) */
inline constexpr uint32_t kFPROPERTY_XOR           = 0xBE0967DDu; /* bswap32(stored ^ XOR) (CL-1426848) */

/* ── FName pool / strings (CL-1426848, ametu p213) ── */
inline constexpr uint64_t kFNAME_POOL_RVA          = 0x10EAA140ull; /* pool base (CL-1426848) */
inline constexpr uint32_t kFNAME_CHUNK_STRIDE      = 0x00000100u; /* chunk stride (CL-1426848) */
inline constexpr uint32_t kFNAME_SLOTS_OFFSET      = 0x00007060u; /* slots offset (CL-1426848) */
inline constexpr uint32_t kFNAME_SLOT_STRIDE       = 0x00000020u; /* slot stride (CL-1426848) */
inline constexpr uint64_t kFNAME_CLMUL_K1          = 0x85FCE63D13122635ull; /* dual-CLMUL K1 (CL-1426848) */
inline constexpr uint64_t kFNAME_CLMUL_K2          = 0xE973108E0AED0639ull; /* dual-CLMUL K2 (CL-1426848) */
inline constexpr uint64_t kFNAME_FNV64_PRIME       = 0x100000001B3ull; /* FNV64 prime (CL-1426848) */
inline constexpr uint64_t kFNAME_FNV64_ADD         = 0x9EC944098CCFFE8Cull; /* FNV64 ADD (CL-1426848) */
inline constexpr uint32_t kFNAME_WIDE_BIT          = 15u; /* bit 15 (CL-1426848) */
inline constexpr uint32_t kFNAME_LEN_MASK          = 0x000003FCu; /* ((hdr>>3)&0x3FC (CL-1426848) */
inline constexpr uint32_t kFNAME_LEN_SHIFT2        = 13u; /* ((hdr>>13)&3 (CL-1426848) */

/* ── Keystream (CL-1426848) ── */
inline constexpr uint64_t kKEYSTREAM_RVA           = 0x10D4B30Cu; /* string keystream (CL-1426848; same as KEYTABLE) */
inline constexpr uint32_t kKEYSTREAM_COUNT         = 64u; /* 64 x u16 (CL-1426848) */
inline constexpr uint32_t kKEYSTREAM_NARROW_STEP   = 106u; /* narrow step (CL-1426848) */
inline constexpr uint32_t kKEYSTREAM_NARROW_PAIR_OFF = 53u; /* pair offset (CL-1426848) */

/* ── TLS (CL-1426848) ── */
inline constexpr uint32_t kTLS_VALIDITY_OFFSET     = 0x00000EC0u; /* GameInstance+TLS[0]+0xEC0 (CL-1426848) */
inline constexpr uint32_t kTLS_BLOCKS_OFFSET       = 0x00000ED0u; /* blocks @ +0xED0 (CL-1426848) */
inline constexpr uint32_t kTLS_BLOCK_COUNT         = 8u; /* 8 blocks (CL-1426848) */
inline constexpr uint32_t kTLS_BLOCK_STRIDE        = 0x00000070u; /* stride 0x70 (CL-1426848) */
inline constexpr uint32_t kTLS_XMM_OFFSET          = 0x00000060u; /* xmm @ +0x60 (CL-1426848) */
inline constexpr uint32_t kTLS_GROUP2_OFFSET       = 0x00001290u; /* group2 @ +0x1290 (CL-1426848) */
inline constexpr uint32_t kTLS_GROUP2_STRIDE       = 0x00000090u; /* stride 0x90 (CL-1426848) */
inline constexpr uint64_t kTLS_IDX_RVA             = 0x0EAF5D78ull; /* TLS_IDX RVA (CL-1426848) */
inline constexpr uint32_t kTLS_CAMERA_MGR_ENC      = 0x000004B0u; /* PlayerController camera cam mgr enc (CL-1426848) */

/* ── Struct sizes / caps ── */
inline constexpr uint32_t kMAX_ACTORS_PER_FRAME   = 256u; /* matches overlay cap */

/* ── Entity classification flags ── */
inline constexpr uint32_t kCLASSFLAG_IsPlayer     = 0x00000001u;
inline constexpr uint32_t kCLASSFLAG_IsAI         = 0x00000002u;
inline constexpr uint32_t kCLASSFLAG_IsVehicle    = 0x00000004u;
inline constexpr uint32_t kCLASSFLAG_IsLoot       = 0x00000008u;
inline constexpr uint32_t kCLASSFLAG_IsExtraction = 0x00000010u;

/* ── ESP colors, 0xAARRGGBB ── */
inline constexpr uint32_t kCOLOR_Player           = 0xFF00FF00u;
inline constexpr uint32_t kCOLOR_AI               = 0xFFFF0000u;
inline constexpr uint32_t kCOLOR_Vehicle          = 0xFF0080FFu;
inline constexpr uint32_t kCOLOR_Loot             = 0xFFFFFF00u;
inline constexpr uint32_t kCOLOR_Extraction       = 0xFF00FFFFu;

/* ── Compat aliases for the old camera/hardened/pointer_chains callers ── */
inline constexpr uint32_t kGUObjectArray_Objects              = 0x00000010u;
inline constexpr uint32_t kActor_RootComponent                = kACTOR_ROOT_COMPONENT;
inline constexpr uint32_t kSceneComponent_ComponentToWorld    = kCOMPONENT_C2W_TRANSLATION;
inline constexpr uint32_t kPlayerController_ControlRotation   = kPC_CONTROL_ROTATION;
inline constexpr uint32_t kPlayerCameraManager                = kPC_PLAYER_CAMERA_MGR;
inline constexpr uint32_t kCameraManager_POV_Location         = kPCM_CAMERA_LOCATION;
inline constexpr uint32_t kCameraManager_POV_Rotation         = kPCM_CAMERA_ROTATION;
inline constexpr uint32_t kCameraManager_POV_FOV              = kPCM_CAMERA_FOV;
inline constexpr uint32_t kCameraManager_CameraCache          = kPCM_CAMERA_LOCATION; /* same field */
inline constexpr uint32_t kPlayerController_AcknowledgedPawn  = 0x000004F8u; /* pending live-pin */

}
#endif
#endif
