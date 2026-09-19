#ifndef HYPERSINV_VMX_CR3_TARGET_SELECTION_H
#define HYPERSINV_VMX_CR3_TARGET_SELECTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_CR3_TARGET_SET_VERSION UINT32_C(1)
#define VMX_CR3_TARGET_DECISION_VERSION UINT32_C(1)
#define VMX_CR3_TARGET_MAX_ROOTS UINT32_C(2)
#define VMX_CR3_TARGET_PAGE_MASK UINT64_C(0xfffffffffffff000)
#define VMX_CR3_TARGET_PCID_MASK UINT64_C(0x0000000000000fff)
#define VMX_CR3_TARGET_NO_FLUSH UINT64_C(0x8000000000000000)

typedef enum VMX_CR3_TARGET_STATUS {
    VMX_CR3_TARGET_OK = 0,
    VMX_CR3_TARGET_INVALID = -1,
    VMX_CR3_TARGET_UNSUPPORTED = -2,
    VMX_CR3_TARGET_INVARIANT = -3,
    VMX_CR3_TARGET_BUSY = -4
} VMX_CR3_TARGET_STATUS;

typedef struct VMX_CR3_TARGET_SET {
    uint32_t Version;
    uint32_t Count;
    uint32_t PhysicalAddressBits;
    uint32_t PcideEnabled;
    uint64_t Roots[VMX_CR3_TARGET_MAX_ROOTS];
    uint64_t Generation;
    uint64_t Reserved;
} VMX_CR3_TARGET_SET;

typedef struct VMX_CR3_TARGET_DECISION {
    uint32_t Version;
    uint32_t Matched;
    uint32_t MatchedIndex;
    uint32_t Pcid;
    uint64_t Operand;
    uint64_t NormalizedRoot;
    uint64_t Generation;
    uint32_t NoFlush;
    uint32_t Reserved;
} VMX_CR3_TARGET_DECISION;

/* WDK-free and allocation-free. roots contains one or two trusted, page-aligned
 * process page-table roots. Two roots support a future KVA-shadow kernel/user
 * pair without resolving process objects inside this module. The output must be
 * initially zero and remains unchanged on failure. */
VMX_CR3_TARGET_STATUS VmxCr3TargetSetBuild(
    uint32_t physical_address_bits,
    uint32_t pcide_enabled,
    const uint64_t* roots,
    uint32_t count,
    uint64_t generation,
    VMX_CR3_TARGET_SET* out_set);

/* Classifies one MOV-to-CR3 operand. PCID and bit 63 are decoded only when
 * PCIDE is enabled. A well-formed non-target operand returns OK with Matched=0.
 * This module remains side-effect free and is consumed only through the
 * authenticated route owner; it performs no process lookup, IOCTL, hook,
 * VMCS, or resident VM-exit operation directly. */
VMX_CR3_TARGET_STATUS VmxCr3TargetSelect(
    const VMX_CR3_TARGET_SET* set,
    uint64_t operand,
    VMX_CR3_TARGET_DECISION* out_decision);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define VMX_CR3_TARGET_ASSERT static_assert
#define VMX_CR3_TARGET_ALIGNOF alignof
#else
#define VMX_CR3_TARGET_ASSERT _Static_assert
#define VMX_CR3_TARGET_ALIGNOF _Alignof
#endif
VMX_CR3_TARGET_ASSERT(sizeof(VMX_CR3_TARGET_SET) == 48,
    "CR3 target set size");
VMX_CR3_TARGET_ASSERT(VMX_CR3_TARGET_ALIGNOF(VMX_CR3_TARGET_SET) == 8,
    "CR3 target set alignment");
VMX_CR3_TARGET_ASSERT(offsetof(VMX_CR3_TARGET_SET, Roots) == 16,
    "CR3 target roots offset");
VMX_CR3_TARGET_ASSERT(sizeof(VMX_CR3_TARGET_DECISION) == 48,
    "CR3 target decision size");
VMX_CR3_TARGET_ASSERT(offsetof(VMX_CR3_TARGET_DECISION, Operand) == 16,
    "CR3 operand offset");
VMX_CR3_TARGET_ASSERT(offsetof(VMX_CR3_TARGET_DECISION, Generation) == 32,
    "CR3 generation offset");
#undef VMX_CR3_TARGET_ASSERT
#undef VMX_CR3_TARGET_ALIGNOF

#endif