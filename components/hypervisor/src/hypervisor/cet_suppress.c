/*
 * cet_suppress.c — CET suppression constants and MSR list (Task 3.5)
 *
 * Provides the CET MSR address list and CET-related VMCS field references.
 * The kernel-mode wrapper functions (CetSuppressInit, CetSuppressCr4Shadow)
 * VMWRITE the VMCS fields — these are kernel-mode only and not unit-testable.
 *
 * This file exposes the CET MSR list for test verification and for use by
 * the MSR bitmap configuration logic.
 */

#include "vmx.h"

/*===========================================================================
 * CET MSR List — all MSRs that must be intercepted for CET suppression
 *
 * IA32_U_CET (0x6A0): User-mode CET control
 * IA32_S_CET (0x6A2): Supervisor-mode CET control
 * IA32_PL0_SSP (0x6A4): Ring 0 shadow stack pointer
 * IA32_PL1_SSP (0x6A5): Ring 1 shadow stack pointer
 * IA32_PL2_SSP (0x6A6): Ring 2 shadow stack pointer
 * IA32_PL3_SSP (0x6A7): Ring 3 shadow stack pointer
 * IA32_INTERRUPT_SSP_TABLE_ADDR (0x6A8): Interrupt SSP table address
 *===========================================================================*/

const uint32_t g_cet_msr_list[CET_MSR_COUNT] = {
    IA32_U_CET,                   /* 0x6A0 */
    IA32_S_CET,                   /* 0x6A2 */
    IA32_PL0_SSP,                 /* 0x6A4 */
    IA32_PL1_SSP,                 /* 0x6A5 */
    IA32_PL2_SSP,                 /* 0x6A6 */
    IA32_PL3_SSP,                 /* 0x6A7 */
    IA32_INTERRUPT_SSP_TABLE_ADDR /* 0x6A8 */
};
