/*
 * cpuid_handler.c — CPUID transparency handler (Task 3.1)
 *
 * Pure-function CPUID masking for stealth:
 * - Leaf 1: Clear ECX bit 31 (hypervisor present)
 * - Leaf 0x40000000-0x400000FF: Zero all registers (no hypervisor vendor leaf)
 * - Leaf 7 subleaf 0: Clear ECX bit 7 (CET_SHSTK) and EDX bit 20 (CET_IBT)
 * - All other leaves: pass through unmodified
 */

#include "vmx.h"

/*===========================================================================
 * CPUID Transparency Constants
 *===========================================================================*/

#define CPUID_LEAF1_ECX_HYPERVISOR_BIT  31
#define CPUID_LEAF7_ECX_CET_SHSTK       7
#define CPUID_LEAF7_EDX_CET_IBT         20
#define CPUID_HYPERVISOR_LEAF_START     0x40000000
#define CPUID_HYPERVISOR_LEAF_END       0x400000FF

/*===========================================================================
 * cpuid_transparent_apply_stealth — Pure function (unit-testable)
 *
 * Modifies CPUID output registers in-place to hide hypervisor and CET
 * features from the guest. This is the core stealth logic — deterministic,
 * no side effects, fully testable without kernel mode.
 *===========================================================================*/

void cpuid_transparent_apply_stealth(
    uint32_t leaf, uint32_t subleaf,
    uint32_t* in_out_eax, uint32_t* in_out_ebx,
    uint32_t* in_out_ecx, uint32_t* in_out_edx)
{
    if (leaf == 1) {
        /* Leaf 1: Clear hypervisor-present bit (ECX bit 31).
         * All other bits pass through unmodified. */
        *in_out_ecx &= ~(1u << CPUID_LEAF1_ECX_HYPERVISOR_BIT);

    } else if (leaf >= CPUID_HYPERVISOR_LEAF_START &&
               leaf <= CPUID_HYPERVISOR_LEAF_END) {
        /* Hypervisor vendor leaves: zero all output registers.
         * Guest sees no hypervisor vendor string. */
        *in_out_eax = 0;
        *in_out_ebx = 0;
        *in_out_ecx = 0;
        *in_out_edx = 0;

    } else if (leaf == 7 && subleaf == 0) {
        /* Leaf 7 subleaf 0: Clear CET feature bits.
         * ECX bit 7 = CET_SHSTK (shadow stack)
         * EDX bit 20 = CET_IBT (indirect branch tracking)
         * All other bits pass through. */
        *in_out_ecx &= ~(1u << CPUID_LEAF7_ECX_CET_SHSTK);
        *in_out_edx &= ~(1u << CPUID_LEAF7_EDX_CET_IBT);

    }
    /* else: pass through — no modifications. */
}

/*===========================================================================
 * VmxHandleCpuid_Transparent — VM-Exit handler wrapper
 *
 * Called from the VM-Exit dispatch on EXIT_REASON_CPUID.
 * Reads guest RAX (leaf) and RCX (subleaf), executes host CPUID,
 * applies stealth masking via the pure function, and writes results
 * to guest registers.
 *===========================================================================*/

int VmxHandleCpuid_Transparent(vm_exit_context_t* ctx) {
    if (ctx == NULL) {
        return 0;
    }

    uint32_t leaf    = (uint32_t)ctx->guest_gpr[GPR_RAX];
    uint32_t subleaf = (uint32_t)ctx->guest_gpr[GPR_RCX];

#ifdef _KERNEL_MODE
    int cpu_info[4];
    __cpuidex(cpu_info, (int)leaf, (int)subleaf);
    uint32_t eax = (uint32_t)cpu_info[0];
    uint32_t ebx = (uint32_t)cpu_info[1];
    uint32_t ecx = (uint32_t)cpu_info[2];
    uint32_t edx = (uint32_t)cpu_info[3];
#else
    /* User-mode stub: return zeroed output (tests verify register overwrite) */
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    (void)leaf;
    (void)subleaf;
#endif

    /* Apply transparency masking (pure logic) */
    cpuid_transparent_apply_stealth(leaf, subleaf, &eax, &ebx, &ecx, &edx);

    /* Write results to guest context */
    ctx->guest_gpr[GPR_RAX] = eax;
    ctx->guest_gpr[GPR_RBX] = ebx;
    ctx->guest_gpr[GPR_RCX] = ecx;
    ctx->guest_gpr[GPR_RDX] = edx;

    ctx->advance_rip = true;
    return 0;
}
