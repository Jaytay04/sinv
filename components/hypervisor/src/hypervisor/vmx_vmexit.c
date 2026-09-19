/*
 * vmx_vmexit.c — Historical VM-Exit simulation model
 *
 * This file is compiled only by the legacy user-mode unit test. Production
 * dispatch is owned by vmx_persistent_dispatch.c and its explicit adapters.
 */

#include "vmx_legacy_vmexit.h"

/*===========================================================================
 * RIP Advancement Helper
 *
 * Adds the instruction length to guest RIP. Called by handlers that
 * need to advance past the exiting instruction (CPUID, RDTSC, MSR, etc.).
 *===========================================================================*/

void VmxAdvanceGuestRip(vm_exit_context_t* ctx) {
    if (ctx != NULL) {
        ctx->guest_rip += ctx->instruction_length;
    }
}

/*===========================================================================
 * Default / Unknown Exit Handler
 *
 * Logs unknown exits and resumes without RIP advancement.
 * This is a catch-all for exit reasons we don't explicitly handle.
 *===========================================================================*/

int VmxHandleDefault(vm_exit_context_t* ctx) {
    (void)ctx;  /* Unknown exit — just resume */
    return 0;
}

/*===========================================================================
 * Exception / NMI Handler (reason 0)
 *
 * Exceptions within the guest are currently not handled — we resume
 * the guest and let it handle its own exceptions. This is a passthrough.
 *===========================================================================*/

int VmxHandleExceptionNmi(vm_exit_context_t* ctx) {
    (void)ctx;
    return 0;
}

/*===========================================================================
 * External Interrupt Handler (reason 1)
 *
 * External interrupts are acknowledged by the CPU on VM-Exit when
 * "Acknowledge Interrupt on Exit" is set. We just resume — the guest
 * will handle the interrupt normally.
 *===========================================================================*/

int VmxHandleExternalInterrupt(vm_exit_context_t* ctx) {
    (void)ctx;
    return 0;
}

/*===========================================================================
 * Triple Fault Handler (reason 2)
 *
 * Triple fault means the guest is in an unrecoverable state.
 * In a real hypervisor this would trigger a shutdown or debug break.
 * Currently: just resume (for debugging purposes).
 *===========================================================================*/

int VmxHandleTripleFault(vm_exit_context_t* ctx) {
    (void)ctx;
    return 0;
}

/*===========================================================================
 * CPUID Fast-Path Handler (reason 10)
 *
 * Reads guest RAX (leaf) and guest RCX (sub-leaf), executes CPUID,
 * and writes results to guest RAX/RBX/RCX/RDX. Advances RIP.
 *
 * In kernel mode: uses __cpuidex intrinsic.
 * In user mode (test stub): returns zeroed output (test verifies
 * that GPRs are overwritten, not the specific values).
 *===========================================================================*/

int VmxHandleCpuid(vm_exit_context_t* ctx) {
    uint32_t leaf  = (uint32_t)ctx->guest_gpr[GPR_RAX];
    uint32_t subleaf = (uint32_t)ctx->guest_gpr[GPR_RCX];

#ifdef _KERNEL_MODE
    int cpu_info[4];
    __cpuidex(cpu_info, (int)leaf, (int)subleaf);
    ctx->guest_gpr[GPR_RAX] = (uint64_t)cpu_info[0];
    ctx->guest_gpr[GPR_RBX] = (uint64_t)cpu_info[1];
    ctx->guest_gpr[GPR_RCX] = (uint64_t)cpu_info[2];
    ctx->guest_gpr[GPR_RDX] = (uint64_t)cpu_info[3];
#else
    /* User-mode stub: return zeroed output.
     * The test verifies that GPRs are overwritten, which proves the
     * handler dispatched correctly and wrote results. */
    ctx->guest_gpr[GPR_RAX] = 0;
    ctx->guest_gpr[GPR_RBX] = 0;
    ctx->guest_gpr[GPR_RCX] = 0;
    ctx->guest_gpr[GPR_RDX] = 0;
    (void)leaf;
    (void)subleaf;
#endif

    ctx->advance_rip = true;
    return 0;
}

/*===========================================================================
 * HLT Passthrough Handler (reason 12)
 *
 * HLT does not advance RIP (it's a "pause" instruction, not a fault).
 * We simply resume the guest immediately — no work needed.
 *===========================================================================*/

int VmxHandleHlt(vm_exit_context_t* ctx) {
    (void)ctx;  /* Nothing to do — guest will resume at same RIP */
    /* Do NOT set advance_rip — HLT doesn't advance */
    return 0;
}

/*===========================================================================
 * RDTSC Fast-Path Handler (reason 16)
 *
 * Reads the Time-Stamp Counter via __rdtsc() and writes the value to
 * guest RAX (low 32 bits) and RDX (high 32 bits). Advances RIP.
 *
 * Note: By default we don't intercept RDTSC (MSR bitmap pass-through),
 * but the handler is ready if needed for stealth with TSC offset.
 *===========================================================================*/

int VmxHandleRdtsc(vm_exit_context_t* ctx) {
#ifdef _KERNEL_MODE
    uint64_t tsc = __rdtsc();
    ctx->guest_gpr[GPR_RAX] = (uint32_t)tsc;
    ctx->guest_gpr[GPR_RDX] = (uint32_t)(tsc >> 32);
#else
    /* User-mode stub: return zero.
     * Test verifies GPRs are overwritten, proving dispatch correctness. */
    ctx->guest_gpr[GPR_RAX] = 0;
    ctx->guest_gpr[GPR_RDX] = 0;
#endif

    ctx->advance_rip = true;
    return 0;
}

