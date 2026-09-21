#ifndef HYPERSINV_EFI_HYPERVISOR_H
#define HYPERSINV_EFI_HYPERVISOR_H

/*
 * Minimal persistent EFI VMX payload contract.
 *
 * The EFI entry point and all firmware-facing types are intentionally kept
 * in the payload translation unit.  This header contains the freestanding
 * VMX contract plus pure helpers used by the host tests.  No Windows, WDK,
 * or C run-time headers are required by the portable part of the contract.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_PAGE_SIZE UINT64_C(4096)
#define HV_EFI_PAGE_MASK (HV_EFI_PAGE_SIZE - UINT64_C(1))
#define HV_EFI_IDENTITY_MAP_LIMIT UINT64_C(0x40000000)
#define HV_EFI_MAX_PHYSICAL_ADDRESS UINT64_C(0x3fffffff)
#define HV_EFI_CONTEXT_PAGES UINT32_C(1)
#define HV_EFI_RESIDENT_CODE_PAGES UINT32_C(2)
#define HV_EFI_RUNTIME_SCANNER_CODE_PAGES UINT32_C(16)
#define HV_EFI_RUNTIME_SCANNER_STATE_PAGES UINT32_C(7)
#define HV_EFI_RUNTIME_SCANNER_RANGE_PAGES UINT32_C(4)
#define HV_EFI_RUNTIME_SCANNER_MAX_RAM_RANGES \
    ((HV_EFI_RUNTIME_SCANNER_RANGE_PAGES * HV_EFI_PAGE_SIZE) / \
     UINT32_C(16))
#define HV_EFI_GUEST_TRANSITION_CODE_PAGES UINT32_C(1)
#define HV_EFI_PROCESSOR_STATUS_AS_BSP UINT32_C(0x00000001)
#define HV_EFI_PROCESSOR_STATUS_ENABLED UINT32_C(0x00000002)
#define HV_EFI_PROCESSOR_STATUS_HEALTHY UINT32_C(0x00000004)
#define HV_EFI_MAX_SUPPORTED_PROCESSORS UINT32_C(64)
#define HV_EFI_AP_OPERATION_TIMEOUT_US UINT64_C(60000000)
#define HV_EFI_VMXON_PAGES UINT32_C(1)
#define HV_EFI_VMCS_PAGES UINT32_C(1)
#define HV_EFI_HOST_STACK_PAGES UINT32_C(4)
#define HV_EFI_GUEST_STACK_PAGES UINT32_C(1)
#define HV_EFI_PAGE_TABLE_PAGES UINT32_C(8)
#define HV_EFI_MSR_BITMAP_PAGE UINT32_C(3)
#define HV_EFI_IO_BITMAP_A_PAGE UINT32_C(4)
#define HV_EFI_IO_BITMAP_B_PAGE UINT32_C(5)
#define HV_EFI_IO_BITMAP_BYTES UINT32_C(4096)
#define HV_EFI_VMWARE_BACKDOOR_PORT UINT16_C(0x5658)
#define HV_EFI_VMWARE_MESSAGE_COMMAND UINT32_C(30)
#define HV_EFI_VMWARE_MESSAGE_STATUS_HB_ECX (UINT32_C(0x0080) << 16)
#define HV_EFI_EPT_PML4_PAGES UINT32_C(1)
#define HV_EFI_EPT_GUEST_PHYSICAL_BITS UINT32_C(39)
#define HV_EFI_EPT_PDPT_PAGES \
    (UINT32_C(1) << (HV_EFI_EPT_GUEST_PHYSICAL_BITS - UINT32_C(39)))
#define HV_EFI_EPT_LOW_PD_PAGES UINT32_C(512)
#define HV_EFI_EPT_BASE_TABLE_PAGES \
    (HV_EFI_EPT_PML4_PAGES + HV_EFI_EPT_PDPT_PAGES + \
     HV_EFI_EPT_LOW_PD_PAGES)
/* Each runtime allocation can touch at most the stated number of 2-MiB
 * regions.  This is a hard capacity bound, not a claim that every launch
 * consumes every split page. */
#define HV_EFI_EPT_SPLITS_PER_PROCESSOR UINT32_C(12)
#define HV_EFI_EPT_GLOBAL_SPLIT_RESERVE UINT32_C(16)
#define HV_EFI_EPT_MAX_SPLIT_PAGES \
    (HV_EFI_MAX_SUPPORTED_PROCESSORS * \
         HV_EFI_EPT_SPLITS_PER_PROCESSOR + \
     HV_EFI_EPT_GLOBAL_SPLIT_RESERVE)
#define HV_EFI_EPT_TABLE_PAGES \
    (HV_EFI_EPT_BASE_TABLE_PAGES + HV_EFI_EPT_MAX_SPLIT_PAGES)
/* Six global runtime ranges, five fixed VT-d/ACPI ranges, and up to eight
 * VT-d unit MMIO ranges, plus nine private ranges per processor. */
#define HV_EFI_EPT_MAX_DENY_RANGES \
    (UINT32_C(19) + UINT32_C(9) * HV_EFI_MAX_SUPPORTED_PROCESSORS)
#define HV_EFI_GUEST_DESCRIPTOR_STORAGE_PAGES UINT32_C(3)
#define HV_EFI_HOST_DESCRIPTOR_STORAGE_PAGES UINT32_C(3)
#define HV_EFI_DESCRIPTOR_STORAGE_PAGES \
    (HV_EFI_GUEST_DESCRIPTOR_STORAGE_PAGES + \
     HV_EFI_HOST_DESCRIPTOR_STORAGE_PAGES)
#define HV_EFI_DESCRIPTOR_TABLE_MAX_SIZE HV_EFI_PAGE_SIZE
#define HV_EFI_DESCRIPTOR_GUEST_GDT_PAGE UINT32_C(0)
#define HV_EFI_DESCRIPTOR_GUEST_IDT_PAGE UINT32_C(1)
#define HV_EFI_DESCRIPTOR_GUEST_TSS_PAGE UINT32_C(2)
#define HV_EFI_DESCRIPTOR_HOST_GDT_PAGE \
    HV_EFI_GUEST_DESCRIPTOR_STORAGE_PAGES
#define HV_EFI_DESCRIPTOR_HOST_IDT_PAGE UINT32_C(4)
#define HV_EFI_DESCRIPTOR_HOST_TSS_PAGE UINT32_C(5)
#define HV_EFI_HOST_GDT_LIMIT UINT16_C(0x27)
#define HV_EFI_HOST_IDT_LIMIT UINT16_C(0x0fff)
#define HV_EFI_HOST_CODE_SELECTOR UINT16_C(0x08)
#define HV_EFI_HOST_DATA_SELECTOR UINT16_C(0x10)
#define HV_EFI_HOST_TSS_SELECTOR UINT16_C(0x18)
#define HV_EFI_IDT_GATE_SIZE UINT32_C(16)
#define HV_EFI_IDT_GATE_COUNT UINT32_C(256)
#define HV_EFI_GUEST_INTERRUPT_STUB_SIZE UINT32_C(10)
#define HV_EFI_TSS64_MIN_SIZE UINT32_C(104)
#define HV_EFI_TSS64_RSP0_OFFSET UINT32_C(4)
#define HV_EFI_TSS64_IST1_OFFSET UINT32_C(36)
#define HV_EFI_TSS64_IST_COUNT UINT32_C(7)
#define HV_EFI_TSS64_IO_MAP_OFFSET UINT32_C(102)

/* CPUID leaf used by the probe image and by the resident VM-exit path. */
#define HV_EFI_HEALTH_LEAF UINT32_C(0x48565031) /* "HVP1" */
#define HV_EFI_HEALTH_SIGNATURE UINT32_C(0x55454649) /* "UEFI" */
#define HV_EFI_HEALTH_VERSION UINT32_C(1)
#define HV_EFI_HEALTH_STATUS_READY UINT32_C(1)
#define HV_EFI_HEALTH_STATUS_PERSISTENT UINT32_C(2)

