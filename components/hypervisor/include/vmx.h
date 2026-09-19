/*
 * vmx.h — VMX instruction wrappers, VMCS field encodings, and VMX constants
 *
 * This header is WDK-independent and usable from both kernel-mode driver code
 * and user-mode unit tests that link hypervisor_core.
 */

#ifndef HYPERSINV_VMX_H
#define HYPERSINV_VMX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * MSR Addresses
 *===========================================================================*/

#define IA32_VMX_BASIC              0x480
#define IA32_VMX_PINBASED_CTLS      0x481
#define IA32_VMX_PROCBASED_CTLS     0x482
#define IA32_VMX_EXIT_CTLS          0x483
#define IA32_VMX_ENTRY_CTLS         0x484
#define IA32_VMX_MISC               0x485
#define IA32_VMX_CR0_FIXED0         0x486
#define IA32_VMX_CR0_FIXED1         0x487
#define IA32_VMX_CR4_FIXED0         0x488
#define IA32_VMX_CR4_FIXED1         0x489
#define IA32_VMX_PROCBASED_CTLS2    0x48B
#define IA32_VMX_EPT_VPID_CAP       0x48C
#define IA32_VMX_TRUE_PINBASED_CTLS 0x48D
#define IA32_VMX_TRUE_PROCBASED_CTLS 0x48E
#define IA32_VMX_TRUE_EXIT_CTLS     0x48F
#define IA32_VMX_TRUE_ENTRY_CTLS    0x490
#define IA32_VMX_VMFUNC             0x491

#define IA32_DEBUGCTL               0x1D9
#define IA32_FEATURE_CONTROL        0x3A
#define IA32_EFER                   0xC0000080
#define IA32_SYSENTER_CS            0x174
#define IA32_SYSENTER_ESP           0x175
#define IA32_SYSENTER_EIP           0x176
#define IA32_PAT                    0x277
#define IA32_STAR                   0xC0000081
#define IA32_LSTAR                  0xC0000082
#define IA32_CSTAR                  0xC0000083
#define IA32_FMASK                  0xC0000084
#define IA32_FS_BASE                0xC0000100
#define IA32_GS_BASE                0xC0000101

/* CET MSRs (Control-flow Enforcement Technology) */
#define IA32_U_CET                  0x6A0
#define IA32_S_CET                  0x6A2
#define IA32_PL0_SSP                0x6A4
#define IA32_PL1_SSP                0x6A5
#define IA32_PL2_SSP                0x6A6
#define IA32_PL3_SSP                0x6A7
#define IA32_INTERRUPT_SSP_TABLE_ADDR 0x6A8

/* Hyper-V synthetic MSR range */
#define HV_X64_MSR_GUEST_OS_ID      0x40000000
#define HV_SYNTHETIC_MSR_START      0x40000000
#define HV_SYNTHETIC_MSR_END        0x400000FF

/* VMX capability MSR range (all VMX MSRs return 0 for stealth) */
#define VMX_CAP_MSR_START           0x480
#define VMX_CAP_MSR_END             0x493

/*===========================================================================
 * IA32_VMX_BASIC bit layout
 *===========================================================================*/

#define VMX_BASIC_REVISION_MASK     0x7FFFFFFFull  /* bits 30:0 */
#define VMX_BASIC_VMCS_SIZE_SHIFT   32
#define VMX_BASIC_VMCS_SIZE_MASK    0x1FFFull       /* bits 44:32 (13 bits) */
#define VMX_BASIC_MEMORY_TYPE_MASK  0x003C000000000000ull  /* bits 53:50 */
#define VMX_BASIC_MEMORY_TYPE_SHIFT 50
#define VMX_BASIC_MEMORY_TYPE_WB    6               /* Write-back cache type */
#define VMX_BASIC_TRUE_CONTROLS     (1ull << 55)
#define VMX_BASIC_PHYS_ADDR_32      (1ull << 48)    /* 32-bit physical address restriction */
#define VMX_BASIC_RESERVED_BIT31    (1ull << 31)    /* Must be zero */

/*===========================================================================
 * VMX_BASIC_CAPABILITIES — Parsed IA32_VMX_BASIC MSR fields
 *===========================================================================*/
typedef struct _VMX_BASIC_CAPABILITIES {
    uint32_t revision_id;        /* bits 30:0 of IA32_VMX_BASIC */
    uint32_t vmcs_size;          /* bytes (from bits 44:32) */
    bool     restricts_to_32bit; /* bit 48: physical addresses limited to 32 bits */
    uint8_t  memory_type;        /* bits 53:50 (IA32_VMX_BASIC[53:50]) */
    bool     has_true_controls;  /* bit 55: IA32_VMX_TRUE_* MSRs available */
    uint64_t HighestPhysicalAddress; /* inclusive physical-address limit */
    bool     is_valid;           /* overall parse succeeded */
} VMX_BASIC_CAPABILITIES;

typedef enum _VMX_BASIC_PARSE_RESULT {
    VMX_BASIC_PARSE_VALID = 0,
    VMX_BASIC_PARSE_INVALID_NULL_CAPABILITIES,
    VMX_BASIC_PARSE_INVALID_RESERVED_BIT,
    VMX_BASIC_PARSE_INVALID_VMCS_SIZE,
    VMX_BASIC_PARSE_INVALID_MEMORY_TYPE
} VMX_BASIC_PARSE_RESULT;

/*===========================================================================
 * VMCS Field Encodings (Intel SDM Vol 3, Appendix B)
 *
 * Format: bits 14:13 = width (0=16-bit, 1=64-bit, 2=32-bit, 3=natural)
 *         bit  15    = 0 (must be 0 for valid encodings)
 *         bits 9:1   = field index
 *         bits 11:10 = field type (0=control, 1=VM-exit read-only, 2=guest, 3=host)
 *===========================================================================*/

/* 16-bit Control Fields */
#define VMCS_CTRL_VPID                      0x00000000u
#define VMCS_CTRL_PIN_BASED                 0x00004000u
#define VMCS_CTRL_CPU_BASED                 0x00004002u
#define VMCS_CTRL_EXCEPTION_BITMAP          0x00004004u
#define VMCS_CTRL_PF_ERROR_CODE_MASK        0x00004006u
#define VMCS_CTRL_PF_ERROR_CODE_MATCH       0x00004008u
#define VMCS_CTRL_CR3_TARGET_COUNT          0x0000400Au
#define VMCS_CTRL_VM_EXIT_CONTROLS          0x0000400Cu
#define VMCS_CTRL_VM_EXIT_MSR_STORE_COUNT   0x0000400Eu
#define VMCS_CTRL_VM_EXIT_MSR_LOAD_COUNT    0x00004010u
#define VMCS_CTRL_VM_ENTRY_CONTROLS         0x00004012u
#define VMCS_CTRL_VM_ENTRY_MSR_LOAD_COUNT   0x00004014u
#define VMCS_CTRL_VM_ENTRY_INT_INFO         0x00004016u
#define VMCS_CTRL_VM_ENTRY_EXCEPTION_ERR    0x00004018u
#define VMCS_CTRL_VM_ENTRY_INSTR_LENGTH     0x0000401Au
#define VMCS_CTRL_TPR_THRESHOLD             0x0000401Cu
#define VMCS_CTRL_SECONDARY                 0x0000401Eu

