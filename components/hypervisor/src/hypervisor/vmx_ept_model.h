#ifndef HYPERSINV_VMX_EPT_MODEL_H
#define HYPERSINV_VMX_EPT_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_STATUS {
    VMX_EPT_STATUS_OK = 0,
    VMX_EPT_STATUS_INVALID = -1,
    VMX_EPT_STATUS_UNSUPPORTED = -2
} VMX_EPT_STATUS;

#define VMX_EPT_CAP_EXECUTE_ONLY             (UINT32_C(1) << 0)
#define VMX_EPT_CAP_WALK_4                    (UINT32_C(1) << 1)
#define VMX_EPT_CAP_EPTP_UC                   (UINT32_C(1) << 2)
#define VMX_EPT_CAP_EPTP_WB                   (UINT32_C(1) << 3)
#define VMX_EPT_CAP_PAGE_2MB                  (UINT32_C(1) << 4)
#define VMX_EPT_CAP_PAGE_1GB                  (UINT32_C(1) << 5)
#define VMX_EPT_CAP_INVEPT                    (UINT32_C(1) << 6)
#define VMX_EPT_CAP_ACCESSED_DIRTY            (UINT32_C(1) << 7)
#define VMX_EPT_CAP_INVEPT_SINGLE_CONTEXT     (UINT32_C(1) << 8)
#define VMX_EPT_CAP_INVEPT_ALL_CONTEXTS       (UINT32_C(1) << 9)

#define VMX_EPT_CAP_PROJECT_REQUIRED (VMX_EPT_CAP_WALK_4 | \
    VMX_EPT_CAP_EPTP_WB | VMX_EPT_CAP_PAGE_2MB | VMX_EPT_CAP_INVEPT | \
    VMX_EPT_CAP_INVEPT_SINGLE_CONTEXT | VMX_EPT_CAP_INVEPT_ALL_CONTEXTS)

#define VMX_EPT_MEMORY_TYPE_UC UINT8_C(0)
#define VMX_EPT_MEMORY_TYPE_WC UINT8_C(1)
#define VMX_EPT_MEMORY_TYPE_WT UINT8_C(4)
#define VMX_EPT_MEMORY_TYPE_WP UINT8_C(5)
#define VMX_EPT_MEMORY_TYPE_WB UINT8_C(6)

#define VMX_EPT_PERMISSION_READ    UINT8_C(1)
#define VMX_EPT_PERMISSION_WRITE   UINT8_C(2)
#define VMX_EPT_PERMISSION_EXECUTE UINT8_C(4)
#define VMX_EPT_PERMISSION_MASK    UINT8_C(7)

#define VMX_EPT_ENTRY_FLAG_IGNORE_PAT UINT8_C(1)
#define VMX_EPT_ENTRY_FLAG_AD_ENABLED UINT8_C(2)
#define VMX_EPT_ENTRY_FLAG_ACCESSED   UINT8_C(4)
#define VMX_EPT_ENTRY_FLAG_DIRTY      UINT8_C(8)
#define VMX_EPT_ENTRY_FLAG_MASK       UINT8_C(15)

typedef struct VMX_EPT_CAPABILITIES {
    uint64_t Raw;
    uint32_t Flags;
    uint32_t Reserved;
} VMX_EPT_CAPABILITIES;

typedef struct VMX_EPT_POINTER_CONFIG {
    uint64_t Pml4PhysicalAddress;
    uint32_t PhysicalAddressBits;
    uint8_t MemoryType;
    uint8_t EnableAccessedDirty;
    uint16_t Reserved;
} VMX_EPT_POINTER_CONFIG;


typedef enum VMX_EPT_ENTRY_KIND {
    VMX_EPT_ENTRY_NON_LEAF = 1,
    VMX_EPT_ENTRY_LEAF_4KB = 2,
    VMX_EPT_ENTRY_LEAF_2MB = 3,
    VMX_EPT_ENTRY_LEAF_1GB = 4
} VMX_EPT_ENTRY_KIND;

typedef struct VMX_EPT_ENTRY_CONFIG {
    uint64_t TargetPhysicalAddress;
    uint32_t PhysicalAddressBits;
    uint8_t Kind;
    uint8_t Permissions;
    uint8_t MemoryType;
    uint8_t Flags;
    uint64_t Reserved;
} VMX_EPT_ENTRY_CONFIG;

#define VMX_EPT_TABLE_ENTRIES   UINT32_C(512)
#define VMX_EPT_TABLE_PAGE_SIZE UINT32_C(4096)

typedef enum VMX_EPT_TABLE_LEVEL {
    VMX_EPT_TABLE_LEVEL_PT = 1,
    VMX_EPT_TABLE_LEVEL_PD = 2,
    VMX_EPT_TABLE_LEVEL_PDPT = 3,
    VMX_EPT_TABLE_LEVEL_PML4 = 4
} VMX_EPT_TABLE_LEVEL;

/*
 * Plan a contiguous identity extent [0, MappedBytes). MappedBytes must be an
 * exact leaf-size multiple, so the plan never owns or maps bytes beyond it.
 * The plan describes counts only; it performs no allocation or VMX operation.
 */typedef struct VMX_EPT_IDENTITY_CONFIG {
    uint64_t MappedBytes;
    uint32_t PhysicalAddressBits;
    uint8_t LeafKind;
    uint8_t Reserved8[3];
    uint64_t Reserved;
} VMX_EPT_IDENTITY_CONFIG;

typedef struct VMX_EPT_IDENTITY_PLAN {
    uint64_t MappedBytes;
    uint64_t LeafBytes;
    uint64_t LeafEntries;
    uint64_t TotalTableBytes;
    uint32_t PhysicalAddressBits;
    uint8_t LeafKind;
    uint8_t Reserved8[3];
    uint32_t Pml4Entries;
    uint32_t Pml4Pages;
    uint32_t PdptPages;
    uint32_t PdPages;
    uint32_t PtPages;
    uint32_t TotalTablePages;
    uint32_t Reserved;
} VMX_EPT_IDENTITY_PLAN;

typedef struct VMX_EPT_OWNED_PAGE {
    uint32_t Ordinal;
    uint32_t Level;
    uint32_t LevelIndex;
    uint32_t EntryCount;
    uint64_t Reserved;
} VMX_EPT_OWNED_PAGE;

VMX_EPT_STATUS VmxEptDecodeCapabilities(
    uint64_t raw,
    VMX_EPT_CAPABILITIES* capabilities);

VMX_EPT_STATUS VmxEptValidateCapabilities(
    const VMX_EPT_CAPABILITIES* capabilities);

VMX_EPT_STATUS VmxEptBuildPointer(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_POINTER_CONFIG* config,
    uint64_t* ept_pointer);

/* Build present entries only; callers represent an absent entry with zero. */
VMX_EPT_STATUS VmxEptBuildEntry(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_ENTRY_CONFIG* config,
    uint64_t* entry);

VMX_EPT_STATUS VmxEptValidateEntry(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_ENTRY_CONFIG* config,
    uint64_t entry);

VMX_EPT_STATUS VmxEptBuildIdentityPlan(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* config,
    VMX_EPT_IDENTITY_PLAN* plan);

VMX_EPT_STATUS VmxEptValidateIdentityPlan(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* config,
    const VMX_EPT_IDENTITY_PLAN* plan);

VMX_EPT_STATUS VmxEptDescribeOwnedPage(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* config,
    const VMX_EPT_IDENTITY_PLAN* plan,
    uint32_t ordinal,
    VMX_EPT_OWNED_PAGE* page);

#ifdef __cplusplus
}
#endif
#endif