/*===========================================================================
 * CR Access Handler (reason 28)
 *
 * Full implementation in cr3_handler.c (VmxHandleCrAccessFull).
 * Handles MOV to/from CR instructions, CR3 interception for process
 * detection, and CR4 CET bit shadowing.
 *===========================================================================*/

extern int VmxHandleCrAccessFull(vm_exit_context_t* ctx);

/*===========================================================================
 * MSR Read/Write Handlers (reasons 31, 32)
 *
 * Full implementations in msr_handler.c (VmxHandleMsrRead, VmxHandleMsrWrite).
 * These use the MSR dispatch table to return stealth values for VMX caps,
 * CET MSRs, Hyper-V synthetic MSRs, and IA32_FEATURE_CONTROL.
 *===========================================================================*/

/*===========================================================================
 * EPT Violation Handler (reason 48)
 *
 * Full implementation in ept_violation.c. Decodes exit qualification,
 * looks up GPA in hook table, and applies hook actions.
 * Does NOT advance RIP — this is a fault-class exit (guest instruction
 * did not complete, will be re-executed after fixing EPT permissions).
 *
 * The hook_table is provided by the kernel-mode caller (core_mgmt.c).
 * For unit testing, a mock hook_table is passed directly.
 *===========================================================================*/

extern int EptHandleViolation(vm_exit_context_t* ctx, hook_table_t* hook_table);
extern int EptHandleMisconfig(vm_exit_context_t* ctx);

int VmxHandleEptViolation(vm_exit_context_t* ctx) {
    /* Pure-logic handler — the kernel-mode wrapper provides the hook_table.
     * For the pure-logic path (unit tests), we handle without a hook table
     * by passing NULL, which causes all violations to return "not hooked".
     */
    return EptHandleViolation(ctx, NULL);
}

/*===========================================================================
 * VMCALL Handler (reason 18)
 *
 * Handles VMCALL instructions from the guest. Currently a stub.
 * Advances RIP.
 *===========================================================================*/

int VmxHandleVmcall(vm_exit_context_t* ctx) {
    (void)ctx;
    ctx->advance_rip = true;
    return 0;
}

/*===========================================================================
 * Main VM-Exit Dispatch Switch
 *
 * Called from the assembly trampoline with the guest context.
 * Reads the exit reason from the context structure and dispatches
 * to the appropriate handler.
 *
 * The calling convention: the assembly trampoline saves all GPRs
 * into the guest_gpr[] array and sets exit_reason from VMCS.
 * This function dispatches based on exit_reason and sets advance_rip.
 * The trampoline then advances RIP if needed and VMRESUMEs.
 *===========================================================================*/

int VmxHandleExit(vm_exit_context_t* ctx) {
    if (ctx == NULL) {
        return -1;
    }

    /* Reset advance_rip — handlers set it if needed */
    ctx->advance_rip = false;

    switch (ctx->exit_reason) {

    case EXIT_REASON_EXCEPTION_NMI:
        return VmxHandleExceptionNmi(ctx);

    case EXIT_REASON_EXTERNAL_INTERRUPT:
        return VmxHandleExternalInterrupt(ctx);

    case EXIT_REASON_TRIPLE_FAULT:
        return VmxHandleTripleFault(ctx);

    case EXIT_REASON_CPUID:
        return VmxHandleCpuid_Transparent(ctx);

    case EXIT_REASON_HLT:
        return VmxHandleHlt(ctx);

    case EXIT_REASON_RDTSC:
        return VmxHandleRdtsc(ctx);

    case EXIT_REASON_VMCALL:
        return VmxHandleVmcall(ctx);

    case EXIT_REASON_CR_ACCESS:
        return VmxHandleCrAccessFull(ctx);

    case EXIT_REASON_MSR_READ:
        return VmxHandleMsrRead(ctx);

    case EXIT_REASON_MSR_WRITE:
        return VmxHandleMsrWrite(ctx);

    case EXIT_REASON_EPT_VIOLATION:
        return VmxHandleEptViolation(ctx);

    case EXIT_REASON_EPT_MISCONFIG:
        return EptHandleMisconfig(ctx);

    default:
        return VmxHandleDefault(ctx);
    }
}

/*===========================================================================
 * VPID Assignment and INVVPID (Task 2.7)
 *
 * VPID assignment strategy: VPID 1 = primary guest.
 * Per-core VPID = processor_number + 1.
 * VPID 0 is reserved (INVVPID uses it to mean "all contexts").
 *
 * INVVPID operations are kernel-mode only (privileged instructions).
 * User-mode tests verify the assignment logic.
 *===========================================================================*/

uint16_t VmxAssignVpid(uint32_t processor_number) {
    /* VPID 1-based: processor 0 gets VPID 1, processor N gets VPID N+1 */
    if (processor_number > 0xFFFE) {
        return VPID_INVALID;
    }
    return (uint16_t)(processor_number + 1);
}

int VmxInvVpidSingleContext(uint16_t vpid, uint64_t gpa) {
#ifdef _KERNEL_MODE
    /* INVVPID type 1: single-context invalidation.
     * Invalidate TLB entries for a specific VPID and GPA. */
    /* TODO: implement with __invvpid intrinsic when kernel mode is available */
    (void)vpid;
    (void)gpa;
    return 0;
#else
    (void)vpid;
    (void)gpa;
    return 0;
#endif
}

int VmxInvVpidAllContexts(void) {
#ifdef _KERNEL_MODE
    /* INVVPID type 2: all-contexts invalidation.
     * Flush all TLB entries regardless of VPID. */
    /* TODO: implement with __invvpid intrinsic when kernel mode is available */
    return 0;
#else
    return 0;
#endif
}