/* 64-bit Control Fields */
#define VMCS_CTRL_IO_BITMAP_A_ADDR          0x00002000u
#define VMCS_CTRL_IO_BITMAP_B_ADDR          0x00002002u
#define VMCS_CTRL_MSR_BITMAP_ADDR           0x00002004u
#define VMCS_CTRL_VM_EXIT_MSR_STORE_ADDR    0x00002006u
#define VMCS_CTRL_VM_EXIT_MSR_LOAD_ADDR     0x00002008u
#define VMCS_CTRL_VM_ENTRY_MSR_LOAD_ADDR    0x0000200Au
#define VMCS_CTRL_EXECUTIVE_VMCS_PTR        0x0000200Cu
#define VMCS_CTRL_PML_ADDR                  0x0000200Eu
#define VMCS_CTRL_TSC_OFFSET                0x00002010u
#define VMCS_CTRL_VIRTUAL_APIC_ADDR         0x00002012u
#define VMCS_CTRL_APIC_ACCESS_ADDR          0x00002014u
#define VMCS_CTRL_POSTED_INTR_DESC_ADDR     0x00002016u
#define VMCS_CTRL_VM_FUNCTION_CTLS          0x00002018u
#define VMCS_CTRL_EPTP                      0x0000201Au
#define VMCS_CTRL_EOI_EXIT_BITMAP_0         0x0000201Cu
#define VMCS_CTRL_EOI_EXIT_BITMAP_1         0x0000201Eu
#define VMCS_CTRL_EOI_EXIT_BITMAP_2         0x00002020u
#define VMCS_CTRL_EOI_EXIT_BITMAP_3         0x00002022u

/* Natural-Width Control Fields */
#define VMCS_CTRL_CR0_GUEST_HOST_MASK        0x00006000u
#define VMCS_CTRL_CR4_GUEST_HOST_MASK        0x00006002u
#define VMCS_CTRL_CR0_READ_SHADOW            0x00006004u
#define VMCS_CTRL_CR4_READ_SHADOW            0x00006006u

/* Natural-Width Guest-State Fields */
#define VMCS_GUEST_CR0                      0x00006800u
#define VMCS_GUEST_CR3                      0x00006802u
#define VMCS_GUEST_CR4                      0x00006804u
#define VMCS_GUEST_ES_BASE                  0x00006806u
#define VMCS_GUEST_CS_BASE                  0x00006808u
#define VMCS_GUEST_SS_BASE                  0x0000680Au
#define VMCS_GUEST_DS_BASE                  0x0000680Cu
#define VMCS_GUEST_FS_BASE                  0x0000680Eu
#define VMCS_GUEST_GS_BASE                  0x00006810u
#define VMCS_GUEST_LDTR_BASE                0x00006812u
#define VMCS_GUEST_TR_BASE                  0x00006814u
#define VMCS_GUEST_GDTR_BASE                0x00006816u
#define VMCS_GUEST_IDTR_BASE                0x00006818u
#define VMCS_GUEST_DR7                      0x0000681Au
#define VMCS_GUEST_RSP                      0x0000681Cu
#define VMCS_GUEST_RIP                      0x0000681Eu
#define VMCS_GUEST_RFLAGS                   0x00006820u
#define VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS 0x00006822u
#define VMCS_GUEST_IA32_SYSENTER_ESP        0x00006824u
#define VMCS_GUEST_IA32_SYSENTER_EIP        0x00006826u
#define VMCS_GUEST_S_CET                    0x00006828u
#define VMCS_GUEST_SSP                      0x0000682Au
#define VMCS_GUEST_INTR_SSP_TABLE           0x0000682Cu

/* 32-bit Guest-State Fields */
#define VMCS_GUEST_ES_LIMIT                 0x00004800u
#define VMCS_GUEST_CS_LIMIT                 0x00004802u
#define VMCS_GUEST_SS_LIMIT                 0x00004804u
#define VMCS_GUEST_DS_LIMIT                 0x00004806u
#define VMCS_GUEST_FS_LIMIT                 0x00004808u
#define VMCS_GUEST_GS_LIMIT                 0x0000480Au
#define VMCS_GUEST_LDTR_LIMIT               0x0000480Cu
#define VMCS_GUEST_TR_LIMIT                 0x0000480Eu
#define VMCS_GUEST_GDTR_LIMIT               0x00004810u
#define VMCS_GUEST_IDTR_LIMIT               0x00004812u
#define VMCS_GUEST_ES_ACCESS                0x00004814u
#define VMCS_GUEST_CS_ACCESS                0x00004816u
#define VMCS_GUEST_SS_ACCESS                0x00004818u
#define VMCS_GUEST_DS_ACCESS                0x0000481Au
#define VMCS_GUEST_FS_ACCESS                0x0000481Cu
#define VMCS_GUEST_GS_ACCESS                0x0000481Eu
#define VMCS_GUEST_LDTR_ACCESS              0x00004820u
#define VMCS_GUEST_TR_ACCESS                0x00004822u
#define VMCS_GUEST_INTERRUPTIBILITY_STATE   0x00004824u
#define VMCS_GUEST_ACTIVITY_STATE           0x00004826u
#define VMCS_GUEST_SMBASE                   0x00004828u
#define VMCS_GUEST_IA32_SYSENTER_CS         0x0000482Au
#define VMCS_GUEST_VMX_PREEMPTION_TIMER     0x0000482Eu

/* 16-bit Guest-State Fields */
#define VMCS_GUEST_ES_SELECTOR              0x00000800u
#define VMCS_GUEST_CS_SELECTOR              0x00000802u
#define VMCS_GUEST_SS_SELECTOR              0x00000804u
#define VMCS_GUEST_DS_SELECTOR              0x00000806u
#define VMCS_GUEST_FS_SELECTOR              0x00000808u
#define VMCS_GUEST_GS_SELECTOR              0x0000080Au
#define VMCS_GUEST_LDTR_SELECTOR            0x0000080Cu
#define VMCS_GUEST_TR_SELECTOR              0x0000080Eu

