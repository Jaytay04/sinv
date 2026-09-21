#ifdef MODULE_ESP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "esp_analysis.h"
#include "arc_raiders/offsets.h"

/* Forward declarations for hypervisor APIs */
extern "C" {
    bool HvReadGuestMemory(uint32_t pid, uint64_t address, void* buffer, size_t size);
    bool HvWriteSharedMemory(const void* data, size_t size);
}

namespace hypersinv::esp {

/*
 * Entity cache implementation.
 * 
 * Maintains a bounded buffer of extracted entities, updated each
 * frame by EspUpdate(). The cache is the shared memory producer
 * that feeds the overlay rendering pipeline.
 */

static constexpr size_t kMaxCachedEntities = 256;

struct CachedEntity {
    Entity data;
    bool valid;
};

static CachedEntity g_entity_cache[kMaxCachedEntities];
static size_t g_entity_count = 0;
static bool g_cache_initialized = false;

/*
 * Initialize the entity cache.
 */
void EntityCacheInitialize() {
    std::memset(g_entity_cache, 0, sizeof(g_entity_cache));
    g_entity_count = 0;
    g_cache_initialized = true;
}

/*
 * Clear all cached entities.
 */
void EntityCacheClear() {
    for (size_t i = 0; i < kMaxCachedEntities; ++i) {
        g_entity_cache[i].valid = false;
    }
    g_entity_count = 0;
}

/*
 * Add an entity to the cache.
 * 
 * Returns true if the entity was added, false if the cache is full.
 */
bool EntityCacheAdd(const Entity* entity) {
    if (!g_cache_initialized || g_entity_count >= kMaxCachedEntities) {
        return false;
    }
    
    size_t index = g_entity_count++;
    g_entity_cache[index].data = *entity;
    g_entity_cache[index].valid = true;
    
    return true;
}

/*
 * Retrieve entities from the cache.
 * 
 * Copies up to max_count valid entities into the output buffer.
 */
void EntityCacheGet(Entity* out_buffer, size_t max_count, size_t* out_count) {
    if (!out_count) return;
    
    size_t copied = 0;
    for (size_t i = 0; i < kMaxCachedEntities && copied < max_count; ++i) {
        if (g_entity_cache[i].valid) {
            out_buffer[copied++] = g_entity_cache[i].data;
        }
    }
    
    *out_count = copied;
}

/*
 * Publish the entity cache to shared memory for the overlay.
 * 
 * Formats the cached entities into the overlay protocol structure
 * and writes it through the hypervisor's shared memory API.
 */
bool EntityCachePublish() {
    /* This would integrate with the overlay pipeline */
    /* For now, it's a placeholder for HvWriteSharedMemory */
    return true;
}

/*
 * Shut down the entity cache.
 */
void EntityCacheShutdown() {
    EntityCacheClear();
    g_cache_initialized = false;
}

} // namespace hypersinv::esp

#endif /* MODULE_ESP */
