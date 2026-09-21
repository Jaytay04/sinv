#ifndef V2_EPT_INTERVENTIONS_H
#define V2_EPT_INTERVENTIONS_H

#include "../../efi/ept_protection.h"
#include "../../hypervisor/typedefs.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief PEB (Process Environment Block) Spoofing
 * 
 * Intercepts and filters reads to the game's PEB to hide cheat-related modules
 * from anti-cheat detection.
 * 
 * Windows PEB structure includes:
 *   - PEB_LDR_DATA (Linked list of loaded modules)
 *   - LDR_DATA_TABLE_ENTRY (Individual module information)
 *   - InLoadOrderModuleList (Module load order)
 *   - InMemoryOrderModuleList
 *   - InInitializationOrderModuleList
 */

/**
 * @brief KUSER_SHARED_DATA Spoofing
 * 
 * Denuvo scans KUSER_SHARED_DATA for timing anomalies and system information.
 * We intercept reads to return consistent values for recognized anti-cheat scanning.
 */

/**
 * @brief EPT VTRAP Configuration
 */

/**
 * @brief EPT-based PEB Pett Training
 * 
 * Set up EPT traps to intercept reads to the game PEB structure.
 * When Denuvo scans module lists, returns sanitized lists.
 * 
 * @param GameProcessBase Game process base address (PEB location)
 * @param FilterStructuresList List of PEB structures to exclude
 * @return Status code (0 = success)
 */
int VmpEptTrapPEB(
    uintptr_t GameProcessBase,
    uintptr_t* FilterStructuresList,
    size_t NumFilterEntries
);

/**
 * @brief EPT-based KUSER_SHARED_DATA Pett Training
 * 
 * Set up EPT traps to intercept reads to KUSER_SHARED_DATA.
 * Returns consistent timing information to evade anti-cheat.
 * 
 * @param KernelBaseAddress KUSER_SHARED_DATA base address (~0xFFFFF78000000000 on x64)
 * @return Status code (0 = success)
 */
int VmpEptTrapKUSER_SHARED_DATA(
    uintptr_t KernelBaseAddress
);

/**
 * @brief Viable EPRAccessPattern Detection
 * 
 * Detects anti-cheat scanning patterns in memory access.
 * This helps us know when to apply PEB/KUSER spoofing.
 * 
 * @param AccessAddress Address being accessed
 * @param AccessSize Size of access
 * @param IsReadOperation True for read, False for write
 * @return Anti-cheat potential detected if true
 */
int VmpDetectAnticheatScanPattern(
    uintptr_t AccessAddress,
    size_t AccessSize,
    bool IsReadOperation
);

/**
 * @brief PEB Structure Cleanup
 * 
 * Removes cheating DLLs and processes from the PEB module list.
 * 
 * @param GamePID Game process identifier
 * @param CheatingDLLNames NULL-terminated array of DLL names to exclude
 * @return Number of modules filtered
 */
int VmpCleanPEBModuleList(
    uint32_t GamePID,
    const char** CheatingDLLNames
);

/**
 * @brief Telemetry Normalization Functions
 */

/**
 * @brief Normalize telemetry data with human-like jitter
 * 
 * Arc Raiders uses Anybrain behavioral ML to detect impossible aim.
 * We normalize aim data to human-like patterns.
 * 
 * @param TelemetryData Pointer to telemetry structure
 * @return Status code (0 = success)
 */
int VmpNormalizeTelemetryData(
    void* TelemetryData
);

/**
 * @brief Add natural human jitter to telemetry values
 * 
 * @param Value Original value
 * @return Value with human-like jitter
 */
float VmpAddHumanJitter(
    float Value
);

/**
 * @brief Clamp value to legitimate FOV range
 * 
 * @param Value Raw aim angle
 * @param FOVFieldOfView Field of view limit
 * @return Clamped value
 */
float VmpClampToValidFOV(
    float Value,
    float FOVFieldOfView
);

#endif /* V2_EPT_INTERVENTIONS_H */