/* Natural-Width Host-State Fields */
#define VMCS_HOST_CR0                       0x00006C00u
#define VMCS_HOST_CR3                       0x00006C02u
#define VMCS_HOST_CR4                       0x00006C04u
#define VMCS_HOST_FS_BASE                   0x00006C06u
#define VMCS_HOST_GS_BASE                   0x00006C08u
#define VMCS_HOST_TR_BASE                   0x00006C0Au
#define VMCS_HOST_GDTR_BASE                 0x00006C0Cu
#define VMCS_HOST_IDTR_BASE                 0x00006C0Eu
#define VMCS_HOST_IA32_SYSENTER_ESP         0x00006C10u
#define VMCS_HOST_IA32_SYSENTER_EIP         0x00006C12u
#define VMCS_HOST_RSP                       0x00006C14u
#define VMCS_HOST_RIP                       0x00006C16u
#define VMCS_HOST_S_CET                     0x00006C18u
#define VMCS_HOST_SSP                       0x00006C1Au
#define VMCS_HOST_INTR_SSP_TABLE            0x00006C1Cu

/* 16-bit Host-State Fields */
#define VMCS_HOST_ES_SELECTOR               0x00000C00u
#define VMCS_HOST_CS_SELECTOR               0x00000C02u
#define VMCS_HOST_SS_SELECTOR               0x00000C04u
#define VMCS_HOST_DS_SELECTOR               0x00000C06u
#define VMCS_HOST_FS_SELECTOR               0x00000C08u
#define VMCS_HOST_GS_SELECTOR               0x00000C0Au
#define VMCS_HOST_TR_SELECTOR               0x00000C0Cu

/* 64-bit Guest-State (continued) */
#define VMCS_GUEST_VMCS_LINK_POINTER        0x00002800u
#define VMCS_GUEST_IA32_DEBUGCTL            0x00002802u
#define VMCS_GUEST_IA32_PAT                 0x00002804u
#define VMCS_GUEST_IA32_EFER                0x00002806u
#define VMCS_GUEST_IA32_PERF_GLOBAL_CTRL    0x00002808u
#define VMCS_GUEST_PDPTE0                   0x0000280Au
#define VMCS_GUEST_PDPTE1                   0x0000280Cu
#define VMCS_GUEST_PDPTE2                   0x0000280Eu
#define VMCS_GUEST_PDPTE3                   0x00002810u
#define VMCS_GUEST_IA32_BNDCFGS             0x00002812u
#define VMCS_GUEST_IA32_RTIT_CTL            0x00002814u

/* 64-bit Host-State Fields */
#define VMCS_HOST_IA32_PAT                  0x00002C00u
#define VMCS_HOST_IA32_EFER                 0x00002C02u
#define VMCS_HOST_IA32_PERF_GLOBAL_CTRL     0x00002C04u

/* 32-bit Host-State Fields */
#define VMCS_HOST_IA32_SYSENTER_CS          0x00004C00u

/*===========================================================================
 * Pin-Based VM-Execution Controls (bit positions in VMCS_CTRL_PIN_BASED)
 *===========================================================================*/

#define PIN_BASED_EXT_INTR_EXITING           (1u << 0)
#define PIN_BASED_NMI_EXITING                (1u << 3)
#define PIN_BASED_VIRTUAL_NMI                (1u << 5)
#define PIN_BASED_ACTIVATE_PREEMPTION_TIMER  (1u << 6)
#define PIN_BASED_PROCESS_POSTED_INTR        (1u << 7)

/*===========================================================================
 * Primary Processor-Based VM-Execution Controls (VMCS_CTRL_CPU_BASED)
 *===========================================================================*/

#define CPU_BASED_INTR_WINDOW_EXITING        (1u << 2)
#define CPU_BASED_USE_TSC_OFFSETTING         (1u << 3)
#define CPU_BASED_HLT_EXITING                (1u << 7)
#define CPU_BASED_INVLPG_EXITING             (1u << 9)
#define CPU_BASED_MWAIT_EXITING              (1u << 10)
#define CPU_BASED_RDPMC_EXITING              (1u << 11)
#define CPU_BASED_RDTSC_EXITING              (1u << 12)
#define CPU_BASED_CR3_LOAD_EXITING           (1u << 15)
#define CPU_BASED_CR3_STORE_EXITING          (1u << 16)
#define CPU_BASED_CR8_LOAD_EXITING           (1u << 19)
#define CPU_BASED_CR8_STORE_EXITING          (1u << 20)
#define CPU_BASED_USE_TPR_SHADOW             (1u << 21)
#define CPU_BASED_NMI_WINDOW_EXITING         (1u << 22)
#define CPU_BASED_MOV_DR_EXITING             (1u << 23)
#define CPU_BASED_UNCOND_IO_EXITING          (1u << 24)
#define CPU_BASED_USE_IO_BITMAPS             (1u << 25)
#define CPU_BASED_MONITOR_TRAP_FLAG          (1u << 27)
#define CPU_BASED_USE_MSR_BITMAPS            (1u << 28)
#define CPU_BASED_MONITOR_EXITING            (1u << 29)
#define CPU_BASED_PAUSE_EXITING              (1u << 30)
#define CPU_BASED_ACTIVATE_SECONDARY_CTLS    (1u << 31)

/*===========================================================================
 * Secondary Processor-Based VM-Execution Controls (VMCS_CTRL_SECONDARY)
 *===========================================================================*/

#define SECONDARY_VIRTUALIZE_APIC_ACCESSES   (1u << 0)
#define SECONDARY_ENABLE_EPT                 (1u << 1)
#define SECONDARY_DESCRIPTOR_TABLE_EXITING   (1u << 2)
#define SECONDARY_ENABLE_RDTSCP              (1u << 3)
#define SECONDARY_VIRTUALIZE_X2APIC_MODE     (1u << 4)
#define SECONDARY_ENABLE_VPID                (1u << 5)
#define SECONDARY_WBINVD_EXITING             (1u << 6)
#define SECONDARY_UNRESTRICTED_GUEST         (1u << 7)
#define SECONDARY_APIC_REGISTER_VIRT         (1u << 8)
#define SECONDARY_VIRTUAL_INTR_DELIVERY      (1u << 9)
#define SECONDARY_PAUSE_LOOP_EXITING         (1u << 10)
#define SECONDARY_RDRAND_EXITING             (1u << 11)
#define SECONDARY_ENABLE_INVPCID             (1u << 12)
#define SECONDARY_ENABLE_VM_FUNCTIONS        (1u << 13)
#define SECONDARY_VMCS_SHADOWING             (1u << 14)
#define SECONDARY_ENABLE_ENCLS_EXITING       (1u << 15)
#define SECONDARY_RDSEED_EXITING             (1u << 16)
#define SECONDARY_ENABLE_PML                 (1u << 17)
#define SECONDARY_EPT_VIOLATION_VE           (1u << 18)
#define SECONDARY_ENABLE_XSAVES_XRSTORS      (1u << 20)
#define SECONDARY_MODE_BASED_EXEC_EPT        (1u << 22)
#define SECONDARY_USE_TSC_SCALING            (1u << 25)

