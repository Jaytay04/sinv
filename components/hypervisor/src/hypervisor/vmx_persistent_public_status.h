#ifndef HYPERSINV_VMX_PERSISTENT_PUBLIC_STATUS_H
#define HYPERSINV_VMX_PERSISTENT_PUBLIC_STATUS_H
/* WDK-free, pointer-free Persistent V3 status wire contract. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _VMX_PERSISTENT_LIFECYCLE VMX_PERSISTENT_LIFECYCLE;
typedef struct _VMX_PERSISTENT_CONTEXT VMX_PERSISTENT_CONTEXT;
typedef struct _VMX_PERSISTENT_OPERATION_RESULT VMX_PERSISTENT_OPERATION_RESULT;
#define VMX_PERSISTENT_PUBLIC_STATUS_VERSION UINT32_C(3)
#define VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX UINT32_MAX
#define VMX_PERSISTENT_PUBLIC_HEADER_HAS_OPERATION_ERROR (UINT32_C(1)<<0)
#define VMX_PERSISTENT_PUBLIC_HEADER_HAS_CLEANUP_ERROR   (UINT32_C(1)<<1)
#define VMX_PERSISTENT_PUBLIC_HEADER_BUSY                (UINT32_C(1)<<2)
#define VMX_PERSISTENT_PUBLIC_RECORD_ACTIVE              (UINT32_C(1)<<0)
#define VMX_PERSISTENT_PUBLIC_RECORD_CLEANUP_REQUIRED    (UINT32_C(1)<<1)
#define VMX_PERSISTENT_PUBLIC_RECORD_PUBLISHED           (UINT32_C(1)<<2)
#define VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_ENTERED       (UINT32_C(1)<<3)
#define VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_LEFT          (UINT32_C(1)<<4)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_CONTEXT_RESOURCES (UINT32_C(1)<<0)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_RESTORE            (UINT32_C(1)<<1)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_CR0                (UINT32_C(1)<<2)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_CR4                (UINT32_C(1)<<3)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_GDTR               (UINT32_C(1)<<4)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_IDTR               (UINT32_C(1)<<5)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_AFFINITY           (UINT32_C(1)<<6)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_IRQL               (UINT32_C(1)<<7)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_VMX_ACTIVE         (UINT32_C(1)<<8)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_VMCS_CURRENT       (UINT32_C(1)<<9)
#define VMX_PERSISTENT_PUBLIC_RESIDUAL_SHUTDOWN           (UINT32_C(1)<<10)
typedef enum _VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_SOURCE {
    VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_NONE=0,
    VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_VMXON=1,
    VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_INITIAL_VMCLEAR=2,
    VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_VMPTRLD=3,
    VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_PLAN=4,
    VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_CLEANUP_VMCLEAR=5,
    VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_CLEANUP_VMXOFF=6
} VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_SOURCE;
typedef struct _VMX_PERSISTENT_PUBLIC_STATUS_HEADER {
    uint32_t Version,HeaderSize,RecordSize,TotalSize;
    uint32_t ContextCount,LifecycleState,Flags,Reserved;
    int32_t FirstOperationStatus; uint32_t FirstOperationIndex;
    int32_t FirstCleanupStatus; uint32_t FirstCleanupIndex;
} VMX_PERSISTENT_PUBLIC_STATUS_HEADER;
typedef struct _VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD {
    uint32_t SystemIndex; uint16_t Group; uint8_t Number,LifecycleStage;
    uint32_t Flags,OwnedMask,ContextOwnedResources,ResidualFlags;
    uint32_t OperationStage; int32_t PrimaryStatus,CleanupStatus;
    uint32_t PrimarySource,PrimaryOutcome,PrimaryErrorValid,PrimaryError;
    uint32_t CleanupSource,CleanupOutcome,CleanupErrorValid,CleanupError,Reserved;
} VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD;
#define VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE UINT32_C(48)
#define VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE UINT32_C(72)
typedef struct _VMX_PERSISTENT_PUBLIC_STATUS_VIEW {
    uint32_t Version,HeaderSize,RecordSize,Reserved;
    const VMX_PERSISTENT_LIFECYCLE* Lifecycle;
    const VMX_PERSISTENT_CONTEXT* Contexts; size_t ContextCount;
    const VMX_PERSISTENT_OPERATION_RESULT* OperationResults; size_t OperationResultCount;
    size_t Count;
} VMX_PERSISTENT_PUBLIC_STATUS_VIEW;
typedef enum _VMX_PERSISTENT_PUBLIC_STATUS_RESULT {
    VMX_PERSISTENT_PUBLIC_STATUS_OK=0,
    VMX_PERSISTENT_PUBLIC_STATUS_INVALID=-1,
    VMX_PERSISTENT_PUBLIC_STATUS_BUFFER_TOO_SMALL=-2,
    VMX_PERSISTENT_PUBLIC_STATUS_OVERFLOW=-3
} VMX_PERSISTENT_PUBLIC_STATUS_RESULT;
/* V3 requires exact current HeaderSize/RecordSize; future sizes are rejected.
 * Query is output=NULL/output_size=0 and returns BUFFER_TOO_SMALL plus the exact
 * required size without writing output. Non-query output must not overlap View
 * or its arrays. If separateness cannot be proven, INVALID preserves output and
 * required_size. Once proven, every failure clears exactly output_size bytes;
 * required_size is zero on INVALID/OVERFLOW and exact on OK/BUFFER_TOO_SMALL.
 * Success clears the full buffer before deterministic publication. */
VMX_PERSISTENT_PUBLIC_STATUS_RESULT VmxPersistentPublicStatusEncode(
    const VMX_PERSISTENT_PUBLIC_STATUS_VIEW* view,
    void* output,size_t output_size,size_t* required_size);
#ifdef __cplusplus
}
#define VMX_PUBLIC_ASSERT static_assert
#define VMX_PUBLIC_ALIGNOF alignof
#else
#define VMX_PUBLIC_ASSERT _Static_assert
#define VMX_PUBLIC_ALIGNOF _Alignof
#endif
VMX_PUBLIC_ASSERT(sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)==48,"public header ABI");
VMX_PUBLIC_ASSERT(sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD)==72,"public record ABI");
VMX_PUBLIC_ASSERT(VMX_PUBLIC_ALIGNOF(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)==4,"public header alignment");
VMX_PUBLIC_ASSERT(VMX_PUBLIC_ALIGNOF(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD)==4,"public record alignment");
VMX_PUBLIC_ASSERT(offsetof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER,FirstOperationStatus)==32,"header offsets");
VMX_PUBLIC_ASSERT(offsetof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD,ResidualFlags)==20,"record offsets");
VMX_PUBLIC_ASSERT(offsetof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD,PrimarySource)==36,"diagnostic offsets");
#undef VMX_PUBLIC_ASSERT
#undef VMX_PUBLIC_ALIGNOF
#endif
