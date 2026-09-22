#ifndef HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H
#define HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H
#ifdef MODULE_ESP
#include <cstdint>
namespace hypersinv::esp::arc_raiders {
/* Static / RVA anchors (2026-09-22 patch; previous CL-1372005 set superseded). */
inline constexpr uint64_t kWORLD_RVA          = 0x10971338u; /* GWORLD -> UWorld (double-deref), reported by qwe900 */
inline constexpr uint64_t kWORLD_ALT          = 0x10839A98u; /* alternate GWORLD report, needs verify */
inline constexpr uint64_t kG_OBJECTS          = 0x10D853F0u; /* GUObjectArray (CL-1372005, needs re-pin) */
inline constexpr uint64_t kGNAMES_RVA         = 0x10987D40u; /* GNames base */
inline constexpr uint64_t kKEYTABLE_RVA       = 0x1082B26Cu; /* name key table */
inline constexpr uint64_t kFSTRING_VERIFY_RVA = 0x0D3C29A0u; /* FString verification routine */
/* Game chain offsets (latest known per 2026-09-22 doc; live re-verify pending). */
inline constexpr uint32_t kUWORLD_GAME_INSTANCE    = 0x00000478u; /* UWorld +0x478 */
inline constexpr uint32_t kGI_LOCAL_PLAYERS        = 0x00000138u; /* TArray<ULocalPlayer*> */
inline constexpr uint32_t kLP_PLAYER_CONTROLLER    = 0x000000A0u; /* ULocalPlayer +0xA0 */
inline constexpr uint32_t kPC_CONTROL_ROTATION     = 0x00000438u; /* FRotator */
inline constexpr uint32_t kPC_PLAYER_CAMERA_MGR    = 0x000004E0u; /* PC -> PCM, live-pinned via class scan */
inline constexpr uint32_t kPCM_CAMERA_LOCATION     = 0x00000CA0u; /* FVector doubles */
inline constexpr uint32_t kPCM_CAMERA_ROTATION     = 0x00000CC0u; /* FRotator floats */
inline constexpr uint32_t kPCM_CAMERA_FOV          = 0x00000CE8u; /* vertical degrees */
/* Actor / scene-component offsets. */
inline constexpr uint32_t kPERSISTENT_LEVEL        = 0x00000120u; /* UWorld +0x120 */
inline constexpr uint32_t kLEVEL_Actors            = 0x00000098u; /* FLevel +0x98 TArray */
inline constexpr uint32_t kACTOR_ROOT_COMPONENT    = 0x00000250u;
inline constexpr uint32_t kOWNING_GAME_INSTANCE    = 0x00000190u;
/* 2026-09-22: world transform split, was single 96B FTransform at 0x2E0. */
inline constexpr uint32_t kCOMPONENT_C2W_ROTATION  = 0x00000310u; /* quat, 4 doubles */
inline constexpr uint32_t kCOMPONENT_C2W_TRANSLATION = 0x00000330u; /* FVector, 3 doubles */
/* Mesh-component bone anchors (2026-09-22 pipeline; port pending). */
inline constexpr uint32_t kBONE_SEED               = 0x000007B0u; /* 16B seed */
inline constexpr uint32_t kBONE_SELECTOR           = 0x00000848u; /* (u32 >> 15) & 1 */
inline constexpr uint64_t kBONE_SEED_XOR           = 0xC05F21012B46E100ull; /* xmm XOR */
inline constexpr uint64_t kBONE_SEED_ADD           = 0x3FA0DEFFD4B91F00ull; /* xmm ADD */
/* Struct sizes / caps. */
inline constexpr uint32_t kMAX_ACTORS_PER_FRAME   = 256; /* matches overlay cap */
/* Entity classification flags. */
inline constexpr uint32_t kCLASSFLAG_IsPlayer     = 0x00000001u;
inline constexpr uint32_t kCLASSFLAG_IsAI         = 0x00000002u;
inline constexpr uint32_t kCLASSFLAG_IsVehicle    = 0x00000004u;
inline constexpr uint32_t kCLASSFLAG_IsLoot       = 0x00000008u;
inline constexpr uint32_t kCLASSFLAG_IsExtraction = 0x00000010u;
/* ESP colors, 0xAARRGGBB. */
inline constexpr uint32_t kCOLOR_Player           = 0xFF00FF00u;
inline constexpr uint32_t kCOLOR_AI               = 0xFFFF0000u;
inline constexpr uint32_t kCOLOR_Vehicle          = 0xFF0080FFu;
inline constexpr uint32_t kCOLOR_Loot             = 0xFFFFFF00u;
inline constexpr uint32_t kCOLOR_Extraction       = 0xFF00FFFFu;
/* Compat aliases for the old camera/hardened/pointer_chains callers. */
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