/* CET-related secondary controls (present on 11th-gen+ Intel) */
/* NOTE: CET load/save bits are in VM-EXIT and VM-ENTRY controls, NOT secondary proc-based.
   See VM_EXIT_LOAD_CET_STATE below and VM_ENTRY_LOAD_CET_STATE in entry controls. */

/*===========================================================================
 * VM-Exit Controls (VMCS_CTRL_VM_EXIT_CONTROLS)
 *===========================================================================*/

#define VM_EXIT_SAVE_DEBUG_CONTROLS          (1u << 2)
#define VM_EXIT_HOST_ADDR_SPACE_SIZE         (1u << 9)
#define VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL   (1u << 12)
#define VM_EXIT_ACK_INTR_ON_EXIT             (1u << 15)
#define VM_EXIT_SAVE_IA32_PAT                (1u << 18)
#define VM_EXIT_LOAD_IA32_PAT                (1u << 19)
#define VM_EXIT_SAVE_IA32_EFER               (1u << 20)
#define VM_EXIT_LOAD_IA32_EFER               (1u << 21)
#define VM_EXIT_SAVE_VMX_PREEMPTION_TIMER    (1u << 22)
#define VM_EXIT_LOAD_CET_STATE               (1u << 28)

/*===========================================================================
 * VM-Entry Controls (VMCS_CTRL_VM_ENTRY_CONTROLS)
 *===========================================================================*/

#define VM_ENTRY_LOAD_DEBUG_CONTROLS         (1u << 2)
#define VM_ENTRY_IA32E_MODE_GUEST            (1u << 9)
#define VM_ENTRY_ENTRY_SMM                   (1u << 10)
#define VM_ENTRY_DEACTIVATE_DUAL_MONITOR     (1u << 11)
#define VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL  (1u << 13)
#define VM_ENTRY_LOAD_IA32_PAT               (1u << 14)
#define VM_ENTRY_LOAD_IA32_EFER              (1u << 15)
#define VM_ENTRY_LOAD_IA32_BNDCFGS           (1u << 16)
#define VM_ENTRY_CONCEAL_VMX_FROM_PT         (1u << 17)

/* CET: bit 20 — Load guest CET state on VM entry. */
#define VM_ENTRY_LOAD_CET_STATE              (1u << 20)

/*===========================================================================
 * VM-Exit Reason Codes (basic exit reason from VMCS 0x00004402)
 *===========================================================================*/

#define EXIT_REASON_EXCEPTION_NMI            0
#define EXIT_REASON_EXTERNAL_INTERRUPT       1
#define EXIT_REASON_TRIPLE_FAULT             2
#define EXIT_REASON_INIT                     3
#define EXIT_REASON_SIPI                     4
#define EXIT_REASON_IO_SMI                   5
#define EXIT_REASON_OTHER_SMI                6
#define EXIT_REASON_INTR_WINDOW              7
#define EXIT_REASON_NMI_WINDOW               8
#define EXIT_REASON_TASK_SWITCH              9
#define EXIT_REASON_CPUID                    10
#define EXIT_REASON_GETSEC                   11
#define EXIT_REASON_HLT                      12
#define EXIT_REASON_INVD                     13
#define EXIT_REASON_INVLPG                   14
#define EXIT_REASON_RDPMC                    15
#define EXIT_REASON_RDTSC                    16
#define EXIT_REASON_RSM                      17
#define EXIT_REASON_VMCALL                   18
#define EXIT_REASON_VMCLEAR                  19
#define EXIT_REASON_VMLAUNCH                 20
#define EXIT_REASON_VMPTRLD                  21
#define EXIT_REASON_VMPTRST                  22
#define EXIT_REASON_VMREAD                   23
#define EXIT_REASON_VMWRITE                  24
#define EXIT_REASON_VMOFF                    25
#define EXIT_REASON_VMON                     26
#define EXIT_REASON_CR_ACCESS                28
#define EXIT_REASON_DR_ACCESS                29
#define EXIT_REASON_IO_INSTRUCTION           30
#define EXIT_REASON_MSR_READ                 31
#define EXIT_REASON_MSR_WRITE                32
#define EXIT_REASON_INVALID_GUEST_STATE      33
#define EXIT_REASON_MSR_LOADING              34
#define EXIT_REASON_MWAIT_INSTRUCTION        36
#define EXIT_REASON_MONITOR_TRAP_FLAG        37
#define EXIT_REASON_MONITOR_INSTRUCTION      39
#define EXIT_REASON_PAUSE_INSTRUCTION        40
#define EXIT_REASON_MCE_DURING_ENTRY         41
#define EXIT_REASON_TPR_BELOW_THRESHOLD      43
#define EXIT_REASON_APIC_ACCESS              44
#define EXIT_REASON_EOI_INDUCED              45
#define EXIT_REASON_GDTR_IDTR                46
#define EXIT_REASON_LDTR_TR                  47
#define EXIT_REASON_EPT_VIOLATION            48
#define EXIT_REASON_EPT_MISCONFIG            49
#define EXIT_REASON_INVEPT                   50
#define EXIT_REASON_RDTSCP                   51
#define EXIT_REASON_VMX_PREEMPTION_TIMER     52
#define EXIT_REASON_INVVPID                  53
#define EXIT_REASON_WBINVD                   54
#define EXIT_REASON_XSETBV                   55
#define EXIT_REASON_APIC_WRITE               56
#define EXIT_REASON_RDRAND                   57
#define EXIT_REASON_INVPCID                  58
#define EXIT_REASON_VMFUNC                   59
#define EXIT_REASON_ENCLS                    60
#define EXIT_REASON_RDSEED                   61
#define EXIT_REASON_PML_FULL                 62
#define EXIT_REASON_XSAVES                   63
#define EXIT_REASON_XRSTORS                  64
#define EXIT_REASON_UMWAIT                   67
#define EXIT_REASON_TPAUSE                   68

/*===========================================================================
 * VM-Exit Qualification for CR Access (exit reason 28)
 *===========================================================================*/

#define CR_ACCESS_TYPE_MOV_TO_CR             0
#define CR_ACCESS_TYPE_MOV_FROM_CR           1
#define CR_ACCESS_TYPE_CLTS                  2
#define CR_ACCESS_TYPE_LMSW                  3

/* CR number (bits 3:0 of exit qualification) */
#define CR_ACCESS_CR0                        0
#define CR_ACCESS_CR3                        3
#define CR_ACCESS_CR4                        4
#define CR_ACCESS_CR8                        8

