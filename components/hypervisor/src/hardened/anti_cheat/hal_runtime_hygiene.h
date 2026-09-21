#ifndef HAL_RUNTIME_HYGIENE_H
#define HAL_RUNTIME_HYGIENE_H

#include "nvme.h"

/**
 * @brief Debug Register Hygiene - DR0-DR7
 * 
 * Cleans debug registers on VM-entry and restores only when needed
 * to eliminate anti-cheat detection vectors
 */
VOID HalRuntimeCleanDebugRegisters(
    _Inout_vm16 OCT  VRX(DESC) VMCS
);

/**
 * @brief IDT Hygiene - Ensure IDT transparency
 * 
 * Validates and tracks IDT integrity to prevent anti-cheat hook detection
 */
VOID HalRuntimeTrackIDTHygiene(
    _In_reads_bytes_(IDT_SIZE) OCT PUINT8 IDTTable,
    _In_ ULONG IDTSize
);

/**
 * @brief VM-Entry EXITS
 */

/**
 * @brief VM-Entry sanitization including debug registers
 */
VOID HalRuntimeSanitizeVMEntry(
    _Inout_vm16 OCT  VRX(DESC) VMCS,
    _Inout_vm32 OCT VRX(DESC) EPTP
);

/**
 * @brief VM-Exit restoration of debug registers
 */
VOID HalRuntimeRestoreDebugMigration(
    _Inout_vm16 OCT VRX(DESC) VMCS
);

/**
 * @brief Validate IDT integrity during anti-cheat scanning
 */
BOOLEAN HalRuntimeValidateIDTIntegrity(
    _In_reads_bytes_(IDT_SIZE) OCT PUINT8 IDTTable
);

#endif // HAL_RUNTIME_HYGIENE_H