/* VMCALL is used only by the private initial guest bootstrap stub. */
#define HV_EFI_CALL_MAGIC UINT64_C(0x55454649564d5831) /* "UEFIVMX1" */
#define HV_EFI_CALL_BOOTSTRAP UINT32_C(1)
#define HV_EFI_CALL_ARM_GUEST_INTERRUPT_GUARD UINT32_C(2)
#define HV_EFI_CALL_QUERY_RUNTIME_SESSION UINT32_C(3)
#define HV_EFI_CALL_PREPARE_NATIVE_DETACH UINT32_C(4)
#define HV_EFI_CALL_COMMIT_NATIVE_DETACH UINT32_C(5)
#define HV_EFI_CALL_RUNTIME_SCAN UINT32_C(6)
#define HV_EFI_CALL_PROCESS_LOCATE UINT32_C(7)
#define HV_EFI_CALL_SHADOW_CONTROL UINT32_C(8)
#define HV_EFI_CALL_ANALYSIS_CONTROL UINT32_C(9)

/* Ring-3 user-mode VMCALL command.  The resident handler validates CPL=3,
 * reads the shared mailbox, executes the command, and resumes the guest. */
#define HV_EFI_CALL_USER_COMMAND UINT32_C(10)
#define HV_EFI_USER_VMCALL_MAGIC UINT64_C(0x55535233564D5831) /* "USR3VMX1" */

/* User-mode mailbox and command state. */
#define HV_EFI_USER_STATE_UNAVAILABLE UINT32_C(0)
#define HV_EFI_USER_STATE_READY UINT32_C(1)
#define HV_EFI_USER_STATE_PROCESSING UINT32_C(2)

#define HV_EFI_RUNTIME_ABI_VERSION UINT32_C(1)
#define HV_EFI_RUNTIME_STATE_UNAVAILABLE UINT32_C(0)
#define HV_EFI_RUNTIME_STATE_ACTIVE UINT32_C(1)
#define HV_EFI_RUNTIME_STATE_PIVOT_ARMED UINT32_C(2)
#define HV_EFI_RUNTIME_STATE_OFFLINE UINT32_C(3)
#define HV_EFI_RUNTIME_CAP_STATUS (UINT32_C(1) << 0)
#define HV_EFI_RUNTIME_CAP_SHUTDOWN (UINT32_C(1) << 1)
#define HV_EFI_RUNTIME_CAP_SUSPEND_PREPARE (UINT32_C(1) << 2)
#define HV_EFI_RUNTIME_CAP_CRASH_MARKER (UINT32_C(1) << 3)
#define HV_EFI_RUNTIME_CAP_SCAN (UINT32_C(1) << 4)
#define HV_EFI_RUNTIME_CAP_PROCESS (UINT32_C(1) << 5)
#define HV_EFI_RUNTIME_CAP_SHADOW_EPT (UINT32_C(1) << 6)
#define HV_EFI_RUNTIME_CAP_ANALYSIS (UINT32_C(1) << 7)
#if HYPERSINV_KERNEL_PROFILE_AVAILABLE
#define HV_EFI_RUNTIME_PROFILE_CAPABILITY \
    (HV_EFI_RUNTIME_CAP_PROCESS | HV_EFI_RUNTIME_CAP_SHADOW_EPT | \
     HV_EFI_RUNTIME_CAP_ANALYSIS)
#else
#define HV_EFI_RUNTIME_PROFILE_CAPABILITY UINT32_C(0)
#endif
#define HV_EFI_RUNTIME_CAPABILITIES \
    (HV_EFI_RUNTIME_CAP_STATUS | HV_EFI_RUNTIME_CAP_SHUTDOWN | \
     HV_EFI_RUNTIME_CAP_SUSPEND_PREPARE | HV_EFI_RUNTIME_CAP_CRASH_MARKER | \
     HV_EFI_RUNTIME_CAP_SCAN | HV_EFI_RUNTIME_PROFILE_CAPABILITY)

#define HV_EFI_PROBE_PASS_MARKER "UEFI_HYPERVISOR_PROBE:PASS\n"
#define HV_EFI_PROBE_FAIL_MARKER "UEFI_HYPERVISOR_PROBE:FAIL\n"
#define HV_EFI_GUEST_INTERRUPT_GUARD_PASS_MARKER \
    "UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS\n"
#define HV_EFI_VM_LAUNCH_PASS_MARKER "uefi hypervisor: VMX launch: PASS"
#define HV_EFI_BSP_PASS_MARKER "uefi hypervisor: BSP virtualization: PASS"
#define HV_EFI_RESIDENT_FATAL_INTERRUPT_MARKER \
    "uefi hypervisor: FATAL INTERRUPT\n"

/* Resident host-stack ABI: [HostStackTop] is the context cookie and the
 * fifteen pushes in HvEfiResidentVmExit occupy the next 120 bytes. */
#define HV_EFI_HOST_CONTEXT_SLOT UINT32_C(0)
#define HV_EFI_HOST_PUSH_COUNT UINT32_C(15)
#define HV_EFI_HOST_PUSH_BYTES \
    (HV_EFI_HOST_PUSH_COUNT * (uint32_t)sizeof(uint64_t))

/* The guard terminates the bounded post-ExitBootServices interrupt probe. */
#define HV_EFI_DEBUG_EXIT_PORT UINT16_C(0x0501)
#define HV_EFI_GUEST_INTERRUPT_GUARD_EXIT_CODE UINT32_C(0x12)
#define HV_EFI_GUEST_INTERRUPT_IST UINT8_C(1)

/* VMX MSRs used by the firmware-side preflight. */
#define HV_EFI_IA32_FEATURE_CONTROL UINT32_C(0x3a)
#define HV_EFI_IA32_VMX_BASIC UINT32_C(0x480)
#define HV_EFI_IA32_VMX_CR0_FIXED0 UINT32_C(0x486)
#define HV_EFI_IA32_VMX_CR0_FIXED1 UINT32_C(0x487)
#define HV_EFI_IA32_VMX_CR4_FIXED0 UINT32_C(0x488)
#define HV_EFI_IA32_VMX_CR4_FIXED1 UINT32_C(0x489)
#define HV_EFI_IA32_VMX_TRUE_PINBASED_CTLS UINT32_C(0x48d)
#define HV_EFI_IA32_VMX_TRUE_PROCBASED_CTLS UINT32_C(0x48e)
#define HV_EFI_IA32_VMX_TRUE_EXIT_CTLS UINT32_C(0x48f)
#define HV_EFI_IA32_VMX_TRUE_ENTRY_CTLS UINT32_C(0x490)
#define HV_EFI_IA32_VMX_PINBASED_CTLS UINT32_C(0x481)
#define HV_EFI_IA32_VMX_PROCBASED_CTLS UINT32_C(0x482)
#define HV_EFI_IA32_VMX_EXIT_CTLS UINT32_C(0x483)
#define HV_EFI_IA32_VMX_ENTRY_CTLS UINT32_C(0x484)
#define HV_EFI_IA32_VMX_PROCBASED_CTLS2 UINT32_C(0x48b)
#define HV_EFI_IA32_VMX_EPT_VPID_CAP UINT32_C(0x48c)
#define HV_EFI_IA32_EFER UINT32_C(0xc0000080)
#define HV_EFI_IA32_PAT UINT32_C(0x277)
#define HV_EFI_IA32_PERF_GLOBAL_CTRL UINT32_C(0x38f)
#define HV_EFI_IA32_SYSENTER_CS UINT32_C(0x174)
#define HV_EFI_IA32_SYSENTER_ESP UINT32_C(0x175)
#define HV_EFI_IA32_SYSENTER_EIP UINT32_C(0x176)
#define HV_EFI_IA32_FS_BASE UINT32_C(0xc0000100)
#define HV_EFI_IA32_GS_BASE UINT32_C(0xc0000101)

#define HV_EFI_FEATURE_CONTROL_LOCK UINT64_C(1)
#define HV_EFI_FEATURE_CONTROL_VMXON_OUTSIDE_SMX (UINT64_C(1) << 2)
#define HV_EFI_VMX_BASIC_REVISION_MASK UINT64_C(0x7fffffff)
#define HV_EFI_VMX_BASIC_RESERVED_BIT31 (UINT64_C(1) << 31)
#define HV_EFI_VMX_BASIC_VMCS_SIZE_MASK UINT64_C(0x1fff)
#define HV_EFI_VMX_BASIC_VMCS_SIZE_SHIFT 32
#define HV_EFI_VMX_BASIC_TRUE_CONTROLS (UINT64_C(1) << 55)

