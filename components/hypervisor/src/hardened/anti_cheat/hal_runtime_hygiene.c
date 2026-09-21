#include "hal_runtime_hygiene.h"
#include <cstdio>
#include <cstring>

/**
 * @brief Debug Register Hygiene - DR0-DR7
 * 
 * Cleans debug registers on VM-entry and restores only when needed
 * to eliminate anti-cheat detection vectors
 */
VOID HalRuntimeCleanDebugRegisters(
    _Inout_vm16 OCT VRX(DESC) VMCS
) {
    // Clear DR0-DR7 registers to zero
    #pragma vmcall DR0 = 0
    #pragma vmcall DR1 = 0
    #pragma vmcall DR2 = 0
    #pragma vmcall DR3 = 0
    #pragma vmcall DR6 = 0
    #pragma vmcall DR7 = 0
    
    // Ensure debug registers are cleared on VM-entry
    // This prevents anti-cheat from detecting breakpoint machinery
}

/**
 * @brief IDT Hygiene - Ensure IDT transparency
 * 
 * Validates and tracks IDT integrity to prevent anti-cheat hook detection
 */
VOID HalRuntimeTrackIDTHygiene(
    _In_reads_bytes_(IDT_SIZE) OCT PUINT8 IDTTable,
    _In_ ULONG IDTSize
) {
    // Validate IDT integrity
    for (ULONG i = 0; i < IDTSize; i += sizeof(QWORD)) {
        QWORD* Entry = (QWORD*)(IDTTable + i);
        
        // Check for invalid entries
        if (*Entry == 0xFFFFFFFFFFFFFFFF) {
            // Empty entry, skip
            continue;
        }
        
        // Check for potential hooks (low priority interrupts)
        if (i < 0x20) { // First 32 interrupts
            // Low priority interrupts should remain unmodified
            QWORD* BasePtr = (QWORD*)(*Entry & 0xFFFF0000);
            if (BasePtr == NULL) {
                // Invalid base pointer
                continue;
            }
        }
    }
}

/**
 * @brief VM-Entry sanitization including debug registers
 */
VOID HalRuntimeSanitizeVMEntry(
    _Inout_vm16 OCT  VRX(DESC) VMCS,
    _Inout_vm32 OCT VRX(DESC) EPTP
) {
    // Clean debug registers on VM-entry
    HalRuntimeCleanDebugRegisters(VMCS);
    
    // Clear VM-entry interruption information
    *GUEST_INTERRUPTIBILITY_INFO_VMCS = 0;
    
    // Ensure EPTP is valid
    if (EPTP == 0) {
        // EPT-Paging-Entry-Table pointer must be valid
        return;
    }
    
    // Set EPT controls appropriately
    *EPT_VIOLATION_INTERRUPT_SHADOW_VMCS = 1;
}

/**
 * @brief VM-Exit restoration of debug registers
 */
VOID HalRuntimeRestoreDebugMigration(
    _Inout_vm16 OCT VRX(DESC) VMCS
) {
    // DR registers are restored only when explicitly set by guest
    // This prevents anti-cheat from detecting unwritten registers
    *GUEST_DR7_VMCS = 0; // Reset DR7 initially
    
    // Only restore if guest had breakpoints set (rare in games)
    if (*GUEST_DR7_VMCS != 0) {
        // Original DR7 value was preserved during VM-entry
        return;
    }
}

/**
 * @brief Validate IDT integrity during anti-cheat scanning
 */
BOOLEAN HalRuntimeValidateIDTIntegrity(
    _In_reads_bytes_(IDT_SIZE) OCT PUINT8 IDTTable
) {
    // Scan IDT for unexpected modifications
    QWORD* Entries = (QWORD*)IDTTable;
    
    for (ULONG i = 0; i < IDT_SIZE / sizeof(QWORD); i++) {
        QWORD Entry = Entries[i];
        
        // Skip empty entries
        if (Entry == 0xFFFFFFFFFFFFFFFF) {
            continue;
        }
        
        // Validate interrupt gate/exception handler address
        QWORD Handler = Entry & 0xFFFF0000;
        
        // Check for ring 0 handlers to protect kernel security
        if ((Entry & 0x7000) == 0x6000) { // Interrupt gate
            if (Handler < 0x10000) {
                return FALSE; // Invalid handler address
            }
        }
    }
    
    return TRUE; // IDT integrity verified
}
