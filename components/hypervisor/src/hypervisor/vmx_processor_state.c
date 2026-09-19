/*
 * vmx_processor_state.c — Processor state validation (R4)
 *
 * Pure validation logic. No hardware access — testable in user mode.
 */

#include "vmx_processor_state.h"

#include <string.h>  /* memset */

/*---------------------------------------------------------------------------
 * Helpers
 *---------------------------------------------------------------------------*/

/* Check if a selector has the TI (Table Indicator) bit set. */
static bool HasTiBit(uint16_t sel)
{
    return (sel & 0x0004u) != 0;
}

/* Check if a selector is null (index == 0, regardless of RPL). */
static bool IsNullSelector(uint16_t sel)
{
    return (sel & 0xFFF8u) == 0;
}

/*
 * Intel 64 permits an unusable SS only for a 64-bit, CPL0 guest. Windows can
 * transiently expose exactly this architectural state (SS=0, CS.L=1) on any
 * logical processor, so it must be represented rather than rejected.
 */
static bool IsLongModeNullSsAllowed(const VMX_PROCESSOR_STATE* state)
{
    const uint32_t cs_long = UINT32_C(1) << 13;
    return state->Ss == 0 &&
           (state->Ia32Efer & EFER_LONG_MODE_ACTIVE) != 0 &&
           (state->CsDesc.access_rights & cs_long) != 0 &&
           (state->Cs & 0x0003u) == 0;
}

static bool IsSegmentBaseCanonical(uint16_t selector, const VMX_SEGMENT_DESC* desc)
{
    return IsNullSelector(selector) ||
           (desc->access_rights & VMX_SEGMENT_ACCESS_UNUSABLE) != 0 ||
           VmxIsAddressCanonical(desc->base);
}

/*---------------------------------------------------------------------------
 * VmxPopulateProcessorState
 *---------------------------------------------------------------------------*/
bool VmxPopulateProcessorState(
    const VMX_RAW_PROCESSOR_STATE* raw,
    const void* gdt_base,
    const VMX_PROCESSOR_MSR_STATE* msrs,
    VMX_PROCESSOR_STATE* state)
{
    uint32_t tr_offset;

    if (state == NULL) return false;
    (void)memset(state, 0, sizeof(*state));
    if (raw == NULL || gdt_base == NULL || msrs == NULL) {
        return false;
    }
    state->Cr0 = msrs->Cr0;
    state->Cr3 = msrs->Cr3;
    state->Cr4 = msrs->Cr4;
    state->Dr7 = msrs->Dr7;
    state->Ia32Debugctl = msrs->Ia32Debugctl;
    state->Ia32Pat = msrs->Ia32Pat;
    state->Ia32Efer = msrs->Ia32Efer;
    state->SysenterCs = msrs->SysenterCs;
    state->SysenterEsp = msrs->SysenterEsp;
    state->SysenterEip = msrs->SysenterEip;
    state->FsBase = msrs->FsBase;
    state->GsBase = msrs->GsBase;

    state->Es = raw->Es;
    state->Cs = raw->Cs;
    state->Ss = raw->Ss;
    state->Ds = raw->Ds;
    state->Fs = raw->Fs;
    state->Gs = raw->Gs;
    state->Ldtr = raw->Ldtr;
    state->Tr = raw->Tr;
    state->GdtrLimit = raw->GdtrLimit;
    state->GdtrBase = raw->GdtrBase;
    state->IdtrLimit = raw->IdtrLimit;
    state->IdtrBase = raw->IdtrBase;

    state->EsDesc = VmxDecodeSegmentSelector(raw->Es, gdt_base, raw->GdtrLimit);
    state->CsDesc = VmxDecodeSegmentSelector(raw->Cs, gdt_base, raw->GdtrLimit);
    state->SsDesc = VmxDecodeSegmentSelector(raw->Ss, gdt_base, raw->GdtrLimit);
    state->DsDesc = VmxDecodeSegmentSelector(raw->Ds, gdt_base, raw->GdtrLimit);
    state->FsDesc = VmxDecodeSegmentSelector(raw->Fs, gdt_base, raw->GdtrLimit);
    state->GsDesc = VmxDecodeSegmentSelector(raw->Gs, gdt_base, raw->GdtrLimit);
    state->LdtrDesc = VmxDecodeSegmentSelector(raw->Ldtr, gdt_base, raw->GdtrLimit);
    state->TrDesc = VmxDecodeSegmentSelector(raw->Tr, gdt_base, raw->GdtrLimit);

    /* TR is a 64-bit available/busy TSS and must include its second slot. */
    tr_offset = (uint32_t)(raw->Tr & 0xFFF8u);
    if (HasTiBit(raw->Tr) || IsNullSelector(raw->Tr) ||
        tr_offset + 15u > raw->GdtrLimit || !state->TrDesc.present ||
        !state->TrDesc.is_system ||
        (state->TrDesc.type != 0x9u && state->TrDesc.type != 0xBu)) {
        return false;
    }

    (void)memcpy(state->TrSysDesc,
                 (const uint8_t*)gdt_base + tr_offset,
                 sizeof(state->TrSysDesc));
    return VmxValidateProcessorState(state);
}