/* IA32_VMX_{TRUE,}* control bit fields. */
#define HV_EFI_VM_EXIT_HOST_ADDR_SPACE_SIZE (UINT32_C(1) << 9)
#define HV_EFI_VM_EXIT_SAVE_PAT (UINT32_C(1) << 18)
#define HV_EFI_VM_EXIT_LOAD_PAT (UINT32_C(1) << 19)
#define HV_EFI_VM_EXIT_SAVE_EFER (UINT32_C(1) << 20)
#define HV_EFI_VM_EXIT_LOAD_EFER (UINT32_C(1) << 21)
#define HV_EFI_VM_ENTRY_LOAD_PAT (UINT32_C(1) << 14)
#define HV_EFI_VM_ENTRY_LOAD_EFER (UINT32_C(1) << 15)
#define HV_EFI_VM_ENTRY_IA32E_MODE_GUEST (UINT32_C(1) << 9)

/* VMCS encodings needed by the minimal launch and resident exit path. */
#define HV_EFI_VMCS_CTRL_PIN_BASED UINT32_C(0x4000)
#define HV_EFI_VMCS_CTRL_CPU_BASED UINT32_C(0x4002)
#define HV_EFI_VMCS_CTRL_SECONDARY UINT32_C(0x401e)
#define HV_EFI_VMCS_CTRL_VM_EXIT UINT32_C(0x400c)
#define HV_EFI_VMCS_CTRL_VM_ENTRY UINT32_C(0x4012)
#define HV_EFI_VMCS_CTRL_EXCEPTION_BITMAP UINT32_C(0x4004)
#define HV_EFI_VMCS_CTRL_PF_ERROR_CODE_MASK UINT32_C(0x4006)
#define HV_EFI_VMCS_CTRL_PF_ERROR_CODE_MATCH UINT32_C(0x4008)
#define HV_EFI_VMCS_CTRL_CR3_TARGET_COUNT UINT32_C(0x400a)
#define HV_EFI_VMCS_CTRL_VM_EXIT_MSR_STORE_COUNT UINT32_C(0x400e)
#define HV_EFI_VMCS_CTRL_VM_EXIT_MSR_LOAD_COUNT UINT32_C(0x4010)
#define HV_EFI_VMCS_CTRL_VM_ENTRY_MSR_LOAD_COUNT UINT32_C(0x4014)
#define HV_EFI_VMCS_CTRL_VM_ENTRY_INT_INFO UINT32_C(0x4016)
#define HV_EFI_VMCS_CTRL_VM_ENTRY_EXCEPTION_ERR UINT32_C(0x4018)
#define HV_EFI_VMCS_CTRL_VM_ENTRY_INSTR_LENGTH UINT32_C(0x401a)
#define HV_EFI_VM_ENTRY_INT_INFO_VALID (UINT32_C(1) << 31)
#define HV_EFI_VM_ENTRY_INT_INFO_DELIVER_ERROR (UINT32_C(1) << 11)
#define HV_EFI_VM_ENTRY_INT_INFO_HARDWARE_EXCEPTION (UINT32_C(3) << 8)
#define HV_EFI_GP_VECTOR UINT32_C(13)
#define HV_EFI_HYPERV_MSR_START UINT32_C(0x40000000)
#define HV_EFI_HYPERV_MSR_END UINT32_C(0x400000ff)
#define HV_EFI_AMD_SEV_STATUS_MSR UINT32_C(0xC0010131)
#define HV_EFI_VM_ENTRY_GP_INJECTION_INFO \
    (HV_EFI_VM_ENTRY_INT_INFO_VALID | \
     HV_EFI_VM_ENTRY_INT_INFO_DELIVER_ERROR | \
     HV_EFI_VM_ENTRY_INT_INFO_HARDWARE_EXCEPTION | HV_EFI_GP_VECTOR)
#define HV_EFI_VMCS_GUEST_ES_SELECTOR UINT32_C(0x800)
#define HV_EFI_VMCS_GUEST_CS_SELECTOR UINT32_C(0x802)
#define HV_EFI_VMCS_GUEST_SS_SELECTOR UINT32_C(0x804)
#define HV_EFI_VMCS_GUEST_DS_SELECTOR UINT32_C(0x806)
#define HV_EFI_VMCS_GUEST_FS_SELECTOR UINT32_C(0x808)
#define HV_EFI_VMCS_GUEST_GS_SELECTOR UINT32_C(0x80a)
#define HV_EFI_VMCS_GUEST_LDTR_SELECTOR UINT32_C(0x80c)
#define HV_EFI_VMCS_GUEST_TR_SELECTOR UINT32_C(0x80e)
#define HV_EFI_VMCS_CTRL_CR0_MASK UINT32_C(0x6000)
#define HV_EFI_VMCS_CTRL_CR4_MASK UINT32_C(0x6002)
#define HV_EFI_VMCS_CTRL_CR0_READ_SHADOW UINT32_C(0x6004)
#define HV_EFI_VMCS_CTRL_CR4_READ_SHADOW UINT32_C(0x6006)
#define HV_EFI_VMCS_CTRL_IO_BITMAP_A UINT32_C(0x2000)
#define HV_EFI_VMCS_CTRL_IO_BITMAP_B UINT32_C(0x2002)
#define HV_EFI_VMCS_CTRL_MSR_BITMAP UINT32_C(0x2004)
#define HV_EFI_VMCS_CTRL_EPT_POINTER UINT32_C(0x201a)
#define HV_EFI_VMCS_CTRL_VPID UINT32_C(0x0000)
#define HV_EFI_VM_CPU_UNCOND_IO_EXITING (UINT32_C(1) << 24)
#define HV_EFI_VM_CPU_CR3_LOAD_EXITING (UINT32_C(1) << 15)
#define HV_EFI_VM_CPU_USE_IO_BITMAPS (UINT32_C(1) << 25)
#define HV_EFI_VM_CPU_USE_MSR_BITMAPS (UINT32_C(1) << 28)
#define HV_EFI_VM_CPU_ACTIVATE_SECONDARY (UINT32_C(1) << 31)
#define HV_EFI_VM_CPU_RDTSC_EXITING (UINT32_C(1) << 10)
#define HV_EFI_VM_SECONDARY_ENABLE_EPT (UINT32_C(1) << 1)
#define HV_EFI_VM_SECONDARY_ENABLE_RDTSCP (UINT32_C(1) << 3)
#define HV_EFI_VM_SECONDARY_ENABLE_VPID (UINT32_C(1) << 5)
#define HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST (UINT32_C(1) << 7)
#define HV_EFI_VM_SECONDARY_ENABLE_INVPCID (UINT32_C(1) << 12)
#define HV_EFI_VM_SECONDARY_ENABLE_XSAVES_XRSTORS (UINT32_C(1) << 20)
#define HV_EFI_EPT_CAP_WALK_LENGTH_4 (UINT64_C(1) << 6)
#define HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_UC (UINT64_C(1) << 8)
#define HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB (UINT64_C(1) << 14)
#define HV_EFI_EPT_CAP_2MB_PAGE (UINT64_C(1) << 16)
#define HV_EFI_EPT_CAP_1GB_PAGE (UINT64_C(1) << 17)
#define HV_EFI_EPT_CAP_INVEPT (UINT64_C(1) << 20)
#define HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT (UINT64_C(1) << 25)
#define HV_EFI_VPID_CAP_INVVPID (UINT64_C(1) << 32)
#define HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT (UINT64_C(1) << 41)
#define HV_EFI_EPT_MEMORY_TYPE_UC UINT32_C(0)
#define HV_EFI_EPT_MEMORY_TYPE_WB UINT32_C(6)
#define HV_EFI_EPTP_WALK_LENGTH_4 (UINT64_C(3) << 3)
#define HV_EFI_EPT_ACCESS_RWX UINT64_C(7)
#define HV_EFI_EPT_ACCESS_RX UINT64_C(5)
#define HV_EFI_EPT_ACCESS_RW UINT64_C(3)
#define HV_EFI_EPT_ACCESS_R UINT64_C(1)
#define HV_EFI_EPT_LARGE_PAGE (UINT64_C(1) << 7)
#define HV_EFI_EPT_2MB_SHIFT UINT32_C(21)
#define HV_EFI_EPT_1GB_SHIFT UINT32_C(30)
#define HV_EFI_EPT_ENTRIES_PER_PAGE UINT32_C(512)
#define HV_EFI_EPT_IDENTITY_PD_COUNT HV_EFI_EPT_LOW_PD_PAGES
#define HV_EFI_EPT_IDENTITY_PAGE_COUNT \
    (HV_EFI_EPT_IDENTITY_PD_COUNT * HV_EFI_EPT_ENTRIES_PER_PAGE)
