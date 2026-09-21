#pragma once

/*
 * Hardened ESP Analysis Module
 * 
 * Implements anti-detection techniques for the ESP pipeline:
 * - Randomized read delays
 * - Distance-based entity filtering
 * - FOV cone checks
 * - Visibility validation
 * - Entity caching to reduce read frequency
 * - Bounded rendering to avoid "impossible knowledge" patterns
 */

#ifdef MODULE_ESP

#include <cstdint>
#include <cstddef>
#include "esp_analysis.h"

extern "C" {
bool HvAnalysisStart(uint32_t pid);
bool HvScanProcess(uint32_t pid, const void* pattern, const void* mask, size_t pattern_len, void** out_results, size_t* out_count);
bool HvReadGuestMemory(uint32_t pid, uint64_t address, void* buffer, size_t size);
bool HvWriteSharedMemory(const void* data, size_t size);
}


namespace hypersinv::esp::hardened {

/* Configuration for hardened ESP */
struct HardenedConfig {
    float max_render_distance;    /* Maximum distance to render entities (meters) */
    float fov_cone_degrees;       /* FOV cone for entity validation (degrees) */
    uint32_t min_read_delay_ms;   /* Minimum delay between memory reads (ms) */
    uint32_t max_read_delay_ms;   /* Maximum delay between memory reads (ms) */
    uint32_t cache_ttl_ms;        /* Entity cache time-to-live (ms) */
    bool require_visibility;      /* Only render entities with line-of-sight */
    bool randomize_timing;        /* Add random delays to defeat timing analysis */
};

/* Initialize hardened ESP module */
bool HardenedEspInitialize(const HardenedConfig* config, const EspConfig* baseConfig);

/* Update with hardening applied */
bool HardenedEspUpdate();

/* Get filtered entities */
void HardenedEspGetEntities(Entity* out_buffer, size_t max_count, size_t* out_count);

/* Shutdown */
void HardenedEspShutdown();

/* Apply random delay between reads */
void ApplyRandomReadDelay();

/* Check if entity should be rendered based on distance */
bool ShouldRenderEntity(float worldX, float worldY, float worldZ, const float* cameraPos);

/* Check if entity is within FOV cone */
bool IsEntityInFOV(float worldX, float worldY, float worldZ, const float* cameraPos, const float* cameraForward, float fovDegrees);

/* Check if entity has line-of-sight */
bool HasLineOfSight(uint32_t pid, uint64_t cameraAddress, uint64_t entityAddress);

/* Calculate distance between two points */
float CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2);

/* Calculate angle between two vectors */
float CalculateAngle(float dx, float dy, float dz);

/* Check if angle is within FOV */
bool CheckFOV(float angle, float fovDegrees);

} // namespace hypersinv::esp::hardened

#endif /* MODULE_ESP */
