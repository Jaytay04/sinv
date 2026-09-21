/*
 * HVR1 authenticated-mailbox transport for the ESP module.
 *
 * Replaces the placeholder Hv* fakes. Talks to the existing companion
 * device using the same HMAC/mailbox protocol as hvr1_service.cpp, so
 * the module issues real ANALYSIS EXTRACT/SCAN/START/STOP commands
 * to the EFI payload instead of reading an empty in-memory fake.
 */
#ifndef HYPERSINV_ESP_HV_BRIDGE_H
#define HYPERSINV_ESP_HV_BRIDGE_H
#ifdef MODULE_ESP
#include <cstdint>
#ifdef __cplusplus
extern "C" {
typedef struct {
    char id[32];
    const uint8_t* pattern;
    const uint8_t* mask;
    uint32_t len;
    int32_t dataoff;
    uint32_t region;
    uint32_t flags;
} EspScanSignature;
bool HvBridgeOpen(const wchar_t* device);
void HvBridgeClose(void);
bool HvAnalysisBackendStart(uint32_t pid);
bool HvAnalysisBackendStop(uint32_t pid);
bool HvAnalysisBackendRead(uint32_t pid, uint64_t address, uint8_t* dst, uint32_t maxsize);
bool HvAnalysisBackendScan(uint32_t pid, const EspScanSignature* sigs, uint32_t sigcount, uint32_t scanmode, uint64_t maxbytes, uint64_t* out_addrs, uint32_t maxresults, uint32_t* outcount);
}
#endif
#endif
#endif