#define HV_EFI_EPT_IDENTITY_1GB_PAGE_COUNT \
    (HV_EFI_EPT_PDPT_PAGES * HV_EFI_EPT_ENTRIES_PER_PAGE)
#define HV_EFI_EPT_PDPT_FIRST_PAGE HV_EFI_EPT_PML4_PAGES
#define HV_EFI_EPT_LOW_PD_FIRST_PAGE \
    (HV_EFI_EPT_PDPT_FIRST_PAGE + HV_EFI_EPT_PDPT_PAGES)
/* Source compatibility for callers built against the original 4-GiB MVP. */
#define HV_EFI_EPT_IDENTITY_4GB_PAGE_COUNT \
    HV_EFI_EPT_IDENTITY_PAGE_COUNT
#define HV_EFI_MEMORY_DESCRIPTOR_VERSION UINT32_C(1)
#define HV_EFI_MEMORY_ATTRIBUTE_WB UINT64_C(0x0000000000000008)
#define HV_EFI_MEMORY_TYPE_RESERVED UINT32_C(0)
#define HV_EFI_MEMORY_TYPE_LOADER_CODE UINT32_C(1)
#define HV_EFI_MEMORY_TYPE_LOADER_DATA UINT32_C(2)
#define HV_EFI_MEMORY_TYPE_BOOT_SERVICES_CODE UINT32_C(3)
#define HV_EFI_MEMORY_TYPE_BOOT_SERVICES_DATA UINT32_C(4)
#define HV_EFI_MEMORY_TYPE_RUNTIME_SERVICES_CODE UINT32_C(5)
#define HV_EFI_MEMORY_TYPE_RUNTIME_SERVICES_DATA UINT32_C(6)
#define HV_EFI_MEMORY_TYPE_CONVENTIONAL UINT32_C(7)
#define HV_EFI_MEMORY_TYPE_UNUSABLE UINT32_C(8)
#define HV_EFI_MEMORY_TYPE_ACPI_RECLAIM UINT32_C(9)
#define HV_EFI_MEMORY_TYPE_ACPI_NVS UINT32_C(10)
#define HV_EFI_MEMORY_TYPE_MMIO UINT32_C(11)
#define HV_EFI_MEMORY_TYPE_MMIO_PORT UINT32_C(12)
#define HV_EFI_MEMORY_TYPE_PAL_CODE UINT32_C(13)
#define HV_EFI_MEMORY_TYPE_PERSISTENT UINT32_C(14)
#define HV_EFI_VMCS_GUEST_CR0 UINT32_C(0x6800)
#define HV_EFI_VMCS_GUEST_CR3 UINT32_C(0x6802)
#define HV_EFI_VMCS_GUEST_CR4 UINT32_C(0x6804)
#define HV_EFI_VMCS_GUEST_ES_BASE UINT32_C(0x6806)
#define HV_EFI_VMCS_GUEST_CS_BASE UINT32_C(0x6808)
#define HV_EFI_VMCS_GUEST_SS_BASE UINT32_C(0x680a)
#define HV_EFI_VMCS_GUEST_DS_BASE UINT32_C(0x680c)
#define HV_EFI_VMCS_GUEST_FS_BASE UINT32_C(0x680e)
#define HV_EFI_VMCS_GUEST_GS_BASE UINT32_C(0x6810)
#define HV_EFI_VMCS_GUEST_LDTR_BASE UINT32_C(0x6812)
#define HV_EFI_VMCS_GUEST_TR_BASE UINT32_C(0x6814)
#define HV_EFI_VMCS_GUEST_GDTR_BASE UINT32_C(0x6816)
#define HV_EFI_VMCS_GUEST_IDTR_BASE UINT32_C(0x6818)
#define HV_EFI_VMCS_GUEST_DR7 UINT32_C(0x681a)
#define HV_EFI_VMCS_GUEST_RSP UINT32_C(0x681c)
#define HV_EFI_VMCS_GUEST_RIP UINT32_C(0x681e)
#define HV_EFI_VMCS_GUEST_RFLAGS UINT32_C(0x6820)
#define HV_EFI_VMCS_GUEST_PENDING_DEBUG UINT32_C(0x6822)
#define HV_EFI_VMCS_GUEST_SYSENTER_ESP UINT32_C(0x6824)
#define HV_EFI_VMCS_GUEST_SYSENTER_EIP UINT32_C(0x6826)
#define HV_EFI_VMCS_GUEST_ES_LIMIT UINT32_C(0x4800)
#define HV_EFI_VMCS_GUEST_CS_LIMIT UINT32_C(0x4802)
#define HV_EFI_VMCS_GUEST_SS_LIMIT UINT32_C(0x4804)
#define HV_EFI_VMCS_GUEST_DS_LIMIT UINT32_C(0x4806)
#define HV_EFI_VMCS_GUEST_FS_LIMIT UINT32_C(0x4808)
#define HV_EFI_VMCS_GUEST_GS_LIMIT UINT32_C(0x480a)
#define HV_EFI_VMCS_GUEST_LDTR_LIMIT UINT32_C(0x480c)
#define HV_EFI_VMCS_GUEST_TR_LIMIT UINT32_C(0x480e)
#define HV_EFI_VMCS_GUEST_GDTR_LIMIT UINT32_C(0x4810)
#define HV_EFI_VMCS_GUEST_IDTR_LIMIT UINT32_C(0x4812)
#define HV_EFI_VMCS_GUEST_ES_ACCESS UINT32_C(0x4814)
#define HV_EFI_VMCS_GUEST_CS_ACCESS UINT32_C(0x4816)
#define HV_EFI_VMCS_GUEST_SS_ACCESS UINT32_C(0x4818)
#define HV_EFI_VMCS_GUEST_DS_ACCESS UINT32_C(0x481a)
#define HV_EFI_VMCS_GUEST_FS_ACCESS UINT32_C(0x481c)
#define HV_EFI_VMCS_GUEST_GS_ACCESS UINT32_C(0x481e)
#define HV_EFI_VMCS_GUEST_LDTR_ACCESS UINT32_C(0x4820)
#define HV_EFI_VMCS_GUEST_TR_ACCESS UINT32_C(0x4822)
#define HV_EFI_VMCS_GUEST_INTERRUPTIBILITY UINT32_C(0x4824)
#define HV_EFI_VMCS_GUEST_ACTIVITY UINT32_C(0x4826)
#define HV_EFI_VMCS_GUEST_SYSENTER_CS UINT32_C(0x482a)
#define HV_EFI_VMCS_GUEST_LINK_POINTER UINT32_C(0x2800)
#define HV_EFI_VMCS_GUEST_PAT UINT32_C(0x2804)
#define HV_EFI_VMCS_GUEST_EFER UINT32_C(0x2806)
#define HV_EFI_VMCS_GUEST_PERF_GLOBAL_CTRL UINT32_C(0x2808)
#define HV_EFI_VMCS_HOST_CR0 UINT32_C(0x6c00)
#define HV_EFI_VMCS_HOST_CR3 UINT32_C(0x6c02)
#define HV_EFI_VMCS_HOST_CR4 UINT32_C(0x6c04)
#define HV_EFI_VMCS_HOST_FS_BASE UINT32_C(0x6c06)
#define HV_EFI_VMCS_HOST_GS_BASE UINT32_C(0x6c08)
#define HV_EFI_VMCS_HOST_TR_BASE UINT32_C(0x6c0a)
#define HV_EFI_VMCS_HOST_GDTR_BASE UINT32_C(0x6c0c)
#define HV_EFI_VMCS_HOST_IDTR_BASE UINT32_C(0x6c0e)
#define HV_EFI_VMCS_HOST_SYSENTER_ESP UINT32_C(0x6c10)
#define HV_EFI_VMCS_HOST_SYSENTER_EIP UINT32_C(0x6c12)
#define HV_EFI_VMCS_HOST_RSP UINT32_C(0x6c14)
#define HV_EFI_VMCS_HOST_RIP UINT32_C(0x6c16)
#define HV_EFI_VMCS_HOST_PAT UINT32_C(0x2c00)
#define HV_EFI_VMCS_HOST_EFER UINT32_C(0x2c02)
#define HV_EFI_VMCS_HOST_PERF_GLOBAL_CTRL UINT32_C(0x2c04)
#define HV_EFI_VMCS_HOST_SYSENTER_CS UINT32_C(0x4c00)
#define HV_EFI_VMCS_HOST_ES_SELECTOR UINT32_C(0xc00)
#define HV_EFI_VMCS_HOST_CS_SELECTOR UINT32_C(0xc02)
#define HV_EFI_VMCS_HOST_SS_SELECTOR UINT32_C(0xc04)
#define HV_EFI_VMCS_HOST_DS_SELECTOR UINT32_C(0xc06)
#define HV_EFI_VMCS_HOST_FS_SELECTOR UINT32_C(0xc08)
#define HV_EFI_VMCS_HOST_GS_SELECTOR UINT32_C(0xc0a)
#define HV_EFI_VMCS_HOST_TR_SELECTOR UINT32_C(0xc0c)
#define HV_EFI_VMCS_VM_INSTRUCTION_ERROR UINT32_C(0x4400)
#define HV_EFI_VMCS_EXIT_REASON UINT32_C(0x4402)
#define HV_EFI_VMCS_EXIT_INSTRUCTION_LEN UINT32_C(0x440c)
#define HV_EFI_VMCS_VM_EXIT_QUALIFICATION UINT32_C(0x6400)
#define HV_EFI_VMCS_GUEST_PHYSICAL_ADDRESS UINT32_C(0x2400)

