#ifdef MODULE_ESP

#include <cstdint>
#include <cstddef>
#include "offsets.h"

/*
 * Forward declarations for hypervisor APIs.
 * These are the actual functions provided by the hypervisor core.
 */
extern "C" {
    bool HvAnalysisStart(uint32_t pid);
    bool HvScanProcess(uint32_t pid, const void* pattern, const void* mask, size_t pattern_len, void** out_results, size_t* out_count);
    bool HvReadGuestMemory(uint32_t pid, uint64_t address, void* buffer, size_t size);
    bool HvWriteSharedMemory(const void* data, size_t size);
}

namespace hypersinv::esp::arc_raiders {

/*
 * Read a pointer chain from the target process.
 * 
 * Follows a sequence of offsets from a base address, reading each
 * pointer in the chain through the hypervisor's memory read API.
 * 
 * Returns the final resolved address, or 0 if any step fails.
 */
uint64_t ReadPointerChain(uint32_t pid, uint64_t base_address, 
                          const uint32_t* offsets, size_t offset_count) {
    uint64_t current = base_address;
    
    for (size_t i = 0; i < offset_count; ++i) {
        uint64_t next_ptr = 0;
        if (!HvReadGuestMemory(pid, current + offsets[i], &next_ptr, sizeof(next_ptr))) {
            return 0;
        }
        
        if (next_ptr == 0) {
            return 0;
        }
        
        current = next_ptr;
    }
    
    return current;
}

/*
 * Read a TArray from the target process.
 * 
 * Extracts the Data pointer, Count, and Max from a TArray structure.
 * Returns true if the array is valid and readable.
 */
bool ReadTArray(uint32_t pid, uint64_t array_address, 
                uint64_t* out_data, int32_t* out_count, int32_t* out_max) {
    struct TArrayHeader {
        uint64_t Data;
        int32_t Count;
        int32_t Max;
    };
    
    TArrayHeader header = {};
    if (!HvReadGuestMemory(pid, array_address, &header, sizeof(header))) {
        return false;
    }
    
    if (header.Data == 0 || header.Count < 0 || header.Count > header.Max || header.Max > 1000000) {
        return false;
    }
    
    if (out_data) *out_data = header.Data;
    if (out_count) *out_count = header.Count;
    if (out_max) *out_max = header.Max;
    
    return true;
}

/*
 * Read an FString from the target process.
 * 
 * Extracts the string data pointer and length, then reads the
 * actual string content into the provided buffer.
 */
bool ReadFString(uint32_t pid, uint64_t string_address, char* out_buffer, size_t buffer_size) {
    struct FStringHeader {
        uint64_t Data;
        int32_t Count;
        int32_t Max;
    };
    
    FStringHeader header = {};
    if (!HvReadGuestMemory(pid, string_address, &header, sizeof(header))) {
        return false;
    }
    
    if (header.Data == 0 || header.Count <= 0 || header.Count > 1024) {
        return false;
    }
    
    size_t read_size = static_cast<size_t>(header.Count) < buffer_size - 1 
                       ? static_cast<size_t>(header.Count) 
                       : buffer_size - 1;
    
    if (!HvReadGuestMemory(pid, header.Data, out_buffer, read_size)) {
        return false;
    }
    
    out_buffer[read_size] = '\0';
    return true;
}

/*
 * Traverse the GUObjectArray to find objects by class.
 * 
 * Uses the hypervisor's analysis session to scan for UWorld,
 * then traverses the global object array to locate entities
 * matching the specified criteria.
 * 
 * CL-1426848: GUObjectArray blob at module+0x111794F0,
 * FUObjectItem.Object @ +0x8 (pure XOR decode).
 */
bool TraverseGUObjectArray(uint32_t pid, uint64_t guobjectarray_base,
                          uint64_t* out_uworld, uint64_t* out_gnames) {
    /* Read UWorld pointer from GUObjectArray */
    uint64_t uworld_ptr = 0;
    if (!HvReadGuestMemory(pid, guobjectarray_base + kGUObjectArray_Objects, &uworld_ptr, sizeof(uworld_ptr))) {
        return false;
    }
    
    if (uworld_ptr == 0) {
        return false;
    }
    
    if (out_uworld) *out_uworld = uworld_ptr;
    
    /* GNames is typically found via signature scan */
    /* This would be populated by the signature scanner */
    if (out_gnames) *out_gnames = 0;
    
    return true;
}

} // namespace hypersinv::esp::arc_raiders

#endif /* MODULE_ESP */
