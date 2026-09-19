#ifndef HYPERSINV_VMX_PERSISTENT_BASIC_CONTEXT_H
#define HYPERSINV_VMX_PERSISTENT_BASIC_CONTEXT_H

/* WDK-free canonical ABI for caller-owned Persistent V3 BasicOps state. */
#include <stddef.h>
#include <stdint.h>

#define VMX_PERSISTENT_BASIC_OPS_MAGIC UINT32_C(0x4F334256)
#define VMX_PERSISTENT_BASIC_OPS_VERSION UINT32_C(2)

typedef struct _VMX_PERSISTENT_BASIC_OPS_CONTEXT {
    uint32_t Magic;
    uint32_t Version;
    uint64_t CurrentVmcsPa;
    uint32_t CurrentVmcsValid;
    uint32_t Reserved;
    uint32_t Cr3RouteBound;
    uint32_t Cr3RouteBoundContract;
} VMX_PERSISTENT_BASIC_OPS_CONTEXT;

static inline int VmxPersistentBasicContextCr3RouteValid(
    const VMX_PERSISTENT_BASIC_OPS_CONTEXT* context)
{
    return context != NULL &&
        ((context->Cr3RouteBound == 0 && context->Cr3RouteBoundContract == 0) ||
         (context->Cr3RouteBound == 1 &&
          context->Cr3RouteBoundContract == ~UINT32_C(1)));
}

#if defined(__cplusplus)
#define VMX_BASIC_CONTEXT_ASSERT static_assert
#define VMX_BASIC_CONTEXT_ALIGNOF(type) alignof(type)
#else
#define VMX_BASIC_CONTEXT_ASSERT _Static_assert
#define VMX_BASIC_CONTEXT_ALIGNOF(type) _Alignof(type)
#endif

VMX_BASIC_CONTEXT_ASSERT(sizeof(VMX_PERSISTENT_BASIC_OPS_CONTEXT) == 32,
    "BasicOps context size");
VMX_BASIC_CONTEXT_ASSERT(
    VMX_BASIC_CONTEXT_ALIGNOF(VMX_PERSISTENT_BASIC_OPS_CONTEXT) == 8,
    "BasicOps context alignment");
VMX_BASIC_CONTEXT_ASSERT(
    offsetof(VMX_PERSISTENT_BASIC_OPS_CONTEXT, Magic) == 0,
    "BasicOps Magic offset");
VMX_BASIC_CONTEXT_ASSERT(
    offsetof(VMX_PERSISTENT_BASIC_OPS_CONTEXT, Version) == 4,
    "BasicOps Version offset");
VMX_BASIC_CONTEXT_ASSERT(
    offsetof(VMX_PERSISTENT_BASIC_OPS_CONTEXT, CurrentVmcsPa) == 8,
    "BasicOps VMCS PA offset");
VMX_BASIC_CONTEXT_ASSERT(
    offsetof(VMX_PERSISTENT_BASIC_OPS_CONTEXT, CurrentVmcsValid) == 16,
    "BasicOps valid offset");
VMX_BASIC_CONTEXT_ASSERT(
    offsetof(VMX_PERSISTENT_BASIC_OPS_CONTEXT, Reserved) == 20,
    "BasicOps reserved offset");
VMX_BASIC_CONTEXT_ASSERT(
    offsetof(VMX_PERSISTENT_BASIC_OPS_CONTEXT, Cr3RouteBound) == 24,
    "BasicOps CR3 route bound offset");
VMX_BASIC_CONTEXT_ASSERT(
    offsetof(VMX_PERSISTENT_BASIC_OPS_CONTEXT, Cr3RouteBoundContract) == 28,
    "BasicOps CR3 route contract offset");

#undef VMX_BASIC_CONTEXT_ALIGNOF
#undef VMX_BASIC_CONTEXT_ASSERT
#endif