#define HV_EFI_EXIT_REASON_CPUID UINT32_C(10)
#define HV_EFI_EXIT_REASON_VMCALL UINT32_C(18)
#define HV_EFI_EXIT_REASON_INIT_SIGNAL UINT32_C(3)
#define HV_EFI_EXIT_REASON_SIPI UINT32_C(4)
#define HV_EFI_EXIT_REASON_CR_ACCESS UINT32_C(28)
#define HV_EFI_EXIT_REASON_IO_INSTRUCTION UINT32_C(30)
#define HV_EFI_EXIT_REASON_RDMSR UINT32_C(31)
#define HV_EFI_EXIT_REASON_WRMSR UINT32_C(32)
#define HV_EFI_EXIT_REASON_EPT_VIOLATION UINT32_C(48)

/* The resident guest does not receive nested VMX/SMX.  Preserve the containing
 * VM's hypervisor-present bit and paravirtual CPUID leaves: hiding them makes
 * virtualization-aware Windows services attempt incompatible bare-metal paths. */
#define HV_EFI_CPUID_LEAF1_GUEST_ECX_MASK UINT32_C(0xffffff9f)

#define HV_EFI_GUEST_ACTIVITY_ACTIVE UINT32_C(0)
#define HV_EFI_GUEST_ACTIVITY_WAIT_FOR_SIPI UINT32_C(3)
#define HV_EFI_GUEST_REAL_MODE_DATA_ACCESS UINT32_C(0x93)
#define HV_EFI_GUEST_REAL_MODE_CODE_ACCESS UINT32_C(0x9b)
#define HV_EFI_GUEST_RESET_TR_ACCESS UINT32_C(0x8b)
#define HV_EFI_GUEST_UNUSABLE_ACCESS UINT32_C(0x10000)
#define HV_EFI_GUEST_RESET_CR0 UINT64_C(0x60000010)
#define HV_EFI_CR0_PE (UINT64_C(1) << 0)
#define HV_EFI_CR0_WP (UINT64_C(1) << 16)
#define HV_EFI_CR0_PG (UINT64_C(1) << 31)
#define HV_EFI_GUEST_VMX_CR0_NE (UINT64_C(1) << 5)
#define HV_EFI_GUEST_RESET_CR0_VMX \
    (HV_EFI_GUEST_RESET_CR0 | HV_EFI_GUEST_VMX_CR0_NE)
#define HV_EFI_GUEST_RESET_DR7 UINT64_C(0x400)
#define HV_EFI_GUEST_RESET_PAT UINT64_C(0x0007040600070406)
#define HV_EFI_EFER_LME (UINT64_C(1) << 8)
#define HV_EFI_EFER_LMA (UINT64_C(1) << 10)
#define HV_EFI_EFER_NXE (UINT64_C(1) << 11)

#define HV_EFI_AP_STARTUP_RESULT_INVALID UINT32_C(0)
#define HV_EFI_AP_STARTUP_RESULT_WAITING UINT32_C(1)
#define HV_EFI_AP_STARTUP_RESULT_STARTED UINT32_C(2)
#define HV_EFI_AP_STARTUP_RESULT_IGNORED UINT32_C(3)
#define HV_EFI_SIPI_VECTOR_MASK UINT64_C(0xff)

#define HV_EFI_CONTEXT_MAGIC UINT64_C(0x55454649564d5831)
#define HV_EFI_CONTEXT_VERSION UINT32_C(6)

#define HV_EFI_AP_PREFLIGHT_UNAVAILABLE UINT32_C(0)
#define HV_EFI_AP_PREFLIGHT_BSP_ONLY UINT32_C(1)
#define HV_EFI_AP_PREFLIGHT_READY UINT32_C(2)
#define HV_EFI_AP_PREFLIGHT_FAILED UINT32_C(3)

#define HV_EFI_PROCESSOR_RESOURCES_UNAVAILABLE UINT32_C(0)
#define HV_EFI_PROCESSOR_RESOURCES_BSP_ONLY UINT32_C(1)
#define HV_EFI_PROCESSOR_RESOURCES_READY UINT32_C(2)
#define HV_EFI_PROCESSOR_RESOURCES_FAILED UINT32_C(3)

#define HV_EFI_PROCESSOR_STATE_VMX_READY UINT32_C(0x00000001)
#define HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND UINT32_C(0x00000002)
#define HV_EFI_PROCESSOR_STATE_VIRTUALIZED UINT32_C(0x00000004)
#define HV_EFI_PROCESSOR_STATE_CPU_CAPTURED UINT32_C(0x00000008)
#define HV_EFI_PROCESSOR_STATE_DESCRIPTORS_READY UINT32_C(0x00000010)
#define HV_EFI_PROCESSOR_STATE_CONTROLS_READY UINT32_C(0x00000020)
#define HV_EFI_PROCESSOR_STATE_STARTUP_CAPABLE UINT32_C(0x00000040)
#define HV_EFI_PROCESSOR_STATE_EPT_READY UINT32_C(0x00000080)
#define HV_EFI_PROCESSOR_STATE_PREPARED \
    (HV_EFI_PROCESSOR_STATE_CPU_CAPTURED | \
     HV_EFI_PROCESSOR_STATE_DESCRIPTORS_READY | \
     HV_EFI_PROCESSOR_STATE_CONTROLS_READY)

#define HV_EFI_PROCESSOR_PREPARATION_UNAVAILABLE UINT32_C(0)
#define HV_EFI_PROCESSOR_PREPARATION_BSP_ONLY UINT32_C(1)
#define HV_EFI_PROCESSOR_PREPARATION_READY UINT32_C(2)
#define HV_EFI_PROCESSOR_PREPARATION_FAILED UINT32_C(3)

#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_NONE UINT32_C(0)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_PROTOCOL UINT32_C(1)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_IDENTITY UINT32_C(2)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_RECORD UINT32_C(3)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_CONTEXT UINT32_C(4)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_VMX_BASIC UINT32_C(5)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_DESCRIPTOR UINT32_C(6)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_CONTROLS UINT32_C(7)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_INVARIANT UINT32_C(8)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_STARTUP_CAPABILITIES UINT32_C(9)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_MEMORY_POLICY UINT32_C(10)
#define HV_EFI_PROCESSOR_PREPARATION_FAILURE_EPT_HIERARCHY UINT32_C(11)

#define HV_EFI_AP_ACTIVATION_UNAVAILABLE UINT32_C(0)
#define HV_EFI_AP_ACTIVATION_BSP_ONLY UINT32_C(1)
#define HV_EFI_AP_ACTIVATION_READY UINT32_C(2)
#define HV_EFI_AP_ACTIVATION_FAILED UINT32_C(3)

