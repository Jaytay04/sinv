#ifndef HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H
#define HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H
#ifdef MODULE_ESP
#include <cstdint>
namespace hypersinv::esp::arc_raiders {
/* Static / RVA anchors (CL-1372005, verified 2026-09-08). */
inline constexpr uint64_t kWORLD_RVA          = 0x10967B98u; /* UWorld, double-deref */
inline constexpr uint64_t kWORLD_STATIC       = 0x10F1C958u; /* GWORLD */
inline constexpr uint64_t kGAME_WORLD_ALT     = 0x10967B98u; /* alternate UWorld */
inline constexpr uint64_t kG_OBJECTS          = 0x10D853F0u; /* GUObjectArray */
inline constexpr uint64_t kFNAME_POOL_RVA     = 0x10AB5DC0u;
inline constexpr uint64_t kFNAME_KEYS_RVA     = 0x109533B4u;
/* Game chain offsets (all +offset). */
inline constexpr uint32_t kGI_LOCAL_PLAYERS        = 0x00000138u;
inline constexpr uint32_t kGI_PLAYER_CONTROLLER    = 0x000000A0u; /* GI +0x0A0 */
inline constexpr uint32_t kPC_CONTROL_ROTATION     = 0x00000438u; /* FRotator */
inline constexpr uint32_t kPC_PLAYER_CAMERA_MGR    = 0x000004E0u; /* live-pinned */
inline constexpr uint32_t kPCM_CAMERA_LOCATION     = 0x00000CA0u; /* FVector doubles */
inline constexpr uint32_t kPCM_CAMERA_ROTATION     = 0x00000CC0u; /* FRotator floats */
inline constexpr uint32_t kPCM_CAMERA_FOV          = 0x00000CE8u; /* vertical degrees */
/* Actor / scene-component offsets (96B double FTransform). */
inline constexpr uint32_t kPERSISTENT_LEVEL        = 0x00000120u; /* UWorld +0x120 */
inline constexpr uint32_t kLEVEL_Actors            = 0x00000098u; /* FLevel +0x98 TArray */
inline constexpr uint32_t kACTOR_ROOT_COMPONENT    = 0x00000250u;
inline constexpr uint32_t kOWNING_GAME_INSTANCE    = 0x00000190u;
inline constexpr uint32_t kCOMPONENT_TO_WORLD      = 0x000002E0u; /* FTransform, 96B */
/* Struct sizes / caps. */
inline constexpr uint32_t kTRANSFORM_SIZE          = 0x60; /* 96B double FTransform */
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
inline constexpr uint32_t kSceneComponent_ComponentToWorld    = kCOMPONENT_TO_WORLD;
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
