/*
 * vmx_processor_state.h — Processor state capture and validation (R4)
 *
 * Defines VMX_PROCESSOR_STATE for full processor context required by
 * VMX transitions. WDK-independent — usable from user-mode tests.
 */

#ifndef HYPERSINV_VMX_PROCESSOR_STATE_H
#define HYPERSINV_VMX_PROCESSOR_STATE_H

#include <stdint.h>
#include <stdbool.h>

#include "vmx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Validated processor state for VMX entry preparation. */
typedef struct _VMX_PROCESSOR_STATE {
    /* Control registers */
    uint64_t Cr0;
    uint64_t Cr3;
    uint64_t Cr4;
    uint64_t Dr7;

    /* MSRs read at capture time */
    uint64_t Ia32Debugctl;
    uint64_t Ia32Pat;
    uint64_t Ia32Efer;
    uint64_t SysenterCs;
    uint64_t SysenterEsp;
    uint64_t SysenterEip;
    uint64_t FsBase;
    uint64_t GsBase;

    /* Captured guest selectors; host emission clears RPL/TI bits. */
    uint16_t Es;
    uint16_t Cs;
    uint16_t Ss;
    uint16_t Ds;
    uint16_t Fs;
    uint16_t Gs;
    uint16_t Ldtr;
    uint16_t Tr;

    /* Decoded GDT/LDT descriptors. */
    VMX_SEGMENT_DESC EsDesc;
    VMX_SEGMENT_DESC CsDesc;
    VMX_SEGMENT_DESC SsDesc;
    VMX_SEGMENT_DESC DsDesc;
    VMX_SEGMENT_DESC FsDesc;
    VMX_SEGMENT_DESC GsDesc;
    VMX_SEGMENT_DESC LdtrDesc;
    VMX_SEGMENT_DESC TrDesc;

    /* Table registers */
    uint64_t GdtrBase;
    uint64_t IdtrBase;
    uint16_t GdtrLimit;
    uint16_t IdtrLimit;

    /* Complete 16-byte system descriptor for TR (raw, for VMCS) */
    uint64_t TrSysDesc[2];

    bool IsValid;
} VMX_PROCESSOR_STATE;

/*===========================================================================
 * VMX_RAW_PROCESSOR_STATE — MASM-captured selectors and table registers
 *
 * Packed to match the assembly layout. Filled by
 * VmxAsmCaptureSelectorsAndTables.
 *===========================================================================*/
#pragma pack(push, 1)
typedef struct _VMX_RAW_PROCESSOR_STATE {
    uint16_t Es;
    uint16_t Cs;
    uint16_t Ss;
    uint16_t Ds;
    uint16_t Fs;
    uint16_t Gs;
    uint16_t Ldtr;
    uint16_t Tr;
    uint16_t GdtrLimit;
    uint64_t GdtrBase;
    uint16_t IdtrLimit;
    uint64_t IdtrBase;
} VMX_RAW_PROCESSOR_STATE;
#pragma pack(pop)
/* Values captured by the kernel adapter and supplied to the pure builder. */
typedef struct _VMX_PROCESSOR_MSR_STATE {
    uint64_t Cr0;
    uint64_t Cr3;
    uint64_t Cr4;
    uint64_t Dr7;
    uint64_t Ia32Debugctl;
    uint64_t Ia32Pat;
    uint64_t Ia32Efer;
    uint64_t SysenterCs;
    uint64_t SysenterEsp;
    uint64_t SysenterEip;
    uint64_t FsBase;
    uint64_t GsBase;
} VMX_PROCESSOR_MSR_STATE;

/*
 * Build and validate a processor state without reading hardware.  gdt_base
 * is explicit so tests can provide a fixture rather than dereferencing the
 * captured GDTR address.  The raw selector/table ABI remains unchanged.
 */
bool VmxPopulateProcessorState(
    const VMX_RAW_PROCESSOR_STATE* raw,
    const void* gdt_base,
    const VMX_PROCESSOR_MSR_STATE* msrs,
    VMX_PROCESSOR_STATE* state);

/*===========================================================================
 * VmxValidateProcessorState — Reject invalid processor state
 *
 * Rules:
 *  - TI-based selectors (bit 2 set) are rejected for all segments.
 *  - Null selectors are rejected for CS and TR.
 *  - SS=0 is accepted only for an IA-32e 64-bit CPL0 guest and is normalized
 *    to the VMCS unusable encoding; other null SS values are rejected.
 *  - Null non-required selectors (ES, DS, FS, GS, LDTR) are treated as
 *    unusable (VMCS unusable bit set, present cleared).
 *  - Guest selectors are preserved; host selectors are sanitized
 *    (selector & 0xFFF8u) during VmxPlanHostState emission, not here.
 *  - Non-canonical bases are rejected.
 *  - Validation mutates IsValid and descriptor normalization fields
 *    (access_rights, present) but leaves selector values unchanged.
 *
 * Returns true if the state is valid for VMX entry.
 *===========================================================================*/
bool VmxValidateProcessorState(VMX_PROCESSOR_STATE* state);

/* VmxAsmCaptureSelectorsAndTables — implemented in src/driver/vmx_capture.asm */
void VmxAsmCaptureSelectorsAndTables(VMX_RAW_PROCESSOR_STATE* state);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PROCESSOR_STATE_H */
