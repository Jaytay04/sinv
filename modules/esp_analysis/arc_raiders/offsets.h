#ifndef HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H
#define HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H

#ifdef MODULE_ESP

#include <cstdint>

/*
 * Arc Raiders (CL-1372005) structure offsets.
 * 
 * All offsets are relative to the base of the containing struct.
 * These are derived from the reference documentation and must be
 * regenerated if the game updates to a new build.
 * 
 * Source: https://www.unknowncheats.me/forum/arc-raiders/590414-arc-raiders-structs-offsets-200.html
 */

namespace hypersinv::esp::arc_raiders {

/* UWorld offsets */
inline constexpr uint32_t kUWorld_PersistentLevel = 0x30;
inline constexpr uint32_t kUWorld_OwningGameInstance = 0x1A0;
inline constexpr uint32_t kUWorld_AuthoritativeGameInstance = 0x1B8;

/* GameInstance offsets */
inline constexpr uint32_t kGameInstance_LocalPlayers = 0x38;

/* LocalPlayer offsets */
inline constexpr uint32_t kLocalPlayer_PlayerController = 0x38;
inline constexpr uint32_t kLocalPlayer_ViewportClient = 0x58;

/* PlayerController offsets */
inline constexpr uint32_t kPlayerController_AcknowledgedPawn = 0x440;
inline constexpr uint32_t kPlayerController_ControlRotation = 0x450;

/* ViewportClient / World offsets */
inline constexpr uint32_t kViewportClient_World = 0x80;

/* Level offsets */
inline constexpr uint32_t kLevel_Actors = 0x98;
inline constexpr uint32_t kLevel_StreamingLevels = 0x140;

/* Actor offsets */
inline constexpr uint32_t kActor_RootComponent = 0x150;
inline constexpr uint32_t kActor_Instigator = 0x188;
inline constexpr uint32_t kActor_CustomTimeDilation = 0x9C;
inline constexpr uint32_t kActor_UniqueID = 0x18;

/* SceneComponent offsets */
inline constexpr uint32_t kSceneComponent_ComponentToWorld = 0x170;
inline constexpr uint32_t kSceneComponent_RelativeLocation = 0x134;
inline constexpr uint32_t kSceneComponent_RelativeRotation = 0x140;
inline constexpr uint32_t kSceneComponent_RelativeScale3D = 0x150;

/* ComponentToWorld (FTransform) */
inline constexpr uint32_t kFTransform_Translation = 0x00;  // FVector
inline constexpr uint32_t kFTransform_Rotation = 0x10;     // FQuat
inline constexpr uint32_t kFTransform_Scale3D = 0x20;      // FVector

/* Pawn offsets */
inline constexpr uint32_t kPawn_PlayerState = 0x3D0;

/* PlayerState offsets */
inline constexpr uint32_t kPlayerState_PlayerName = 0x3C8;
inline constexpr uint32_t kPlayerState_Score = 0x3A8;

/* Character offsets */
inline constexpr uint32_t kCharacter_Mesh = 0x350;

/* SkeletalMeshComponent offsets */
inline constexpr uint32_t kSkeletalMeshComponent_CachedBoneSpaceTransforms = 0x9E0;
inline constexpr uint32_t kSkeletalMeshComponent_BoneArray = 0x9E0;

/* TArray base */
inline constexpr uint32_t kTArray_Data = 0x00;
inline constexpr uint32_t kTArray_Count = 0x08;
inline constexpr uint32_t kTArray_Max = 0x0C;

/* FString base */
inline constexpr uint32_t kFString_Data = 0x00;
inline constexpr uint32_t kFString_Count = 0x10;
inline constexpr uint32_t kFString_Max = 0x14;

/* FName (encrypted) */
inline constexpr uint32_t kFName_ComparisonIndex = 0x00;
inline constexpr uint32_t kFName_Number = 0x04;

/* GNames */
inline constexpr uint32_t kGNames_Elements = 0x00;

/* APlayerCameraManager */
inline constexpr uint32_t kCameraManager_CameraCache = 0x19A0;
inline constexpr uint32_t kCameraManager_POV_Location = 0x10;
inline constexpr uint32_t kCameraManager_POV_Rotation = 0x1C;
inline constexpr uint32_t kCameraManager_POV_FOV = 0x28;

/* FMinimalViewInfo */
inline constexpr uint32_t kMinimalViewInfo_Location = 0x00;
inline constexpr uint32_t kMinimalViewInfo_Rotation = 0x0C;
inline constexpr uint32_t kMinimalViewInfo_FOV = 0x18;

/* FVector size */
inline constexpr uint32_t kFVector_Size = 0x0C;

/* FQuat size */
inline constexpr uint32_t kFQuat_Size = 0x10;

/* FRotator size */
inline constexpr uint32_t kFRotator_Size = 0x0C;

/* FTransform size */
inline constexpr uint32_t kFTransform_Size = 0x30;

/* FMatrix size */
inline constexpr uint32_t kFMatrix_Size = 0x40;

/* GUObjectArray offsets */
inline constexpr uint32_t kGUObjectArray_Objects = 0x10;
inline constexpr uint32_t kFUObjectItem_Object = 0x00;
inline constexpr uint32_t kFUObjectItem_Flags = 0x08;
inline constexpr uint32_t kFUObjectItem_InternalIndex = 0x0C;

/* UObject offsets */
inline constexpr uint32_t kUObject_VTable = 0x00;
inline constexpr uint32_t kUObject_Flags = 0x08;
inline constexpr uint32_t kUObject_InternalIndex = 0x0C;
inline constexpr uint32_t kUObject_Class = 0x10;
inline constexpr uint32_t kUObject_Name = 0x18;
inline constexpr uint32_t kUObject_Outer = 0x20;

/* UStruct offsets */
inline constexpr uint32_t kUStruct_SuperStruct = 0x40;

/* UClass offsets */
inline constexpr uint32_t kUClass_ClassFlags = 0x208;
inline constexpr uint32_t kUClass_ClassName = 0x210;

/* Entity classification flags */
inline constexpr uint32_t kCLASSFLAG_IsPlayer = 0x00000001;
inline constexpr uint32_t kCLASSFLAG_IsAI = 0x00000002;
inline constexpr uint32_t kCLASSFLAG_IsVehicle = 0x00000004;
inline constexpr uint32_t kCLASSFLAG_IsLoot = 0x00000008;
inline constexpr uint32_t kCLASSFLAG_IsExtraction = 0x00000010;

/* Default entity colors (0xAARRGGBB) */
inline constexpr uint32_t kCOLOR_Player = 0xFF00FF00;   /* Green */
inline constexpr uint32_t kCOLOR_AI = 0xFFFF0000;       /* Red */
inline constexpr uint32_t kCOLOR_Vehicle = 0xFF0080FF;  /* Orange */
inline constexpr uint32_t kCOLOR_Loot = 0xFFFFFF00;     /* Yellow */
inline constexpr uint32_t kCOLOR_Extraction = 0xFF00FFFF; /* Cyan */

} // namespace hypersinv::esp::arc_raiders

#endif /* MODULE_ESP */
#endif /* HYPERSINV_ESP_ARC_RAIDERS_OFFSETS_H */