/* CR access exit qualification masks (Intel SDM Vol 3, Section 27.2.1) */
#define CR_ACCESS_QUAL_CR_NUMBER_MASK        0x0000000F  /* bits 3:0 */
#define CR_ACCESS_QUAL_ACCESS_TYPE_MASK      0x00000030  /* bits 5:4 */
#define CR_ACCESS_QUAL_ACCESS_TYPE_SHIFT     4
#define CR_ACCESS_QUAL_GPR_MASK              0x00000F00  /* bits 11:8 */
#define CR_ACCESS_QUAL_GPR_SHIFT             8

/* Alternative names used by task specs (aliases to CR_ACCESS_TYPE_*) */
#define CR_ACCESS_MOV_TO_CR                  CR_ACCESS_TYPE_MOV_TO_CR
#define CR_ACCESS_MOV_FROM_CR                CR_ACCESS_TYPE_MOV_FROM_CR
#define CR_ACCESS_CLTS                       CR_ACCESS_TYPE_CLTS
#define CR_ACCESS_LMSW                       CR_ACCESS_TYPE_LMSW

/* CR number aliases */
#define CR_NUMBER_CR0                        CR_ACCESS_CR0
#define CR_NUMBER_CR3                        CR_ACCESS_CR3
#define CR_NUMBER_CR4                        CR_ACCESS_CR4

/*===========================================================================
 * CR0 / CR4 Bits
 *===========================================================================*/

#define CR0_PE                               (1ull << 0)
#define CR0_MP                               (1ull << 1)
#define CR0_NE                               (1ull << 5)
#define CR0_WP                               (1ull << 16)
#define CR0_PG                               (1ull << 31)

#define CR4_VME                              (1ull << 0)
#define CR4_PVI                              (1ull << 1)
#define CR4_TSD                              (1ull << 2)
#define CR4_DE                               (1ull << 3)
#define CR4_PSE                              (1ull << 4)
#define CR4_PAE                              (1ull << 5)
#define CR4_MCE                              (1ull << 6)
#define CR4_PGE                              (1ull << 7)
#define CR4_PCE                              (1ull << 8)
#define CR4_OSFXSR                           (1ull << 9)
#define CR4_OSXMMEXCPT                       (1ull << 10)
#define CR4_UMIP                             (1ull << 11)
#define CR4_LA57                             (1ull << 12)
#define CR4_VMXE                             (1ull << 13)
#define CR4_SMXE                             (1ull << 14)
#define CR4_FSGSBASE                         (1ull << 16)
#define CR4_PCIDE                            (1ull << 17)
#define CR4_OSXSAVE                          (1ull << 18)
#define CR4_SMEP                             (1ull << 20)
#define CR4_SMAP                             (1ull << 21)
#define CR4_PKE                              (1ull << 22)
#define CR4_CET                              (1ull << 23)

/*===========================================================================
 * IA32_EFER Bits
 *===========================================================================*/

#define EFER_SYSCALL_ENABLE                  (1ull << 0)
#define EFER_LONG_MODE_ENABLE                (1ull << 8)
#define EFER_LONG_MODE_ACTIVE                (1ull << 10)
#define EFER_EXECUTE_DISABLE                 (1ull << 11)

/*===========================================================================
 * IA32_FEATURE_CONTROL Bits
 *===========================================================================*/

#define FEATURE_CONTROL_LOCK_BIT             (1ull << 0)
#define FEATURE_CONTROL_VMXON_OUTSIDE_SMX    (1ull << 2)

/*===========================================================================
 * VMXON / VMCS Region Requirements
 *===========================================================================*/

#define VMX_REGION_ALIGNMENT                4096     /* 4KB alignment */
#define VMX_REGION_ALIGNMENT_MASK           (VMX_REGION_ALIGNMENT - 1)
#define VMX_REGION_MIN_SIZE                 VMX_REGION_ALIGNMENT

/*===========================================================================
 * VMX_REGION_VALIDATION — Result of region contract validation
 *===========================================================================*/
typedef enum _VMX_REGION_VALIDATION {
    VMX_REGION_VALID = 0,
    VMX_REGION_INVALID_NULL_CAPABILITIES,
    VMX_REGION_INVALID_CAPABILITIES,
    VMX_REGION_INVALID_NULL_ADDRESS,
    VMX_REGION_INVALID_VA_ALIGNMENT,
    VMX_REGION_INVALID_PA_ALIGNMENT,
    VMX_REGION_INVALID_PA_WIDTH,
    VMX_REGION_INVALID_SIZE_MISMATCH,
    VMX_REGION_INVALID_PA_EXTENT
} VMX_REGION_VALIDATION;

/*===========================================================================
 * VPID Constants
 *===========================================================================*/

#define VPID_INVALID                        0
#define VPID_DEFAULT                        1

/*===========================================================================
 * VMX Instruction Result Codes
 *===========================================================================*/

#define VMX_SUCCESS                         0
#define VMX_ERROR_INVALID                   (-1)
#define VMX_ERROR_VMXON                     (-2)
#define VMX_ERROR_VMCLEAR                   (-3)
#define VMX_ERROR_VMPTRLD                   (-4)
#define VMX_ERROR_VMLAUNCH                  (-5)
#define VMX_ERROR_VMRESUME                  (-6)
#define VMX_ERROR_NO_VMX                    (-7)
#define VMX_ERROR_ALIGNMENT                 (-8)
#define VMX_ERROR_NULL_POINTER              (-9)

/*===========================================================================
 * Segment Selector Constants
 *===========================================================================*/

#define SEG_SELECTOR_RPL0                   0
#define SEG_SELECTOR_RPL3                   3

/* CS access rights: long mode, ring 0, present, code, readable */
#define CS_ACCESS_RIGHTS_LM_R0              0xA09B
#define CS_ACCESS_RIGHTS_LM_R3              0xA0FB

/* DS/ES/SS access rights: ring 0, present, writable, data */
#define DS_ACCESS_RIGHTS_R0                 0xC093
#define DS_ACCESS_RIGHTS_R3                 0xC0F3

/* FS/GS access rights: ring 3, present, writable, data */
#define FS_ACCESS_RIGHTS_R3                 0xC0F3

/* TR access rights: present, busy TSS */
#define TR_ACCESS_RIGHTS                    0x008B

/*===========================================================================
 * VM-Exit Information Fields (read-only VMCS fields)
 *===========================================================================*/

#define VMCS_VM_INSTRUCTION_ERROR        0x00004400u
#define VMCS_VM_EXIT_REASON             0x00004402u
#define VMCS_EXIT_INTERRUPTION_INFO      0x00004404u
#define VMCS_EXIT_INTERRUPTION_ERROR     0x00004406u
#define VMCS_EXIT_IDT_VECTORING_INFO     0x00004408u
#define VMCS_EXIT_IDT_VECTORING_ERROR    0x0000440Au
#define VMCS_VM_EXIT_INSTRUCTION_LEN    0x0000440Cu
#define VMCS_VM_EXIT_INSTRUCTION_INFO   0x0000440Eu
#define VMCS_VM_EXIT_QUALIFICATION      0x00006400u

