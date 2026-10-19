#ifdef MODULE_ESP

#include "hardened_esp.h"
#include "arc_raiders/offsets.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

/* Windows API for GetTickCount64() */
#include <windows.h>

/* Forward declarations for hypervisor APIs */
extern "C" {
    bool HvReadGuestMemory(uint32_t pid, uint64_t address, void* buffer, size_t size);
}

namespace hypersinv::esp::hardened {

/* Module state */
static bool g_initialized = false;
static HardenedConfig g_config = {};
static EspConfig g_baseConfig = {};
static float g_cameraPos[3] = {0};
static float g_cameraForward[3] = {0};
static uint64_t g_lastReadTick = 0;

/*
 * Initialize hardened ESP module
 */
bool HardenedEspInitialize(const HardenedConfig* config, const EspConfig* baseConfig) {
    if (!config || !baseConfig) {
        return false;
    }
    
    /* Copy configuration */
    g_config = *config;
    g_baseConfig = *baseConfig;
    
    /* Initialize base ESP module */
    if (!EspInitialize(baseConfig)) {
        return false;
    }
    
    /* Seed random number generator */
    srand(static_cast<unsigned int>(std::chrono::system_clock::now().time_since_epoch().count()));
    
    g_initialized = true;
    return true;
}

/*
 * Apply random delay between memory reads
 * 
 * Adds randomized timing to defeat behavioral analysis
 * that looks for consistent read patterns.
 */
void ApplyRandomReadDelay() {
    if (!g_config.randomize_timing) {
        return;
    }
    
    uint32_t delay = g_config.min_read_delay_ms + 
                     (rand() % (g_config.max_read_delay_ms - g_config.min_read_delay_ms + 1));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

/*
 * Calculate distance between two 3D points
 */
float CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float dz = z2 - z1;
    
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

/*
 * Calculate angle from direction vector
 */
float CalculateAngle(float dx, float dy, float dz) {
    return std::atan2(dx, dz);
}

/*
 * Check if angle is within FOV cone
 */
bool CheckFOV(float angle, float fovDegrees) {
    float fovRad = fovDegrees * 3.14159265f / 180.0f;
    return std::abs(angle) <= fovRad / 2.0f;
}

/*
 * Check if entity should be rendered based on distance
 */
bool ShouldRenderEntity(float worldX, float worldY, float worldZ, const float* cameraPos) {
    if (!cameraPos) return false;
    
    float distance = CalculateDistance(
        worldX, worldY, worldZ,
        cameraPos[0], cameraPos[1], cameraPos[2]
    );
    
    return distance <= g_config.max_render_distance;
}

/*
 * Check if entity is within FOV cone
 */
bool IsEntityInFOV(float worldX, float worldY, float worldZ, 
                   const float* cameraPos, const float* cameraForward, 
                   float fovDegrees) {
    float dx = worldX - cameraPos[0];
    float dy = worldY - cameraPos[1];
    float dz = worldZ - cameraPos[2];
    
    /* Direction vector from camera to entity. */
    float dirLen = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (dirLen == 0.0f) return false;
    dx /= dirLen; dy /= dirLen; dz /= dirLen;
    
    /* Dot product with camera_forward. */
    float dot = dx * cameraForward[0] + dy * cameraForward[1] + dz * cameraForward[2];
    /* Clamp to [-1, 1] for acos. */
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    
    return CheckFOV(static_cast<float>(std::acos(dot)), fovDegrees);
}

/*
 * Check if entity has line-of-sight
 */
bool HasLineOfSight(uint32_t pid, uint64_t cameraAddress, uint64_t entityAddress) {
    /* Would need entity address for proper LOS check */
    /* For now, pass through */
    (void)pid;
    (void)cameraAddress;
    (void)entityAddress;
    return true;
}

/*
 * Update hardened ESP module
 */
bool HardenedEspUpdate() {
    /* Apply random delay before reading */
    ApplyRandomReadDelay();

    /* Read camera position via CL-1426848 chain:
     * Resolve player_controller from uworld_signature via
     * kUWORLD_GAME_INSTANCE -> kGI_LOCAL_PLAYERS -> kLP_PLAYER_CONTROLLER,
     * then view_target = player_controller + kVIEW_TARGET (0x4C0),
     * POV at view_target + 0x10 + 0x08/0x28/0x48. */
    if (g_baseConfig.target_pid != 0) {
        uint64_t player_controller = 0;
        if (g_baseConfig.uworld_signature != 0) {
            uint64_t gi = 0;
            if (HvReadGuestMemory(g_baseConfig.target_pid,
                                  g_baseConfig.uworld_signature + arc_raiders::kUWORLD_GAME_INSTANCE,
                                  &gi, sizeof(gi))) {
                if (gi != 0) {
                    uint64_t lp = 0;
                    if (HvReadGuestMemory(g_baseConfig.target_pid,
                                          gi + arc_raiders::kGI_LOCAL_PLAYERS,
                                          &lp, sizeof(lp))) {
                        if (lp != 0) {
                            HvReadGuestMemory(g_baseConfig.target_pid,
                                              lp + arc_raiders::kLP_PLAYER_CONTROLLER,
                                              &player_controller, sizeof(player_controller));
                        }
                    }
                }
            }
        }
        
        if (player_controller != 0) {
            uint64_t view_target = 0;
            if (HvReadGuestMemory(g_baseConfig.target_pid,
                                  player_controller + arc_raiders::kVIEW_TARGET,
                                  &view_target, sizeof(view_target))) {
                /* Location: view_target + 0x10 + 0x08 (FVector3d). */
                double cameraLocation[3] = {};
                if (HvReadGuestMemory(g_baseConfig.target_pid,
                                      view_target + 0x10 + 0x08,
                                      cameraLocation, sizeof(cameraLocation))) {
                    g_cameraPos[0] = static_cast<float>(cameraLocation[0]);
                    g_cameraPos[1] = static_cast<float>(cameraLocation[1]);
                    g_cameraPos[2] = static_cast<float>(cameraLocation[2]);
                }
                
                /* Rotation: view_target + 0x10 + 0x28 (FRotator). */
                float cameraRotation[3] = {0};
                if (HvReadGuestMemory(g_baseConfig.target_pid,
                                      view_target + 0x10 + 0x28,
                                      cameraRotation, sizeof(cameraRotation))) {
                    float pitch = cameraRotation[0] * 3.14159265f / 180.0f;
                    float yaw = cameraRotation[1] * 3.14159265f / 180.0f;
                    
                    g_cameraForward[0] = std::cos(pitch) * std::sin(yaw);
                    g_cameraForward[1] = std::sin(pitch);
                    g_cameraForward[2] = std::cos(pitch) * std::cos(yaw);
                }
            }
        }
    }
    
    /* Update base ESP module */
    bool success = EspUpdate();
    
    if (!success) {
        return false;
    }
    
    /* Apply additional filtering to entities */
    /* This would filter out entities that fail distance/FOV/visibility checks */
    
    g_lastReadTick = GetTickCount64();
    
    return true;
}
/*
 * Get filtered entities
 */
void HardenedEspGetEntities(Entity* out_buffer, size_t max_count, size_t* out_count) {
    if (!out_count) return;
    
    /* Get entities from base module */
    EspGetEntities(out_buffer, max_count, out_count);
    
    /* Apply hardening filters */
    size_t filteredCount = 0;
    for (size_t i = 0; i < *out_count && filteredCount < max_count; ++i) {
        Entity* entity = &out_buffer[i];
        
        /* Skip invisible entities */
        if (entity->is_visible == 0) {
            continue;
        }
        
        /* Check distance filter */
        if (!ShouldRenderEntity(entity->world_x, entity->world_y, entity->world_z, g_cameraPos)) {
            continue;
        }
        
        /* Check FOV filter */
        if (!IsEntityInFOV(entity->world_x, entity->world_y, entity->world_z,
                          g_cameraPos, g_cameraForward, g_config.fov_cone_degrees)) {
            continue;
        }
        
        /* Check line-of-sight if required */
        if (g_config.require_visibility) {
            /* Would need entity address for proper LOS check */
            /* For now, pass through */
        }
        
        /* Entity passed all filters */
        if (filteredCount != i) {
            out_buffer[filteredCount] = *entity;
        }
        filteredCount++;
    }
    
    *out_count = filteredCount;
}

/*
 * Shutdown hardened ESP module
 */
void HardenedEspShutdown() {
    if (!g_initialized) {
        return;
    }
    
    /* Shutdown base module */
    EspShutdown();
    
    /* Clear state */
    g_initialized = false;
    g_cameraPos[0] = g_cameraPos[1] = g_cameraPos[2] = 0;
    g_cameraForward[0] = g_cameraForward[1] = g_cameraForward[2] = 0;
    g_lastReadTick = 0;
}

} // namespace hypersinv::esp::hardened

#endif /* MODULE_ESP */
