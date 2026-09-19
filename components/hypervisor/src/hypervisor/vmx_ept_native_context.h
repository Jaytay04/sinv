#ifndef HYPERSINV_VMX_EPT_NATIVE_CONTEXT_H
#define HYPERSINV_VMX_EPT_NATIVE_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_NATIVE_CONTEXT_MAGIC UINT32_C(0x33435045)
#define VMX_EPT_NATIVE_CONTEXT_VERSION UINT32_C(1)
#define VMX_EPT_NATIVE_MAX_GROUPS UINT16_C(64)
#define VMX_EPT_NATIVE_MAX_PROCESSORS_PER_GROUP UINT8_C(64)
#define VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS UINT32_C(4096)

typedef struct VMX_EPT_NATIVE_CONTEXT {
    uint32_t Magic;
    uint32_t Version;
    uint16_t Group;
    uint8_t Number;
    uint8_t Reserved8;
    uint32_t SystemIndex;
    volatile int32_t RootOwned;
    uint32_t Reserved32;
} VMX_EPT_NATIVE_CONTEXT;

int VmxEptNativeContextValidate(const VMX_EPT_NATIVE_CONTEXT* context);

#if defined(__cplusplus)
#define VMX_EPT_NATIVE_ASSERT static_assert
#define VMX_EPT_NATIVE_ALIGNOF(type) alignof(type)
#else
#define VMX_EPT_NATIVE_ASSERT _Static_assert
#define VMX_EPT_NATIVE_ALIGNOF(type) _Alignof(type)
#endif

VMX_EPT_NATIVE_ASSERT(sizeof(VMX_EPT_NATIVE_CONTEXT) == 24,
    "native EPT context size");
VMX_EPT_NATIVE_ASSERT(VMX_EPT_NATIVE_ALIGNOF(VMX_EPT_NATIVE_CONTEXT) == 4,
    "native EPT context alignment");
VMX_EPT_NATIVE_ASSERT(offsetof(VMX_EPT_NATIVE_CONTEXT, Magic) == 0,
    "native EPT magic offset");
VMX_EPT_NATIVE_ASSERT(offsetof(VMX_EPT_NATIVE_CONTEXT, Group) == 8,
    "native EPT group offset");
VMX_EPT_NATIVE_ASSERT(offsetof(VMX_EPT_NATIVE_CONTEXT, SystemIndex) == 12,
    "native EPT system-index offset");
VMX_EPT_NATIVE_ASSERT(offsetof(VMX_EPT_NATIVE_CONTEXT, RootOwned) == 16,
    "native EPT root-owned offset");

#undef VMX_EPT_NATIVE_ALIGNOF
#undef VMX_EPT_NATIVE_ASSERT

#ifdef __cplusplus
}
#endif
#endif