#define HV_EFI_AP_ACTIVATION_FAILURE_NONE UINT32_C(0)
#define HV_EFI_AP_ACTIVATION_FAILURE_TOPOLOGY UINT32_C(1)
#define HV_EFI_AP_ACTIVATION_FAILURE_IDENTITY UINT32_C(2)
#define HV_EFI_AP_ACTIVATION_FAILURE_INVARIANT UINT32_C(3)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMXON UINT32_C(4)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMCLEAR UINT32_C(5)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMPTRLD UINT32_C(6)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMCS_CONTROLS UINT32_C(7)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_CS UINT32_C(8)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_SS UINT32_C(9)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_TR UINT32_C(10)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_WRITE UINT32_C(11)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMCS_HOST UINT32_C(12)
#define HV_EFI_AP_ACTIVATION_FAILURE_VMLAUNCH UINT32_C(13)

#define HV_EFI_TOPOLOGY_PASS_MARKER \
    "uefi hypervisor: processor topology: PASS"
#define HV_EFI_AP_PREFLIGHT_PASS_MARKER \
    "uefi hypervisor: AP VMX preflight: PASS"
#define HV_EFI_AP_PREFLIGHT_BSP_ONLY_MARKER \
    "uefi hypervisor: AP VMX preflight: BSP ONLY"
#define HV_EFI_AP_PREFLIGHT_UNAVAILABLE_MARKER \
    "uefi hypervisor: AP VMX preflight: UNAVAILABLE"
#define HV_EFI_AP_PREFLIGHT_FAIL_MARKER \
    "uefi hypervisor: AP VMX preflight: FAIL"
#define HV_EFI_PROCESSOR_RESOURCES_PASS_MARKER \
    "uefi hypervisor: processor resources: PASS"
#define HV_EFI_PROCESSOR_RESOURCES_BSP_ONLY_MARKER \
    "uefi hypervisor: processor resources: BSP ONLY"
#define HV_EFI_PROCESSOR_RESOURCES_UNAVAILABLE_MARKER \
    "uefi hypervisor: processor resources: UNAVAILABLE"
#define HV_EFI_PROCESSOR_RESOURCES_FAIL_MARKER \
    "uefi hypervisor: processor resources: FAIL"
#define HV_EFI_PROCESSOR_PREPARATION_PASS_MARKER \
    "uefi hypervisor: processor state: PASS"
#define HV_EFI_PROCESSOR_PREPARATION_BSP_ONLY_MARKER \
    "uefi hypervisor: processor state: BSP ONLY"
#define HV_EFI_PROCESSOR_PREPARATION_UNAVAILABLE_MARKER \
    "uefi hypervisor: processor state: UNAVAILABLE"
#define HV_EFI_PROCESSOR_PREPARATION_FAIL_MARKER \
    "uefi hypervisor: processor state: FAIL"
#define HV_EFI_AP_ACTIVATION_PASS_MARKER \
    "uefi hypervisor: AP virtualization: PASS"
#define HV_EFI_AP_ACTIVATION_BSP_ONLY_MARKER \
    "uefi hypervisor: AP virtualization: BSP ONLY"
#define HV_EFI_AP_ACTIVATION_FAIL_MARKER \
    "uefi hypervisor: AP virtualization: FAIL"
#define HV_EFI_AP_ACTIVATION_FAIL_STOP_MARKER \
    "uefi hypervisor: AP virtualization: FAIL STOP"
#define HV_EFI_AP_ACTIVATION_COUNT_MARKER \
    "uefi hypervisor: activated AP count: 0x"

typedef struct HV_EFI_ENTRY_FRAME {
    uint64_t EntryRsp;
    uint64_t InitialRbx;
    uint64_t InitialRbp;
    uint64_t InitialRsi;
    uint64_t InitialRdi;
    uint64_t InitialR12;
    uint64_t InitialR13;
    uint64_t InitialR14;
    uint64_t InitialR15;
} HV_EFI_ENTRY_FRAME;

/* Runtime-owned topology identity and per-processor resource registry. */
typedef struct HV_EFI_PROCESSOR_RECORD {
    uint64_t ProcessorId;
    uint32_t ProcessorNumber;
    uint32_t FirmwareStatus;
    uint32_t StateFlags;
    uint32_t Reserved;
    uint64_t ContextPhysical;
    uint64_t VmxonPhysical;
    uint64_t VmcsPhysical;
    uint64_t HostStackPhysical;
    uint64_t GuestStackPhysical;
    uint64_t PageTablePhysical;
    uint64_t DescriptorStoragePhysical;
    uint64_t EptPhysical;
    uint64_t ShadowEptPhysical;
    uint64_t ShadowEptStatePhysical;
} HV_EFI_PROCESSOR_RECORD;

typedef struct HV_EFI_MEMORY_DESCRIPTOR_VIEW {
    uint32_t Type;
    uint32_t Reserved;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} HV_EFI_MEMORY_DESCRIPTOR_VIEW;

typedef struct HV_EFI_EPT_DENY_RANGE {
    uint64_t PhysicalBase;
    uint64_t Size;
} HV_EFI_EPT_DENY_RANGE;

/* Pure model mirrored by the resident AP INIT/SIPI VM-exit implementation. */
typedef struct HV_EFI_AP_STARTUP_STATE {
    uint32_t ActivityState;
    uint32_t StartupVector;
    uint16_t GuestCsSelector;
    uint16_t Reserved;
    uint32_t TransitionCount;
    uint64_t GuestCsBase;
    uint64_t GuestRip;
} HV_EFI_AP_STARTUP_STATE;

/*
 * The first qwords are an explicit MASM ABI.  Keep this structure below one
 * page: the firmware allocates it as EfiRuntimeServicesData and the resident
 * VM-exit path accesses it through the private identity map.
 */