/*===========================================================================
 * MTRR-Related MSR Addresses
 *===========================================================================*/

#define IA32_MTRR_CAP                   0x000000FE
#define IA32_MTRR_DEF_TYPE              0x000002FF
#define IA32_MTRR_PHYSBASE0             0x00000200
#define IA32_MTRR_PHYSMASK0             0x00000201
#define IA32_MTRR_PHYSBASE_COUNT        10      /* Max variable MTRR pairs */

/* Fixed-range MTRRs (11 MSRs covering 0x00000-0xFFFFF) */
#define IA32_MTRR_FIX64K_00000          0x00000250
#define IA32_MTRR_FIX16K_80000          0x00000258
#define IA32_MTRR_FIX16K_A0000          0x00000259
#define IA32_MTRR_FIX4K_C0000           0x00000268
#define IA32_MTRR_FIX4K_C8000           0x00000269
#define IA32_MTRR_FIX4K_D0000           0x0000026A
#define IA32_MTRR_FIX4K_D8000           0x0000026B
#define IA32_MTRR_FIX4K_E0000           0x0000026C
#define IA32_MTRR_FIX4K_E8000           0x0000026D
#define IA32_MTRR_FIX4K_F0000           0x0000026E
#define IA32_MTRR_FIX4K_F8000           0x0000026F

/* MTRR DefType bits */
#define MTRR_DEF_TYPE_ENABLE            (1ull << 11)
#define MTRR_DEF_TYPE_FIXED_ENABLE      (1ull << 10)

/* MTRR PhysMask bit 11 = valid */
#define MTRR_PHYSMASK_VALID             (1ull << 11)

/* MTRR memory type constants (Intel SDM Vol 3, Table 13-8) */
#define MTRR_MT_UNCACHEABLE             0
#define MTRR_MT_WRITE_COMBINING         1
#define MTRR_MT_WRITE_THROUGH           4
#define MTRR_MT_WRITE_PROTECTED         5
#define MTRR_MT_WRITE_BACK              6

/*===========================================================================
 * VM-Exit Context Structure
 *
 * Passed from the assembly trampoline to the C handler. Contains guest
 * GPR values, the exit reason from VMCS, and the instruction length.
 * Handlers set `advance_rip` if the guest RIP should be forwarded past
 * the instruction that caused the exit.
 *===========================================================================*/

/* GPR indices in guest_gpr[] */
#define GPR_RAX   0
#define GPR_RCX   1
#define GPR_RDX   2
#define GPR_RBX   3
#define GPR_RSP   4
#define GPR_RBP   5
#define GPR_RSI   6
#define GPR_RDI   7
#define GPR_R8    8
#define GPR_R9    9
#define GPR_R10   10
#define GPR_R11   11
#define GPR_R12   12
#define GPR_R13   13
#define GPR_R14   14
#define GPR_R15   15

typedef struct {
    uint64_t guest_gpr[16];       /* RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7, R8-R15=8-15 */
    uint64_t guest_rip;
    uint64_t guest_rflags;
    uint32_t exit_reason;
    uint32_t instruction_length;
    uint64_t exit_qualification;
    bool     advance_rip;        /* Handler sets to true if RIP should be forwarded */
} vm_exit_context_t;

/*===========================================================================
 * Function Declarations
 *===========================================================================*/

/* Fast-path handlers */
int VmxHandleCpuid(vm_exit_context_t* ctx);
int VmxHandleHlt(vm_exit_context_t* ctx);
int VmxHandleRdtsc(vm_exit_context_t* ctx);

/* Stub handlers for exits not yet fully implemented */
int VmxHandleDefault(vm_exit_context_t* ctx);
int VmxHandleCrAccessFull(vm_exit_context_t* ctx);
int VmxHandleMsrRead(vm_exit_context_t* ctx);
int VmxHandleMsrWrite(vm_exit_context_t* ctx);
int VmxHandleEptViolation(vm_exit_context_t* ctx);
int VmxHandleVmcall(vm_exit_context_t* ctx);
int VmxHandleExceptionNmi(vm_exit_context_t* ctx);
int VmxHandleExternalInterrupt(vm_exit_context_t* ctx);
int VmxHandleTripleFault(vm_exit_context_t* ctx);

/* --- RIP Advancement helper --- */
void VmxAdvanceGuestRip(vm_exit_context_t* ctx);

/* --- VMX Detection & Capabilities --- */
bool     VmxDetectSupport(void);
uint32_t VmxReadVmcsRevision(void);
uint32_t VmxExtractRevisionId(uint64_t vmx_basic_msr);

/* --- VMX Region Management --- */
bool     VmxIsRegionAligned(const void* region);
int      VmxAllocateVmxonRegion(void** region_out, uint64_t* pa_out);
int      VmxAllocateVmcsRegion(void** region_out, uint64_t* pa_out);
void     VmxFreeRegion(void* region);

/* --- VMX Entry / Exit --- */
int      VmxEnterRoot(const void* vmxon_region_pa, const void* vmcs_region_pa);
int      VmxConfigureHostState(void);
int      VmxConfigureGuestState(void);
int      VmxConfigureControls(void);
int      VmxLaunch(void);
int      VmxLeaveRoot(void);

/* --- VMCS Field Access --- */
int      VmxReadVmcsField(uint32_t encoding, uint64_t* value);
int      VmxWriteVmcsField(uint32_t encoding, uint64_t value);

/* --- VMCS Field Encoding Verification --- */
uint32_t VmcsFieldEncoding_Width(uint32_t encoding);
uint32_t VmcsFieldEncoding_Type(uint32_t encoding);
uint32_t VmcsFieldEncoding_Index(uint32_t encoding);
bool     VmcsFieldEncoding_IsValid(uint32_t encoding);

/* --- Debug State --- */
uint32_t VmxGetCoreCount(void);

/* --- CPU Feature Detection (pure logic — operates on register values) --- */
bool CpuCheckVmxFromEcx(uint32_t ecx);
bool CpuCheckEptFromCtls2(uint64_t procbased_ctls2);
bool CpuCheckVpidFromCtls2(uint64_t procbased_ctls2);
bool CpuCheckUnrestrictedGuestFromCtls2(uint64_t procbased_ctls2);
bool CpuCheckCetFromLeaf7(uint32_t ecx, uint32_t edx);

