#include "vmx_cr3_target_selection.h"
#include <string.h>

static int Zero(const void* value, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)value;
    size_t index;
    if (value == NULL) return 0;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0) return 0;
    return 1;
}

static int Span(const void* value, size_t size, uintptr_t* begin, uintptr_t* end)
{
    uintptr_t address = (uintptr_t)value;
    if (value == NULL || size == 0 || address > UINTPTR_MAX - size) return 0;
    *begin = address;
    *end = address + size;
    return 1;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t first_begin, first_end, second_begin, second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int PhysicalBitsValid(uint32_t bits)
{
    return bits >= 36 && bits <= 52;
}

static uint64_t PhysicalMask(uint32_t bits)
{
    return (UINT64_C(1) << bits) - 1;
}

static int RootValid(uint64_t root, uint64_t physical_mask)
{
    return root != 0 && (root & VMX_CR3_TARGET_PCID_MASK) == 0 &&
        (root & ~physical_mask) == 0;
}

static int SetValid(const VMX_CR3_TARGET_SET* set)
{
    uint64_t physical_mask;
    if (set == NULL || set->Version != VMX_CR3_TARGET_SET_VERSION ||
        set->Count == 0 || set->Count > VMX_CR3_TARGET_MAX_ROOTS ||
        !PhysicalBitsValid(set->PhysicalAddressBits) ||
        set->PcideEnabled > 1 || set->Generation == 0 || set->Reserved != 0)
        return 0;
    physical_mask = PhysicalMask(set->PhysicalAddressBits);
    if (!RootValid(set->Roots[0], physical_mask)) return 0;
    if (set->Count == 1)
        return set->Roots[1] == 0;
    return RootValid(set->Roots[1], physical_mask) &&
        set->Roots[0] != set->Roots[1];
}

VMX_CR3_TARGET_STATUS VmxCr3TargetSetBuild(
    uint32_t physical_address_bits,
    uint32_t pcide_enabled,
    const uint64_t* roots,
    uint32_t count,
    uint64_t generation,
    VMX_CR3_TARGET_SET* out_set)
{
    VMX_CR3_TARGET_SET staged;
    uint64_t physical_mask;
    size_t root_bytes;
    uint32_t index;

    if (roots == NULL || out_set == NULL || count == 0 ||
        count > VMX_CR3_TARGET_MAX_ROOTS ||
        !PhysicalBitsValid(physical_address_bits) || pcide_enabled > 1 ||
        generation == 0 || !Zero(out_set, sizeof(*out_set)))
        return VMX_CR3_TARGET_INVALID;
    root_bytes = (size_t)count * sizeof(*roots);
    if (!Separate(roots, root_bytes, out_set, sizeof(*out_set)))
        return VMX_CR3_TARGET_INVALID;
    physical_mask = PhysicalMask(physical_address_bits);
    for (index = 0; index < count; ++index)
        if (!RootValid(roots[index], physical_mask))
            return VMX_CR3_TARGET_INVALID;
    if (count == 2 && roots[0] == roots[1])
        return VMX_CR3_TARGET_INVALID;

    memset(&staged, 0, sizeof(staged));
    staged.Version = VMX_CR3_TARGET_SET_VERSION;
    staged.Count = count;
    staged.PhysicalAddressBits = physical_address_bits;
    staged.PcideEnabled = pcide_enabled;
    for (index = 0; index < count; ++index) staged.Roots[index] = roots[index];
    staged.Generation = generation;
    if (!SetValid(&staged)) return VMX_CR3_TARGET_INVARIANT;
    *out_set = staged;
    return VMX_CR3_TARGET_OK;
}

VMX_CR3_TARGET_STATUS VmxCr3TargetSelect(
    const VMX_CR3_TARGET_SET* set,
    uint64_t operand,
    VMX_CR3_TARGET_DECISION* out_decision)
{
    VMX_CR3_TARGET_DECISION staged;
    uint64_t physical_mask, allowed_mask, root;
    uint32_t index;

    if (!SetValid(set) || out_decision == NULL ||
        !Separate(set, sizeof(*set), out_decision, sizeof(*out_decision)) ||
        !Zero(out_decision, sizeof(*out_decision)))
        return VMX_CR3_TARGET_INVALID;
    physical_mask = PhysicalMask(set->PhysicalAddressBits);
    allowed_mask = physical_mask;
    if (set->PcideEnabled) allowed_mask |= VMX_CR3_TARGET_NO_FLUSH;
    if ((operand & ~allowed_mask) != 0)
        return VMX_CR3_TARGET_UNSUPPORTED;
    if (!set->PcideEnabled && (operand & VMX_CR3_TARGET_PCID_MASK) != 0)
        return VMX_CR3_TARGET_UNSUPPORTED;
    root = operand & physical_mask & VMX_CR3_TARGET_PAGE_MASK;

    memset(&staged, 0, sizeof(staged));
    staged.Version = VMX_CR3_TARGET_DECISION_VERSION;
    staged.MatchedIndex = UINT32_MAX;
    staged.Pcid = set->PcideEnabled
        ? (uint32_t)(operand & VMX_CR3_TARGET_PCID_MASK) : 0;
    staged.Operand = operand;
    staged.NormalizedRoot = root;
    staged.Generation = set->Generation;
    staged.NoFlush = set->PcideEnabled &&
        (operand & VMX_CR3_TARGET_NO_FLUSH) != 0;
    for (index = 0; index < set->Count; ++index) {
        if (root == set->Roots[index]) {
            staged.Matched = 1;
            staged.MatchedIndex = index;
            break;
        }
    }
    *out_decision = staged;
    return VMX_CR3_TARGET_OK;
}