/*---------------------------------------------------------------------------
 * VmxValidateProcessorState
 *---------------------------------------------------------------------------*/
bool VmxValidateProcessorState(VMX_PROCESSOR_STATE* state)
{
    if (state == NULL) return false;

    state->IsValid = false;

    /* Reject TI-based selectors BEFORE sanitization */
    if (HasTiBit(state->Es)  || HasTiBit(state->Cs)  ||
        HasTiBit(state->Ss)  || HasTiBit(state->Ds)  ||
        HasTiBit(state->Fs)  || HasTiBit(state->Gs)  ||
        HasTiBit(state->Ldtr) || HasTiBit(state->Tr)) {
        return false;
    }

    /* CS must be present and valid */
    if (IsNullSelector(state->Cs) || !state->CsDesc.present || (state->CsDesc.access_rights & VMX_SEGMENT_ACCESS_UNUSABLE) != 0) return false;
    /*
     * A null SS is valid only for the exact IA-32e CPL0 case. Publish the
     * architecturally unusable VMCS encoding; all other SS values remain
     * subject to the original present/usable checks.
     */
    if (IsNullSelector(state->Ss)) {
        if (!IsLongModeNullSsAllowed(state)) return false;
        state->SsDesc.base = 0;
        state->SsDesc.limit = 0;
        state->SsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
        state->SsDesc.present = false;
    } else if (!state->SsDesc.present ||
               (state->SsDesc.access_rights &
                VMX_SEGMENT_ACCESS_UNUSABLE) != 0) {
        return false;
    }
    /* TR must be present, valid, and a system descriptor */
    if (IsNullSelector(state->Tr) || !state->TrDesc.present || !state->TrDesc.is_system || (state->TrDesc.access_rights & VMX_SEGMENT_ACCESS_UNUSABLE) != 0) return false;

    /* Null non-required data segments become unusable */
    if (IsNullSelector(state->Es)) {
        state->EsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
        state->EsDesc.present = false;
    }
    if (IsNullSelector(state->Ds)) {
        state->DsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
        state->DsDesc.present = false;
    }
    if (IsNullSelector(state->Fs)) {
        state->FsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
        state->FsDesc.present = false;
    }
    if (IsNullSelector(state->Gs)) {
        state->GsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
        state->GsDesc.present = false;
    }

    /* Null LDTR becomes unusable */
    if (IsNullSelector(state->Ldtr)) {
        state->LdtrDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
        state->LdtrDesc.present = false;
    }

    /* Reject non-canonical bases for every usable segment and table. */
    if (!IsSegmentBaseCanonical(state->Es, &state->EsDesc) || !IsSegmentBaseCanonical(state->Cs, &state->CsDesc) ||
        !IsSegmentBaseCanonical(state->Ss, &state->SsDesc) || !IsSegmentBaseCanonical(state->Ds, &state->DsDesc) ||
        !IsSegmentBaseCanonical(state->Fs, &state->FsDesc) || !IsSegmentBaseCanonical(state->Gs, &state->GsDesc) ||
        !IsSegmentBaseCanonical(state->Ldtr, &state->LdtrDesc) || !IsSegmentBaseCanonical(state->Tr, &state->TrDesc) ||
        !VmxIsAddressCanonical(state->GdtrBase) || !VmxIsAddressCanonical(state->IdtrBase) ||
        !VmxIsAddressCanonical(state->FsBase) || !VmxIsAddressCanonical(state->GsBase)) {
        return false;
    }

    state->IsValid = true;
    return true;
}