/* --- CPUID Transparency (cpuid_handler.c) --- */
void cpuid_transparent_apply_stealth(
    uint32_t leaf, uint32_t subleaf,
    uint32_t* in_out_eax, uint32_t* in_out_ebx,
    uint32_t* in_out_ecx, uint32_t* in_out_edx);
int VmxHandleCpuid_Transparent(vm_exit_context_t* ctx);

/* --- MSR Handler Types & Functions (msr_handler.c) --- */
typedef enum {
    MSR_ACTION_NONE = 0,
    MSR_ACTION_RETURN_ZERO,
    MSR_ACTION_FEATURE_CONTROL_LOCKED,
    MSR_ACTION_DISCARD_WRITE,
} msr_action_t;

msr_action_t MsrLookupReadAction(uint32_t msr_addr);
msr_action_t MsrLookupWriteAction(uint32_t msr_addr);
int MsrBitmapByteOffset(uint32_t msr_addr);
int MsrBitmapBitPosition(uint32_t msr_addr, int* out_high_range);
void MsrBitmapSetInterceptBits(uint8_t* bitmap, uint32_t msr_addr,
                               int read_intercept, int write_intercept);
int VmxHandleMsrRead(vm_exit_context_t* ctx);
int VmxHandleMsrWrite(vm_exit_context_t* ctx);

/* --- CR3 Access Handler (cr3_handler.c) --- */
uint8_t CrExitQual_GetCrNumber(uint64_t exit_qualification);
uint8_t CrExitQual_GetAccessType(uint64_t exit_qualification);
uint8_t CrExitQual_GetGpr(uint64_t exit_qualification);
int VmxHandleCrAccessFull(vm_exit_context_t* ctx);

/* --- CET Suppression (cet_suppress.c) --- */
#define CET_MSR_COUNT 7
extern const uint32_t g_cet_msr_list[CET_MSR_COUNT];

/* --- EPT Violation Handling (ept_violation.c) --- */
/* Forward declaration of hook table (defined in ept.h) */
struct hook_table;
typedef struct hook_table hook_table_t;

uint8_t  EptViolationGetAccessType(uint64_t exit_qualification);
uint64_t EptViolationGetGpa(uint64_t exit_qualification);
uint64_t EptViolationGetGla(uint64_t exit_qualification);
int      EptHandleViolation(vm_exit_context_t* ctx, hook_table_t* hook_table);
int      EptHandleMisconfig(vm_exit_context_t* ctx);

/*===========================================================================
 * VMX_ADJUST_RESULT — Result of control field adjustment
 *===========================================================================*/
typedef struct VMX_ADJUST_RESULT {
    uint64_t value;             /* Adjusted control value */
    uint64_t missing_required;  /* Required bits that could not be set */
    bool     is_valid;          /* True if all constraints satisfied */
} VMX_ADJUST_RESULT;

/*===========================================================================
 * VMX_SEGMENT_DESC — Decoded segment descriptor from GDT
 *===========================================================================*/
typedef struct VMX_SEGMENT_DESC {
    uint64_t base;           /* Full segment base, including system base[63:32] */
    uint32_t limit;          /* Effective byte limit after granularity expansion */
    uint32_t access_rights;  /* VMCS access-rights format, including unusable bit */
    uint8_t  ar_bytes;       /* Compatibility view of access_rights[7:0] */
    uint8_t  dpl;            /* Descriptor Privilege Level (0-3) */
    uint8_t  type;           /* Segment type (bits 3:0 of access byte) */
    bool     present;        /* Present flag */
    bool     is_system;      /* Descriptor S bit is clear */
} VMX_SEGMENT_DESC;

#define VMX_SEGMENT_ACCESS_UNUSABLE  (1u << 16)

/*===========================================================================
 * HYPERVISOR_SIGNATURE — VMCALL ABI magic value
 *
 * "HVSRVN\x56" — 8-byte ASCII signature placed in RAX before VMCALL.
 *===========================================================================*/
#define HYPERVISOR_SIGNATURE  0x4853505253564E56ULL

/*===========================================================================
 * VMX Region Functions (vmx_region.c)
 *
 * Pure helpers for parsing VMX BASIC capabilities and validating
 * VMXON/VMCS region contracts. No VMX instructions or CR mutations.
 *===========================================================================*/

bool VmxParseBasicCapabilities(
    uint64_t vmx_basic,
    VMX_BASIC_CAPABILITIES* capabilities);

VMX_BASIC_PARSE_RESULT VmxParseBasicCapabilitiesResult(
    uint64_t vmx_basic,
    VMX_BASIC_CAPABILITIES* capabilities);

VMX_REGION_VALIDATION VmxValidateRegionContract(
    const VMX_BASIC_CAPABILITIES* capabilities,
    const void* virtual_address,
    uint64_t physical_address,
    size_t allocation_size);

bool VmxInitializeRegion(
    void* region,
    uint32_t revision_id,
    size_t region_size);

/*===========================================================================
 * Pure Helper Functions (static inline)
 *
 * These operate on register/msr values passed as parameters. No hardware
 * access — safe for unit testing with known test vectors.
 *===========================================================================*/

/*-----------------------------------------------------------------------------
 * VmxSelectTrueControl — Select between true and legacy control MSR values.
 *
 * IA32_VMX_BASIC[55] reports whether IA32_VMX_TRUE_*_CTLS MSRs exist. This
 * capability is independent of the primary control that activates secondary
 * processor-based controls.
 *-----------------------------------------------------------------------------*/
static inline uint64_t VmxSelectTrueControl(
    uint64_t vmx_basic,
    uint64_t msr_true,
    uint64_t msr_legacy)
{
    return (vmx_basic & VMX_BASIC_TRUE_CONTROLS) ? msr_true : msr_legacy;
}

/*-----------------------------------------------------------------------------
 * VmxAdjustControls — Adjust VM-execution control fields per hardware MSR
 * constraints.
 *
 * VMX capability MSR low bits (must_be_1): bit=1 means the control MUST be
 * 1; bit=0 permits the control to be 0.  VMX capability MSR high bits
 * (allowed_1): bit=1 permits the control to be 1; bit=0 requires it to be 0.
 *
 * The function computes the adjusted value by:
 *   1. Setting bits that MUST be 1 (where must_be_1 bit is 1)
 *   2. Clearing bits that MUST be 0 (where allowed_1 bit is 0)
 *   3. OR-ing in required_mask bits
 *
 * Returns invalid when required_mask bits cannot be satisfied (they conflict
 * with allowed_1=0 constraints).  The result is also invalid when a
 * capability MSR requires a bit to be 1 but does not allow that 1-setting.
 *-----------------------------------------------------------------------------*/