typedef struct HV_EFI_CONTEXT {
    uint64_t Signature;              /* 0 */
    uint32_t Version;                /* 8 */
    uint32_t Size;                   /* 12 */
    uint64_t EntryRsp;               /* 16 */
    uint64_t InitialRbx;             /* 24 */
    uint64_t InitialRbp;             /* 32 */
    uint64_t InitialRsi;             /* 40 */
    uint64_t InitialRdi;             /* 48 */
    uint64_t InitialR12;             /* 56 */
    uint64_t InitialR13;             /* 64 */
    uint64_t InitialR14;             /* 72 */
    uint64_t InitialR15;             /* 80 */
    uint64_t HostStackTop;           /* 88 */
    uint64_t GuestStackTop;          /* 96 */
    uint64_t ResidentCodeBase;       /* 104 */
    uint64_t ResidentCodeSize;       /* 112 */
    uint64_t ResidentHostRip;        /* 120 */
    uint64_t GuestBootstrapRip;      /* 128 */
    uint64_t GuestReturnRip;         /* 136 */
    uint64_t VmxonPhysical;          /* 144 */
    uint64_t VmcsPhysical;           /* 152 */
    uint64_t PageTablePhysical;      /* 160 */
    uint64_t GuestCr0;               /* 168 */
    uint64_t GuestCr3;               /* 176 */
    uint64_t GuestCr4;               /* 184 */
    uint64_t HostCr0;                /* 192 */
    uint64_t HostCr4;                /* 200 */
    uint64_t Efer;                   /* 208 */
    uint64_t Pat;                    /* 216 */
    uint64_t FsBase;                 /* 224 */
    uint64_t GsBase;                 /* 232 */
    uint64_t TrBase;                 /* 240: guest TR base */
    uint64_t SysenterCs;             /* 248 */
    uint64_t SysenterEsp;            /* 256 */
    uint64_t SysenterEip;            /* 264 */
    uint64_t PerfGlobalCtrl;         /* 272 */
    uint64_t GdtrBase;               /* 280: guest GDTR base */
    uint64_t IdtrBase;               /* 288: guest IDTR base */
    uint64_t GdtrLimit;              /* 296 */
    uint64_t IdtrLimit;              /* 304 */
    uint64_t Cs;                     /* 312 */
    uint64_t Ss;                     /* 320 */
    uint64_t Ds;                     /* 328 */
    uint64_t Es;                     /* 336 */
    uint64_t Fs;                     /* 344 */
    uint64_t Gs;                     /* 352 */
    uint64_t Ldtr;                   /* 360 */
    uint64_t Tr;                     /* 368 */
    uint64_t Dr7;                    /* 376 */
    uint64_t Rflags;                 /* 384 */
    uint64_t GuestRsp;               /* 392 */
    uint64_t GuestRflags;            /* 400 */
    uint64_t GuestRax;               /* 408 */
    uint64_t GuestRbx;               /* 416 */
    uint64_t GuestRcx;               /* 424 */
    uint64_t GuestRdx;               /* 432 */
    uint64_t GuestRbp;               /* 440 */
    uint64_t GuestRsi;               /* 448 */
    uint64_t GuestRdi;               /* 456 */
    uint64_t GuestR8;                /* 464 */
    uint64_t GuestR9;                /* 472 */
    uint64_t GuestR10;               /* 480 */
    uint64_t GuestR11;               /* 488 */
    uint64_t GuestR12;               /* 496 */
    uint64_t GuestR13;               /* 504 */
    uint64_t GuestR14;               /* 512 */
    uint64_t GuestR15;               /* 520 */
    uint64_t GuestRip;               /* 528 */
    uint32_t State;                  /* 536 */
    uint32_t HealthSeen;             /* 540 */
    uint32_t ReadySeen;              /* 544 */
    uint32_t Failure;                /* 548 */
    uint32_t VmxError;               /* 552 */
    uint32_t VmxErrorValid;          /* 556 */
    uint32_t LastExitReason;         /* 560 */
    uint32_t LastExitLength;         /* 564 */
    uint32_t VmcsActive;             /* 568: VMCS writes are legal only when set */
    uint32_t PinControls;            /* 572 */
    uint32_t CpuControls;            /* 576 */
    uint32_t ExitControls;           /* 580 */
    uint32_t EntryControls;          /* 584 */
    uint32_t GuestEsLimit;           /* 588 */
    uint32_t GuestCsLimit;           /* 592 */
    uint32_t GuestSsLimit;           /* 596 */
    uint32_t GuestDsLimit;           /* 600 */
    uint32_t GuestFsLimit;           /* 604 */
    uint32_t GuestGsLimit;           /* 608 */
    uint32_t GuestLdtrLimit;         /* 612 */
    uint32_t GuestTrLimit;           /* 616 */
    uint64_t OriginalCr0;            /* 624 (natural uint64 alignment) */
    uint64_t OriginalCr4;            /* 632 */
    uint64_t MsrBitmapPhysical;      /* 640 */
    /* The pre-640 fields above retain their historical guest-state ABI names.
     * GdtrBase/IdtrBase/TrBase are guest bases once descriptor residency is
     * prepared.  These fields hold the independent VMX-host descriptor state. */
    uint64_t HostGdtrBase;            /* 648 */
    uint64_t HostIdtrBase;            /* 656 */
    uint64_t HostTrBase;              /* 664 */
    uint64_t DescriptorStoragePhysical; /* 672 */
    uint64_t GuestTssBase;             /* 680 */
    uint64_t HostTssBase;              /* 688 */
    uint32_t DescriptorStoragePages;   /* 696 */
    uint32_t GuestTssSize;             /* 700 */
    uint32_t HostTssSize;              /* 704 */
    uint32_t DescriptorStateReady;     /* 708 */
    uint32_t SecondaryControls;        /* 712 */
    uint32_t ProcessorCount;           /* 716 */
    uint32_t EnabledProcessorCount;    /* 720 */
    uint32_t BspProcessorNumber;       /* 724 */
    uint32_t VmxReadyProcessorCount;   /* 728 */
    uint32_t ApPreflightStatus;        /* 732 */
    uint64_t ProcessorRecordsPhysical; /* 736 */
    uint32_t ProcessorRecordPages;     /* 744 */
    uint32_t ProcessorRecordCount;     /* 748 */
    uint32_t CurrentProcessorNumber;   /* 752 */
    uint32_t ResourceBoundProcessorCount; /* 756 */
    uint32_t ProcessorResourceStatus;  /* 760 */
    uint32_t ProcessorPreparationFailure; /* 764 */
    uint32_t PreparedProcessorCount;   /* 768 */
    uint32_t ProcessorPreparationStatus; /* 772 */
    uint32_t HostDescriptorStateReady; /* 776 */
    uint32_t ReservedPreparation;      /* 780 */
    uint64_t EptVpidCapabilities;      /* 784 */
    uint32_t ApStartupCapabilitiesReady; /* 792 */
    uint16_t Vpid;                     /* 796 */
    uint16_t ReservedStartup;          /* 798 */
    uint64_t EptPhysical;              /* 800 */
    uint64_t EptPointer;               /* 808 */
    uint32_t EptHierarchyReady;         /* 816 */
    uint32_t EptWriteBackLeafCount;     /* 820 */
    uint32_t ApActivationStatus;         /* 824 */
    uint32_t ActivatedApCount;           /* 828 */
    uint32_t ApActivationFailure;        /* 832 */
    uint32_t ReservedActivation;         /* 836 */
    HV_EFI_AP_STARTUP_STATE ApStartupState; /* 840 */
    uint64_t GuestCr0FixedMask;          /* 872 */
    uint64_t GuestCr4FixedMask;          /* 880 */
    uint32_t EptSplitPageCount;          /* 888 */
    uint32_t EptDeniedPageCount;         /* 892 */
    uint64_t GuestTransitionCodeBase;    /* 896 */
    uint64_t GuestTransitionCodeSize;    /* 904 */
    uint64_t RuntimeToken[4];             /* 912: root-owned boot token */
    uint64_t RuntimeGeneration;           /* 944 */
    uint32_t RuntimeState;                /* 952 */
    uint32_t RuntimeCapabilities;         /* 956 */
    uint64_t PhysicalIommuStatePhysical;   /* 960 */
    uint32_t PhysicalIommuStatePages;      /* 968 */
    uint32_t DmaIsolationMode;             /* 972 */
    uint64_t PhysicalIommuTablePhysical;   /* 976 */
    uint32_t PhysicalIommuTablePages;      /* 984 */
    uint32_t DmaIsolationState;            /* 988 */
    uint64_t AcpiShadowPhysical;           /* 992 */
    uint64_t AcpiGuestRsdpPage;            /* 1000 */
    uint64_t AcpiGuestXsdtPage;            /* 1008 */
    uint64_t RuntimeScannerCodeBase;        /* 1016 */
    uint64_t RuntimeScannerCodeSize;        /* 1024 */
    uint64_t RuntimeScannerEntry;           /* 1032 */
    uint64_t RuntimeScannerStatePhysical;   /* 1040 */
    uint64_t RuntimeScannerRamRangesPhysical; /* 1048 */
    uint32_t RuntimeScannerCodePages;       /* 1056 */
    uint32_t RuntimeScannerRamRangePages;   /* 1060 */
    uint32_t RuntimeScannerRamRangeCount;   /* 1064 */
    uint32_t ReservedRuntimeScanner;        /* 1068 */
    uint64_t RuntimeProcessEntry;           /* 1072 */
    uint64_t RuntimeShadowEntry;            /* 1080 */
    uint64_t ShadowEptPhysical;              /* 1088 */
    uint64_t ShadowEptPointer;               /* 1096 */
    uint64_t ShadowEptStatePhysical;          /* 1104 */
    uint64_t ShadowTargetCr3;                /* 1112 */
    uint32_t ShadowState;                    /* 1120 */
    uint32_t ShadowActive;                   /* 1124 */
    uint64_t ShadowViolationCount;           /* 1128 */
    uint64_t ShadowWriteRedirectCount;       /* 1136 */
    uint64_t RuntimeAnalysisEntry;            /* 1144 */
    uint64_t RuntimeAnalysisStatePhysical;    /* 1152 */
    uint32_t RuntimeAnalysisStatePages;       /* 1160 */
    uint32_t ReservedRuntimeAnalysis;         /* 1164 */
    /* Ring-3 user-mode VMCALL state. */
        uint32_t UserCommandState;                /* 1168 */
} HV_EFI_CONTEXT;

