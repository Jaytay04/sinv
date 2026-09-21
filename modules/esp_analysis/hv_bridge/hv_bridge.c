#ifdef MODULE_ESP
#include "hv_bridge.h"
#include "../../../components/hypervisor/src/driver/efi_runtime_companion/hvr1_protocol.h"
#include "../../../components/hypervisor/src/driver/efi_runtime_companion/hvr1_auth.h"
#include "../../../components/hypervisor/src/efi/runtime_analysis_abi.h"
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <string.h>
#include <vector>
namespace {
HANDLE g_device = INVALID_HANDLE_VALUE;
uint8_t g_key[HVR1_AUTH_KEY_BYTES];
uint8_t g_nonce[HVR1_AUTH_NONCE_BYTES];
uint64_t g_gen = 0;
uint64_t g_seq = 0;
const wchar_t kDev[] = L"\\\\\\.\\\\\\HypersinvEfiRuntimeCompanion";
bool Send(const HV_EFI_ANALYSIS_REQUEST& r, HV_EFI_ANALYSIS_RESPONSE* out) {
    if (g_device == INVALID_HANDLE_VALUE) return false;
    std::vector<uint8_t> b(sizeof(HVR1_IOCTL_REQUEST)+sizeof(HV_EFI_ANALYSIS_MAILBOX),0);
    auto* io=reinterpret_cast<HVR1_IOCTL_REQUEST*>(b.data());
    auto* mb=reinterpret_cast<HVR1_MAILBOX*>(b.data()+sizeof(HVR1_IOCTL_REQUEST));
    auto* am=reinterpret_cast<HV_EFI_ANALYSIS_MAILBOX*>(mb->Payload);
    io->Size=sizeof(*io); io->Version=HVR1_PROTOCOL_VERSION;
    io->Operation=HVR1_IOCTL_OPERATION_ANALYSIS; io->ExpectedGeneration=g_gen;
    mb->Magic=HVR1_PROTOCOL_MAGIC; mb->Version=HVR1_PROTOCOL_VERSION;
    mb->HeaderSize=HVR1_MAILBOX_HEADER_BYTES; mb->TotalSize=sizeof(HV_EFI_ANALYSIS_MAILBOX);
    mb->Operation=HVR1_MAILBOX_OPERATION_ANALYSIS; mb->Generation=g_gen; mb->Sequence=g_seq;
    mb->PayloadOffset=HVR1_MAILBOX_HEADER_BYTES; mb->PayloadSize=sizeof(HV_EFI_ANALYSIS_MAILBOX);
    am->Magic=HV_EFI_ANALYSIS_MAGIC; am->Version=HV_EFI_ANALYSIS_VERSION;
    am->HeaderSize=offsetof(HV_EFI_ANALYSIS_MAILBOX,Request);
    am->State=HV_EFI_ANALYSIS_MAILBOX_REQUEST_READY; am->Generation=g_gen;
    am->Request=r; am->Request.Size=sizeof(am->Request);
    am->Request.Version=HV_EFI_ANALYSIS_VERSION; am->Request.Sequence=g_seq;
    if(!Hvr1ComputeMailboxAuthTag(g_key,g_nonce,mb,mb->AuthTag)) return false;
    DWORD got=0;
    if(!DeviceIoControl(g_device,HVR1_IOCTL_SUBMIT,b.data(),(DWORD)b.size(),b.data(),(DWORD)b.size(),&got,NULL))
        return false;
    mb=reinterpret_cast<HVR1_MAILBOX*>(b.data());
    am=reinterpret_cast<HV_EFI_ANALYSIS_MAILBOX*>(mb->Payload);
    if(!Hvr1VerifyMailboxAuthTag(g_key,g_nonce,mb) ||
        am->State!=HV_EFI_ANALYSIS_MAILBOX_RESPONSE_READY) return false;
    g_seq=am->Request.Sequence+1;
    *out=am->Response;
    return true;
}
}
extern "C" {
bool HvBridgeOpen(const wchar_t* device) {
    if (device == nullptr) device = kDev;
    HvBridgeClose();
    HANDLE h = CreateFileW(device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    HVR1_MAILBOX_DESCRIPTOR d{};
    DWORD got = NULL;
    if (!DeviceIoControl(h, HVR1_IOCTL_GET_MAILBOX, NULL, 0, &d, sizeof(d), &got, NULL) ||
        got != sizeof(d)) {
        CloseHandle(h);
        return false;
    }
    g_device = h;
    memcpy(g_key, d.SessionKey, sizeof(g_key));
    memcpy(g_nonce, d.SessionNonce, sizeof(g_nonce));
    g_gen = d.Generation;
    g_seq = d.NextSequence;
    return true;
}
void HvBridgeClose() {
    if (g_device != INVALID_HANDLE_VALUE) {
        CloseHandle(g_device);
        g_device = INVALID_HANDLE_VALUE;
    }
    memset(&g_key, 0, sizeof(g_key));
    memset(&g_nonce, 0, sizeof(g_nonce));
    g_gen = 0;
    g_seq = 0;
}
bool HvAnalysisBackendStart(uint32_t pid) {
    if (!pid) return false;
    HV_EFI_ANALYSIS_REQUEST r{};
    r.Operation = HV_EFI_ANALYSIS_START;
    r.ProcessId = pid;
    HV_EFI_ANALYSIS_RESPONSE out{};
    if (!Send(r, &out)) return false;
    return out.Status == HV_EFI_ANALYSIS_SUCCESS ||
           out.Status == HV_EFI_ANALYSIS_MORE_DATA;
}
bool HvAnalysisBackendStop(uint32_t pid) {
    if (!pid) return false;
    HV_EFI_ANALYSIS_REQUEST r{};
    r.Operation = HV_EFI_ANALYSIS_STOP;
    r.ProcessId = pid;
    HV_EFI_ANALYSIS_RESPONSE out{};
    (void)Send(r, &out);
    return true;
}
bool HvAnalysisBackendRead(uint32_t pid, uint64_t address, uint8_t* dst, uint32_t maxsize) {
    if (!pid || !dst || maxsize == 0 ||
        maxsize > HV_EFI_ANALYSIS_MAX_STRING_BYTES) {
        return false;
    }
    HV_EFI_ANALYSIS_REQUEST r{};
    r.Operation = HV_EFI_ANALYSIS_EXTRACT;
    r.ProcessId = pid;
    r.Address = address;
    r.ElementCount = maxsize;
    r.FieldName[0] = 'r';
    HV_EFI_ANALYSIS_RESPONSE out{};
    if (!Send(r, &out) || out.ReturnedFieldCount == 0) return false;
    uint32_t n = out.Fields[0].ValueBytes;
    if (n > maxsize) n = maxsize;
    memcpy(dst, out.Fields[0].Value.Bytes, n);
    return true;
}
bool HvAnalysisBackendScan(uint32_t pid, const EspScanSignature* sigs, uint32_t sigcount, uint32_t scanmode, uint64_t maxbytes, uint64_t* out_addrs, uint32_t maxresults, uint32_t* outcount) {
    if (!pid || !sigs || sigcount == 0 || !out_addrs ||
        maxresults == 0) {
        return false;
    }
    HV_EFI_ANALYSIS_REQUEST r{};
    r.Operation = HV_EFI_ANALYSIS_SCAN;
    r.ProcessId = pid;
    r.ScanMode = scanmode ? scanmode : HV_EFI_ANALYSIS_SCAN_FIRST;
    r.SignatureCount = sigcount > HV_EFI_ANALYSIS_MAX_SIGNATURES ?
        HV_EFI_ANALYSIS_MAX_SIGNATURES : sigcount;
    r.MaxBytes = maxbytes ? maxbytes : HV_EFI_ANALYSIS_DEFAULT_SCAN_BYTES;
    for (uint32_t i = 0; i < r.SignatureCount; ++i) {
        auto* s = &r.Signatures[i];
        if (sigs[i].len == 0 ||
            sigs[i].len > HV_EFI_ANALYSIS_PATTERN_BYTES) {
            return false;
        }
        memcpy(s->Identifier, sigs[i].id, sizeof(s->Identifier));
        memcpy(s->Pattern, sigs[i].pattern, sigs[i].len);
        if (sigs[i].mask) memcpy(s->Mask, sigs[i].mask, sigs[i].len);
        s->Length = sigs[i].len;
        s->DataOffset = sigs[i].dataoff;
        s->Flags = sigs[i].flags;
    }
    HV_EFI_ANALYSIS_RESPONSE out{};
    if (!Send(r, &out)) return false;
    uint32_t n = out.ReturnedResultCount;
    if (n > maxresults) n = maxresults;
    for (uint32_t i = 0; i < n; ++i)
        out_addrs[i] = out.Results[i].GuestVirtualAddress;
    if (outcount) *outcount = out.TotalResultCount;
    return true;
}
#endif
}
