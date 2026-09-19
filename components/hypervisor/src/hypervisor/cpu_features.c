/*
 * cpu_features.c — CPU feature detection (pure logic)
 *
 * Pure bit-extraction functions that operate on CPUID/MSR register values
 * passed as parameters. These are fully unit-testable without hardware.
 *
 * Hardware sampling belongs to explicit WDK adapters. This pure library must
 * not expose privileged I/O wrappers or user-mode fallback results.
 */

#include "vmx.h"

/*===========================================================================
 * Pure Logic — CPUID Bit Extraction
 *===========================================================================*/

bool CpuCheckVmxFromEcx(uint32_t ecx) {
    /* CPUID leaf 1, ECX bit 5 = VMX support */
    return (ecx & (1u << 5)) != 0;
}

bool CpuCheckEptFromCtls2(uint64_t procbased_ctls2) {
    /* IA32_VMX_PROCBASED_CTLS2 bit 1 = EPT */
    return (procbased_ctls2 & SECONDARY_ENABLE_EPT) != 0;
}

bool CpuCheckVpidFromCtls2(uint64_t procbased_ctls2) {
    /* IA32_VMX_PROCBASED_CTLS2 bit 5 = VPID */
    return (procbased_ctls2 & SECONDARY_ENABLE_VPID) != 0;
}

bool CpuCheckUnrestrictedGuestFromCtls2(uint64_t procbased_ctls2) {
    /* IA32_VMX_PROCBASED_CTLS2 bit 7 = Unrestricted Guest */
    return (procbased_ctls2 & SECONDARY_UNRESTRICTED_GUEST) != 0;
}

bool CpuCheckCetFromLeaf7(uint32_t ecx, uint32_t edx) {
    /* CPUID leaf 7: ECX bit 7 = SHSTK, EDX bit 20 = IBT (CET) */
    bool has_shstk = (ecx & (1u << 7)) != 0;
    bool has_ibt   = (edx & (1u << 20)) != 0;
    return has_shstk || has_ibt;
}

/*===========================================================================
 * MTRR Range Management — Pure Logic
 *
 * MTRR parsing operates on register values (no kernel calls).
 *===========================================================================*/

#include "ept.h"   /* for ept_mtrr_entry_t */

/* Unify fixed + variable MTRR ranges into a single sorted list.
 * Variable MTRRs override fixed MTRRs. UC always takes precedence.
 * Pure logic — no kernel dependencies.
 *
 * Parameters:
 *   fixed[]     — parsed fixed-range MTRR entries
 *   fixed_count — number of fixed entries
 *   var[]       — parsed variable-range MTRR entries
 *   var_count   — number of variable entries
 *   unified[]   — output buffer for unified ranges
 *   max_unified — capacity of unified[] buffer
 *   out_count   — receives actual number of unified entries
 *
 * Returns 0 on success, -1 if output buffer too small. */

int MtrrUnifyRanges(
    const ept_mtrr_entry_t fixed[], uint32_t fixed_count,
    const ept_mtrr_entry_t var[],   uint32_t var_count,
    ept_mtrr_entry_t unified[],     uint32_t max_unified,
    uint32_t* out_count)
{
    uint32_t count = 0;
    uint32_t i, j;

    if (out_count == NULL) return -1;
    *out_count = 0;

    /* Step 1: Copy variable MTRRs first (they override fixed).
     * Only copy valid (non-zero base + mask) entries. */
    for (i = 0; i < var_count && count < max_unified; i++) {
        if (var[i].base != 0 || var[i].mask != 0) {
            unified[count++] = var[i];
        }
    }

    /* Step 2: Add fixed ranges that aren't overridden by variable MTRRs.
     * A fixed range is overridden if a variable range covers the same GPA. */
    for (i = 0; i < fixed_count && count < max_unified; i++) {
        bool overridden = false;

        for (j = 0; j < var_count; j++) {
            /* Check if variable range covers the fixed range base */
            uint64_t var_end = var[j].base + (~var[j].mask + 1);
            if (fixed[i].base >= var[j].base && fixed[i].base < var_end) {
                overridden = true;
                break;
            }
        }

        if (!overridden) {
            unified[count++] = fixed[i];
        }
    }

    /* Step 3: Sort by base address (simple insertion sort — small N) */
    for (i = 1; i < count; i++) {
        ept_mtrr_entry_t key = unified[i];
        j = i;
        while (j > 0 && unified[j - 1].base > key.base) {
            unified[j] = unified[j - 1];
            j--;
        }
        unified[j] = key;
    }

    if (count > max_unified) {
        return -1;
    }

    *out_count = count;
    return 0;
}

/* Get default memory type for a GPA from the MTRR map.
 * Returns EPT_MT_WRITE_BACK for RAM, EPT_MT_UNCACHEABLE for MMIO.
 * Walks the MTRR ranges and returns the matching type. */
uint8_t MtrrGetMemoryType(
    uint64_t gpa,
    const ept_mtrr_entry_t mtrr_map[],
    uint32_t mtrr_count)
{
    /* Walk the MTRR map in reverse (higher-priority entries last after sorting).
     * The first match wins. Variable MTRRs override fixed, so they appear first. */
    for (uint32_t i = 0; i < mtrr_count; i++) {
        uint64_t range_end = mtrr_map[i].base + (~mtrr_map[i].mask + 1);
        if (gpa >= mtrr_map[i].base && gpa < range_end) {
            return mtrr_map[i].type;
        }
    }

    /* Default: WB for RAM-like regions, UC for MMIO (above 4GB typically) */
    return EPT_MT_WRITE_BACK;
}
