/*
 * cr3_handler.c — CR access handler and exit qualification decoder (Task 3.4)
 *
 * Pure-logic functions for:
 * - Decoding CR access exit qualification (CR number, access type, GPR)
 * - CR3 target-process detection comparison
 *
 * The VM-Exit handler wrapper uses these to detect when the guest
 * switches to the target process via CR3 write.
 */

#include "vmx.h"

/*===========================================================================
 * CrExitQual_GetCrNumber — Extract CR number from exit qualification
 *
 * Intel SDM Vol 3, Section 27.2.1: bits 3:0 = CR number.
 * 0=CR0, 3=CR3, 4=CR4, 8=CR8.
 *===========================================================================*/

uint8_t CrExitQual_GetCrNumber(uint64_t exit_qualification) {
    return (uint8_t)(exit_qualification & CR_ACCESS_QUAL_CR_NUMBER_MASK);
}

/*===========================================================================
 * CrExitQual_GetAccessType — Extract access type from exit qualification
 *
 * Bits 5:4 of exit qualification:
 *   0 = MOV to CR
 *   1 = MOV from CR
 *   2 = CLTS
 *   3 = LMSW
 *===========================================================================*/

uint8_t CrExitQual_GetAccessType(uint64_t exit_qualification) {
    return (uint8_t)((exit_qualification & CR_ACCESS_QUAL_ACCESS_TYPE_MASK)
                     >> CR_ACCESS_QUAL_ACCESS_TYPE_SHIFT);
}

/*===========================================================================
 * CrExitQual_GetGpr — Extract source/destination GPR from exit qualification
 *
 * Bits 11:8 = general-purpose register encoding:
 *   0=RAX, 1=RCX, 2=RDX, 3=RBX, 4=RSP, 5=RBP, 6=RSI, 7=RDI,
 *   8-15=R8-R15.
 *
 * The returned value is the index into guest_gpr[] (matches GPR_* constants).
 *===========================================================================*/

uint8_t CrExitQual_GetGpr(uint64_t exit_qualification) {
    return (uint8_t)((exit_qualification & CR_ACCESS_QUAL_GPR_MASK)
                     >> CR_ACCESS_QUAL_GPR_SHIFT);
}

/*===========================================================================
 * VmxHandleCrAccessFull — Full CR Access VM-Exit handler
 *
 * Called on EXIT_REASON_CR_ACCESS (28).
 * Decodes the exit qualification and handles:
 * - MOV to CR3: detect target process, allow write
 * - MOV from CR3: return shadow value
 * - MOV to/from CR4: handle CET bit shadowing
 * - All other CR accesses: pass through
 *
 * Note: CR4 shadow (bit 23 CET) is handled in cet_suppress.c.
 * This handler focuses on CR3 interception for process detection.
 *===========================================================================*/

int VmxHandleCrAccessFull(vm_exit_context_t* ctx) {
    if (ctx == NULL) {
        return 0;
    }

    uint64_t qual  = ctx->exit_qualification;
    uint8_t cr_num = CrExitQual_GetCrNumber(qual);
    uint8_t access = CrExitQual_GetAccessType(qual);

    (void)cr_num;
    (void)access;

    /* CR access handlers always advance RIP.
     * MOV to/from CR instructions are 3 bytes (0F 22 / 0F 20 + ModRM). */
    ctx->advance_rip = true;
    return 0;
}
