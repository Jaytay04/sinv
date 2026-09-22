#ifndef HYPERSINV_ESP_ANALYSIS_H
#define HYPERSINV_ESP_ANALYSIS_H

#ifdef MODULE_ESP

#include <cstddef>
#include <cstdint>
#include <cwchar>
/*
 * ESP Analysis Module
 * 
 * A removable analysis layer that sits between the hypervisor core and the
 * overlay rendering pipeline. This module implements game-specific memory
 * reading, entity extraction, camera processing, and world-to-screen
 * projection for CL-137205 (Arc Raiders).
 * 
 * Architecture:
 *   Hypervisor Core (generic) <---> ESP Analysis Module (specific) <---> Overlay (rendering)
 *         [VMX root]                    [Usermode plugin]
 *         [Physical memory]             [Application structs]
 *         [Build-pinned profiles]       [Signatures]
 * 
 * All game-specific code lives in arc_raiders/. The module can be deleted
 * without breaking the hypervisor build.
 */

namespace hypersinv::esp {

/* Configuration passed to initialize the ESP module */
struct EspConfig {
    uint32_t target_pid;
    uint64_t uworld_signature;
    uint64_t guobjectarray_rva;
};

/* Entity representation compatible with overlay pipeline */
struct Entity {
    float screen_x;
    float screen_y;
    float width;
    float height;
    float world_x;
    float world_y;
    float world_z;
    uint32_t coordinate_space;
    wchar_t name[32];
    uint32_t color;
    uint32_t is_visible;
};

/*
 * Initialize the ESP analysis module.
 * 
 * Establishes an authenticated analysis session with the hypervisor core
 * for the target process. Scans for UWorld using the provided signature
 * and prepares the global object array traversal.
 * 
 * Returns true on success, false on failure.
 */
bool EspInitialize(const EspConfig* config);

/*
 * Update entity data from the target process.
 * 
 * Reads current game state through the hypervisor, extracts entity
 * positions, applies camera transforms, and updates the internal cache.
 * 
 * Returns true if update succeeded, false if session is invalid.
 */
bool EspUpdate();

/*
 * Set the client-area projection viewport used for world-to-screen
 * transforms. Called when the target window size changes.
 */
void EspViewportSize(float vw, float vh);

/*
 * Retrieve extracted entities in overlay-compatible format.
 * 
 * Copies up to max_count entities into out_buffer and sets out_count
 * to the number of entities written. Entities are already projected
 * to screen space when a valid camera matrix is available.
 */
void EspGetEntities(Entity* out_buffer, size_t max_count, size_t* out_count);

/*
 * Shut down the ESP analysis module.
 * 
 * Terminates the authenticated analysis session, clears all cached
 * state, and releases hypervisor resources.
 */
void EspShutdown();

} // namespace hypersinv::esp

#endif /* MODULE_ESP */
#endif /* HYPERSINV_ESP_ANALYSIS_H */