/* Source-compatible descriptive aliases for the guest descriptor fields. */
#define GuestTrBase TrBase
#define GuestGdtrBase GdtrBase
#define GuestIdtrBase IdtrBase

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(offsetof(HV_EFI_ENTRY_FRAME, InitialR15) == 64,
               "EFI entry frame ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, HostStackTop) == 88,
               "EFI context MASM ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, GuestRax) == 408,
               "EFI context GPR ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, State) == 536,
               "EFI context status ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, VmcsActive) == 568,
               "VMCS ordering guard ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, MsrBitmapPhysical) == 640,
               "MSR bitmap ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, HostGdtrBase) == 648,
               "host GDTR ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, HostIdtrBase) == 656,
               "host IDTR ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, HostTrBase) == 664,
               "host TR ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, DescriptorStoragePhysical) == 672,
               "descriptor storage ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, DescriptorStateReady) == 708,
               "descriptor state ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, SecondaryControls) == 712,
               "secondary controls ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ProcessorCount) == 716,
               "processor topology ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ApPreflightStatus) == 732,
               "AP preflight ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ProcessorRecordsPhysical) == 736,
               "processor record pointer ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ProcessorRecordCount) == 748,
               "processor record count ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, CurrentProcessorNumber) == 752,
               "current processor number ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ProcessorResourceStatus) == 760,
               "processor resource status ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ProcessorPreparationStatus) == 772,
               "processor preparation status ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, HostDescriptorStateReady) == 776,
               "host descriptor readiness ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, EptVpidCapabilities) == 784,
               "EPT capability ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ApStartupCapabilitiesReady) == 792,
               "AP startup capability ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, Vpid) == 796,
               "VPID ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, EptPhysical) == 800,
               "EPT storage ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, EptPointer) == 808,
               "EPT pointer ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, EptHierarchyReady) == 816,
               "EPT hierarchy readiness ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, EptWriteBackLeafCount) == 820,
               "EPT memory policy ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ApActivationStatus) == 824,
               "AP activation status ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ApStartupState) == 840,
               "AP startup state ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeToken) == 912,
               "runtime token ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeGeneration) == 944,
               "runtime generation ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeState) == 952,
               "runtime state ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, PhysicalIommuStatePhysical) == 960,
               "physical IOMMU state ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, AcpiShadowPhysical) == 992,
               "ACPI shadow ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeScannerCodeBase) == 1016,
               "runtime scanner code ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeScannerEntry) == 1032,
               "runtime scanner entry ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeScannerStatePhysical) == 1040,
               "runtime scanner state ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeScannerRamRangeCount) == 1064,
               "runtime scanner range ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeProcessEntry) == 1072,
               "runtime process entry ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeShadowEntry) == 1080,
               "runtime shadow entry ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ShadowEptPhysical) == 1088,
               "shadow EPT storage ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, ShadowState) == 1120,
               "shadow EPT state ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeAnalysisEntry) == 1144,
               "runtime analysis entry ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, RuntimeAnalysisStatePhysical) == 1152,
               "runtime analysis state ABI drift");
_Static_assert(offsetof(HV_EFI_CONTEXT, UserCommandState) == 1168,
               "user command state ABI drift");
_Static_assert(sizeof(HV_EFI_MEMORY_DESCRIPTOR_VIEW) == 40,
               "UEFI memory descriptor view ABI drift");
_Static_assert(sizeof(HV_EFI_PROCESSOR_RECORD) == 104,
               "processor record ABI drift");
_Static_assert(sizeof(HV_EFI_CONTEXT) <= HV_EFI_PAGE_SIZE,
               "EFI context must fit in one runtime page");
#endif

/* Pure helpers used by the payload preflight and host tests. */
int HvEfiIsCanonical(uint64_t address);
int HvEfiValidateVmxBasic(uint64_t vmxBasic);
int HvEfiValidateFixedControls(uint64_t fixed0, uint64_t fixed1);
int HvEfiAdjustControls(uint32_t desired, uint64_t capability, uint32_t *out);
int HvEfiNormalizeControlRegister(uint64_t current, uint64_t fixed0,
                                   uint64_t fixed1, uint64_t *out);
int HvEfiVmcsWritePhaseValid(const HV_EFI_CONTEXT *context);
int HvEfiRuntimePageAddressValid(uint64_t address);
int HvEfiCanonicalRangeValid(uint64_t address, uint64_t size);
int HvEfiDescriptorTableLimitValid(uint64_t limit);
int HvEfiSelectorRangeValid(uint16_t selector, uint16_t tableLimit,
                            uint32_t descriptorBytes);
int HvEfiPatchTssDescriptor(uint8_t *gdt, uint16_t tableLimit,
                            uint16_t selector, uint64_t tssBase);
int HvEfiBuildTssDescriptor(uint8_t *gdt, uint16_t tableLimit,
                            uint16_t selector, uint64_t tssBase,
                            uint32_t tssLimit);
int HvEfiBuildIdtGate(uint8_t *idt, uint16_t tableLimit, uint8_t vector,
                      uint64_t stubAddress, uint16_t codeSelector,
                      uint8_t ist);
int HvEfiRewriteIdtGates(uint8_t *idt, uint16_t tableLimit,
                         uint64_t stubAddress, uint16_t codeSelector,
                         uint8_t ist);
int HvEfiRewriteIdtVectorGates(uint8_t *idt, uint16_t tableLimit,
                               uint64_t stubBase, uint32_t stubStride,
                               uint16_t codeSelector, uint8_t ist);
int HvEfiPatchTssStackState(uint8_t *tss, uint32_t tssSize,
                            uint64_t stackTop);
int HvEfiPatchTssStackStateWithIst(uint8_t *tss, uint32_t tssSize,
                                   uint64_t stackTop, uint64_t istTop);
uint64_t HvEfiIdentityPdEntry(uint32_t index);
int HvEfiTopologySnapshotValid(uint64_t processorCount,
                               uint64_t enabledProcessorCount,
                               uint64_t bspProcessorNumber);
int HvEfiApActivationTopologyValid(uint64_t enabledProcessorCount);
int HvEfiApActivationResultValid(uint64_t enabledProcessorCount,
                                 uint64_t preparedProcessorCount,
                                 uint64_t activatedApCount,
                                 uint64_t failureCount);
int HvEfiProcessorRecordPageCount(uint64_t processorCount,
                                  uint32_t *pageCount);
int HvEfiProcessorResourceAddressesValid(
    const HV_EFI_PROCESSOR_RECORD *record);
int HvEfiProcessorPreparationStateValid(
    const HV_EFI_PROCESSOR_RECORD *record,
    const HV_EFI_CONTEXT *context);
int HvEfiApStartupCapabilitiesValid(
    uint64_t secondaryCapabilities,
    uint64_t eptVpidCapabilities);
int HvEfiApVmcsControlsValid(const HV_EFI_CONTEXT *context);
int HvEfiBuildEptPointer(uint64_t pml4Physical,
                         uint64_t eptVpidCapabilities,
                         uint64_t *eptPointer);
int HvEfiBuildIoBitmaps(uint8_t *bitmapA, uint8_t *bitmapB,
                        uint32_t bitmapBytes);
int HvEfiIoBitmapsValid(const uint8_t *bitmapA, const uint8_t *bitmapB,
                        uint32_t bitmapBytes);
int HvEfiEptIdentityPde(uint32_t index, uint32_t memoryType,
                        uint64_t *entry);
int HvEfiBuildEptIdentityHierarchy(
    uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    const uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    uint64_t *eptPointer);
int HvEfiBuildProtectedEptIdentityHierarchy(
    uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    const uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    const HV_EFI_EPT_DENY_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint64_t *eptPointer,
    uint32_t *splitPageCount,
    uint32_t *deniedPageCount);
int HvEfiEptIdentityHierarchyValid(
    const uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    uint64_t eptPointer,
    uint32_t expectedWriteBackLeafCount);
int HvEfiProtectedEptIdentityHierarchyValid(
    const uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    const uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    const HV_EFI_EPT_DENY_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint64_t eptPointer,
    uint32_t expectedWriteBackLeafCount,
    uint32_t expectedSplitPageCount,
    uint32_t expectedDeniedPageCount);
int HvEfiBuildEptMemoryTypePlan(
    const uint8_t *memoryMap,
    uint64_t memoryMapSize,
    uint64_t descriptorSize,
    uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    uint32_t *writeBackLeafCount);
uint32_t HvEfiApplyApStartupExit(
    HV_EFI_AP_STARTUP_STATE *state,
    uint32_t exitReason,
    uint64_t exitQualification);
uint32_t HvEfiApSecondaryControlsForCr0(uint32_t startupControls,
                                        uint64_t guestCr0);
uint64_t HvEfiGuestEferForCr0(uint64_t guestEfer, uint64_t guestCr0);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_HYPERVISOR_H */