static inline VMX_ADJUST_RESULT VmxAdjustControls(
    uint64_t desired,
    uint64_t must_be_1,
    uint64_t allowed_1,
    uint64_t required_mask,
    const char* control_name)
{
    VMX_ADJUST_RESULT r = {0};
    (void)control_name;  /* Reserved for future diagnostic logging */

    /* Compute the hardware-adjusted value, then add our required bits. */
    r.value = desired;
    r.value |= must_be_1;         /* Low MSR bits require a 1-setting */
    r.value &= allowed_1;         /* High MSR bits forbid a 1-setting */
    r.value |= required_mask;     /* Add our own required bits */
    r.value &= allowed_1;         /* Re-clear forbidden required bits */

    /* Check required bits and capability-forced 1 bits independently. */
    r.missing_required = (required_mask | must_be_1) & ~r.value;
    r.is_valid = (r.missing_required == 0);

    return r;
}

/*-----------------------------------------------------------------------------
 * VmxComputeFixedCr0 — Apply CR0 fixed0/fixed1 constraints.
 *
 * IA32_VMX_CR0_FIXED0: bits set to 1 = always 1 in VMX operation.
 * IA32_VMX_CR0_FIXED1: bits set to 0 = always 0 in VMX operation.
 *
 * Result: (requested | fixed0) & fixed1
 *-----------------------------------------------------------------------------*/
static inline uint64_t VmxComputeFixedCr0(
    uint64_t requested,
    uint64_t cr0_fixed0,
    uint64_t cr0_fixed1)
{
    return (requested | cr0_fixed0) & cr0_fixed1;
}

/*-----------------------------------------------------------------------------
 * VmxComputeFixedCr4 — Apply CR4 fixed0/fixed1 constraints.
 *
 * Same logic as VmxComputeFixedCr0 but for CR4 MSRs:
 *   IA32_VMX_CR4_FIXED0 / IA32_VMX_CR4_FIXED1
 *-----------------------------------------------------------------------------*/
static inline uint64_t VmxComputeFixedCr4(
    uint64_t requested,
    uint64_t cr4_fixed0,
    uint64_t cr4_fixed1)
{
    return (requested | cr4_fixed0) & cr4_fixed1;
}

/*-----------------------------------------------------------------------------
 * VmxDecodeSegmentSelector — Decode a GDT descriptor entry.
 *
 * Reads the descriptor at gdt_base + (selector & 0xFFF8) and extracts its
 * effective limit and VMCS-format access rights. In IA-32e mode, LDT and TSS
 * descriptors occupy 16 bytes; their upper base is decoded from bytes 8-11.
 *
 * Returns an unusable descriptor for:
 *   - Null selector (0x0000-0x0003)
 *   - LDT selector (the function accepts a GDT base only)
 *   - Descriptor exceeding the inclusive GDTR limit
 *-----------------------------------------------------------------------------*/
static inline VMX_SEGMENT_DESC VmxDecodeSegmentSelector(
    uint16_t selector,
    const void* gdt_base,
    uint32_t gdt_limit)
{
    VMX_SEGMENT_DESC sd = {0};
    sd.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;

    /* Null selector (0x0000-0x0003 per Intel SDM Vol 3, Section 3.4.2) */
    if (selector <= 3) {
        return sd;
    }

    /* TI=1 selects the LDT, which cannot be resolved from a GDT base. */
    if ((selector & 0x0004u) != 0) {
        return sd;
    }

    /* GDTR.limit is the inclusive offset of the last valid GDT byte. */
    uint16_t offset = selector & 0xFFF8u;
    if ((uint32_t)offset + 7u > gdt_limit) {
        return sd;
    }

    /* Read the 8-byte descriptor */
    const uint8_t* gdt = (const uint8_t*)gdt_base;
    const uint8_t* entry = gdt + offset;

    /* Bytes 0-1: Segment Limit[15:0] */
    sd.limit  = (uint32_t)entry[0];
    sd.limit |= (uint32_t)entry[1] << 8;

    /* Bytes 2-3: Base Address[15:0] */
    sd.base  = (uint64_t)entry[2];
    sd.base |= (uint64_t)entry[3] << 8;

    /* Byte 4: Base Address[23:16] */
    sd.base |= (uint64_t)entry[4] << 16;

    /* Byte 5: Access byte */
    uint8_t access = entry[5];
    sd.present = (access & 0x80) != 0;
    sd.dpl     = (access >> 5) & 0x3;
    sd.type    = access & 0x0F;
    sd.ar_bytes = access;
    sd.is_system = (access & 0x10u) == 0;

    /* Byte 6: Flags (G|D/B|L|AVL) and Limit[19:16] */
    uint8_t flags = entry[6];
    sd.limit |= ((uint32_t)(flags & 0x0F)) << 16;
    sd.access_rights = (uint32_t)access | ((uint32_t)(flags & 0xF0u) << 8);

    if ((flags & 0x80u) != 0) {
        sd.limit = (sd.limit << 12) | 0xFFFu;
    }

    /* Byte 7: Base Address[31:24] */
    sd.base |= (uint64_t)entry[7] << 24;

    /* IA-32e LDT and available/busy TSS descriptors occupy two GDT slots. */
    if (sd.is_system &&
        (sd.type == 0x2u || sd.type == 0x9u || sd.type == 0xBu)) {
        if ((uint32_t)offset + 15u > gdt_limit) {
            VMX_SEGMENT_DESC unusable = {0};
            unusable.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE;
            return unusable;
        }

        sd.base |= (uint64_t)entry[8] << 32;
        sd.base |= (uint64_t)entry[9] << 40;
        sd.base |= (uint64_t)entry[10] << 48;
        sd.base |= (uint64_t)entry[11] << 56;
    }

    return sd;
}

/*-----------------------------------------------------------------------------
 * VmxIsCetEnabled — Check if Control-flow Enforcement Technology is active.
 *
 * Reads CR4 bit 23 (CR4_CET) via __readcr4() intrinsic.
 * NOTE: This function faults in user mode — use only in kernel-mode code.
 * In unit tests, test CR4_CET constant instead.
 *-----------------------------------------------------------------------------*/
#ifdef _KERNEL_MODE
#include <intrin.h>
static inline bool VmxIsCetEnabled(void)
{
    return (__readcr4() & CR4_CET) != 0;
}
#else
/* User-mode stub: always returns false. Real check only valid in kernel. */
static inline bool VmxIsCetEnabled(void)
{
    return false;
}
#endif

/*-----------------------------------------------------------------------------
 * VmxIsAddressCanonical — Check if a 64-bit address is in canonical form.
 *
 * On x64, bits 48-63 must all match bit 47 (sign extension).
 *-----------------------------------------------------------------------------*/
static inline bool VmxIsAddressCanonical(uint64_t address)
{
    uint64_t canonical = ((int64_t)(address << 16)) >> 16;
    return address == canonical;
}

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_H */
