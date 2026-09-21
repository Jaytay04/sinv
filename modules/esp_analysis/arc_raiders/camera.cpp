#ifdef MODULE_ESP

#include <cstdint>
#include <cmath>
#include <cstring>
#include "offsets.h"

/* Forward declarations for hypervisor APIs */
extern "C" {
    bool HvReadGuestMemory(uint32_t pid, uint64_t address, void* buffer, size_t size);
}

namespace hypersinv::esp::arc_raiders {

/*
 * FVector structure matching Unreal Engine layout
 */
struct FVector {
    float X, Y, Z;
};

/*
 * FQuat structure for rotation
 */
struct FQuat {
    float X, Y, Z, W;
};

/*
 * FRotator structure
 */
struct FRotator {
    float Pitch, Yaw, Roll;
};

/*
 * FTransform structure
 */
struct FTransform {
    FQuat Rotation;
    FVector Translation;
    float Padding;
    FVector Scale3D;
    float Padding2;
};

/*
 * FMinimalViewInfo structure
 */
struct FMinimalViewInfo {
    FVector Location;
    FRotator Rotation;
    float FOV;
};

/*
 * FMatrix structure (4x4 row-major)
 */
struct FMatrix {
    float M[4][4];
};

/*
 * Read camera information from the target process.
 * 
 * Extracts the camera location, rotation, and FOV from the
 * PlayerCameraManager through the hypervisor's memory read API.
 */
bool ReadCamera(uint32_t pid, uint64_t camera_manager_address,
                FVector* out_location, FRotator* out_rotation,
                float* out_fov) {
    if (camera_manager_address == 0) {
        return false;
    }
    
    /* Read CameraCache */
    FMinimalViewInfo view_info = {};
    if (!HvReadGuestMemory(pid, 
                          camera_manager_address + kCameraManager_CameraCache,
                          &view_info, sizeof(view_info))) {
        return false;
    }
    
    if (out_location) {
        *out_location = view_info.Location;
    }
    
    if (out_rotation) {
        *out_rotation = view_info.Rotation;
    }
    
    if (out_fov) {
        *out_fov = view_info.FOV;
    }
    
    return true;
}

/*
 * Convert degrees to radians
 */
static float DegToRad(float degrees) {
    return degrees * static_cast<float>(M_PI) / 180.0f;
}

/*
 * Construct a view-projection matrix from camera parameters.
 * 
 * Creates a 4x4 row-major matrix that transforms world coordinates
 * to screen space. The matrix combines view (camera transform)
 * and projection (FOV-based perspective) transformations.
 */
void BuildViewProjectionMatrix(const FVector& location, const FRotator& rotation,
                               float fov, float viewport_width, float viewport_height,
                               float* out_matrix) {
    /* Clear the output matrix */
    std::memset(out_matrix, 0, 16 * sizeof(float));
    
    /* Convert rotation to radians */
    float pitch = DegToRad(rotation.Pitch);
    float yaw = DegToRad(rotation.Yaw);
    float roll = DegToRad(rotation.Roll);
    
    /* Calculate trigonometric values */
    float sp = std::sin(pitch);
    float cp = std::cos(pitch);
    float sy = std::sin(yaw);
    float cy = std::cos(yaw);
    float sr = std::sin(roll);
    float cr = std::cos(roll);
    
    /* Build rotation matrix */
    float rotation_matrix[3][3] = {
        {cp * cy, cp * sy, sp},
        {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}
    };
    
    /* Build view matrix (inverse of camera transform) */
    float view_matrix[4][4] = {
        {rotation_matrix[0][0], rotation_matrix[1][0], rotation_matrix[2][0], 
         -(rotation_matrix[0][0] * location.X + rotation_matrix[1][0] * location.Y + rotation_matrix[2][0] * location.Z)},
        {rotation_matrix[0][1], rotation_matrix[1][1], rotation_matrix[2][1],
         -(rotation_matrix[0][1] * location.X + rotation_matrix[1][1] * location.Y + rotation_matrix[2][1] * location.Z)},
        {rotation_matrix[0][2], rotation_matrix[1][2], rotation_matrix[2][2],
         -(rotation_matrix[0][2] * location.X + rotation_matrix[1][2] * location.Y + rotation_matrix[2][2] * location.Z)},
        {0.0f, 0.0f, 0.0f, 1.0f}
    };
    
    /* Build projection matrix */
    float fov_rad = DegToRad(fov);
    float aspect = viewport_width / viewport_height;
    float tan_half_fov = std::tan(fov_rad / 2.0f);
    
    float projection_matrix[4][4] = {
        {1.0f / (aspect * tan_half_fov), 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f / tan_half_fov, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    
    /* Multiply view * projection to get view-projection matrix */
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out_matrix[i * 4 + j] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                out_matrix[i * 4 + j] += view_matrix[i][k] * projection_matrix[k][j];
            }
        }
    }
}

/*
 * Extract camera matrix from PlayerController.
 * 
 * Reads the ControlRotation from the PlayerController and constructs
 * a view-projection matrix suitable for world-to-screen projection.
 */
bool ExtractCameraMatrix(uint32_t pid, uint64_t player_controller_address,
                        float viewport_width, float viewport_height,
                        float* out_view_projection) {
    if (player_controller_address == 0) {
        return false;
    }
    
    FRotator control_rotation = {};
    if (!HvReadGuestMemory(pid,
                          player_controller_address + kPlayerController_ControlRotation,
                          &control_rotation, sizeof(control_rotation))) {
        return false;
    }
    
    /* Get camera location from acknowledged pawn */
    uint64_t pawn_address = 0;
    if (!HvReadGuestMemory(pid,
                          player_controller_address + kPlayerController_AcknowledgedPawn,
                          &pawn_address, sizeof(pawn_address))) {
        return false;
    }
    
    FVector camera_location = {0.0f, 0.0f, 0.0f};
    float fov = 90.0f; /* Default FOV */
    
    if (pawn_address != 0) {
        /* Try to read camera location from pawn's root component */
        uint64_t root_component = 0;
        if (HvReadGuestMemory(pid,
                             pawn_address + kActor_RootComponent,
                             &root_component, sizeof(root_component)) &&
            root_component != 0) {
            FTransform transform = {};
            if (HvReadGuestMemory(pid,
                                 root_component + kSceneComponent_ComponentToWorld,
                                 &transform, sizeof(transform))) {
                camera_location = transform.Translation;
            }
        }
    }
    
    /* Build the view-projection matrix */
    BuildViewProjectionMatrix(camera_location, control_rotation, fov,
                             viewport_width, viewport_height,
                             out_view_projection);
    
    return true;
}

} // namespace hypersinv::esp::arc_raiders

#endif /* MODULE_ESP */
