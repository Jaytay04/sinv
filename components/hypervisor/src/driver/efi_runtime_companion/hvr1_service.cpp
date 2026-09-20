/*
 * LocalSystem service and test CLI for the standalone HVR1 WDM companion.
 *
 * The service is deliberately a normal Win32 process.  It does not install
 * itself, load a driver, or use a private key.  The service account must be
 * LocalSystem because the companion device ACL grants access only to SYSTEM.
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstddef>
#include <cerrno>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "hvr1_protocol.h"
#include "hvr1_auth.h"
#include "runtime_analysis_abi.h"
#include "runtime_scanner.h"
#include "runtime_process_abi.h"
#include "runtime_shadow_abi.h"

namespace {

constexpr wchar_t kServiceName[] = L"HypersinvHvr1RuntimeCompanion";
constexpr wchar_t kDevicePath[] =
    L"\\\\.\\HypersinvEfiRuntimeCompanion";
constexpr wchar_t kRecoveryKeyPath[] =
    L"SOFTWARE\\Hypersinv\\Hvr1RuntimeCompanion";
constexpr wchar_t kRecoveryValueName[] = L"Lifecycle";
constexpr wchar_t kBootSessionKeyPath[] =
    L"SOFTWARE\\Hypersinv\\Hvr1RuntimeCompanion\\BootSession";
constexpr wchar_t kBootSessionValueName[] = L"Identity";
constexpr wchar_t kPowerRequestReason[] =
    L"Hypersinv EFI runtime must detach before system sleep";

constexpr std::uint32_t kRecoveryMagic = UINT32_C(0x31565248);
constexpr std::uint32_t kRecoveryVersion = 2;
constexpr std::uint32_t kMaxSubmitBytes =
    static_cast<std::uint32_t>(sizeof(HVR1_IOCTL_REQUEST) +
                               HVR1_MAILBOX_HEADER_BYTES +
        (sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX) >
             sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX)
         ? sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX)
         : sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX)));

const wchar_t *ScanDiagnosticName(std::uint32_t diagnostic);

void SelfTestDiagnostic(const char *phase)
{
    if (phase == nullptr)
        return;
    HANDLE port = CreateFileW(L"\\\\.\\COM2", GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (port == INVALID_HANDLE_VALUE)
        return;
    COMMTIMEOUTS timeouts{};
    timeouts.WriteTotalTimeoutConstant = 1000;
    (void)SetCommTimeouts(port, &timeouts);
    DCB state{};
    state.DCBlength = sizeof(state);
    if (GetCommState(port, &state)) {
        state.BaudRate = CBR_115200;
        state.ByteSize = 8;
        state.Parity = NOPARITY;
        state.StopBits = ONESTOPBIT;
        (void)SetCommState(port, &state);
    }
    char message[96]{};
    int length = sprintf_s(message, sizeof(message), "HVR1-DIAG:%s\r\n",
                           phase);
    if (length > 0) {
        DWORD written = 0;
        (void)WriteFile(port, message, static_cast<DWORD>(length), &written,
                        nullptr);
    }
    CloseHandle(port);
}

enum class RecoveryState : std::uint32_t {
    Clean = 0,
    Starting = 1,
    Detaching = 2,
    Offline = 3,
    Quarantined = 4
};

#pragma pack(push, 1)
struct RecoveryRecord {
    std::uint32_t Magic;
    std::uint32_t Version;
    std::uint32_t State;
    std::uint32_t Reserved;
    std::uint64_t Generation;
    std::uint64_t Sequence;
    std::uint32_t ExpectedCpuCount;
    std::uint32_t AcknowledgedCpuCount;
    std::int32_t LastError;
    std::uint32_t Reserved2;
    std::uint8_t BootIdentity[16];
};
#pragma pack(pop)

static_assert(sizeof(RecoveryRecord) == 64, "recovery record ABI drift");
static_assert(kMaxSubmitBytes <= HVR1_MAILBOX_BYTES +
                  sizeof(HVR1_IOCTL_REQUEST),
              "submit bound drift");

struct DeviceStatus {
    HVR1_IOCTL_STATUS Value{};
    DWORD Win32Error = ERROR_SUCCESS;
};

bool IsUnsafeRecoveryState(RecoveryState state)
{
    return state == RecoveryState::Starting ||
           state == RecoveryState::Detaching ||
           state == RecoveryState::Quarantined;
}

const wchar_t *RecoveryStateName(RecoveryState state)
{
    switch (state) {
    case RecoveryState::Clean:
        return L"CLEAN";
    case RecoveryState::Starting:
        return L"STARTING";
    case RecoveryState::Detaching:
        return L"DETACHING";
    case RecoveryState::Offline:
        return L"OFFLINE";
    case RecoveryState::Quarantined:
        return L"QUARANTINED";
    default:
        return L"INVALID";
    }
}

void Report(const std::wstring &message)
{
    OutputDebugStringW(message.c_str());
    OutputDebugStringW(L"\n");
}

void ReportError(const wchar_t *operation, DWORD error)
{
    wchar_t buffer[256]{};
    (void)swprintf_s(buffer, _countof(buffer), L"HVR1 %ls failed: %lu",
                     operation, static_cast<unsigned long>(error));
    Report(buffer);
    std::fwprintf(stderr, L"%ls\n", buffer);
}

class RecoveryStore final {
public:
    RecoveryStore() = default;
    RecoveryStore(const RecoveryStore &) = delete;
    RecoveryStore &operator=(const RecoveryStore &) = delete;

    ~RecoveryStore()
    {
        SecureZeroMemory(bootIdentity_, sizeof(bootIdentity_));
        if (bootKey_ != nullptr)
            RegCloseKey(bootKey_);
        if (key_ != nullptr)
            RegCloseKey(key_);
    }

    LONG Open(bool writable)
    {
        if (key_ != nullptr)
            return ERROR_SUCCESS;

        REGSAM access = KEY_QUERY_VALUE;
        if (writable)
            access |= KEY_SET_VALUE;
        DWORD disposition = 0;
        LONG result;
        if (writable) {
            result = RegCreateKeyExW(
                HKEY_LOCAL_MACHINE,
                kRecoveryKeyPath,
                0,
                nullptr,
                REG_OPTION_NON_VOLATILE,
                access,
                nullptr,
                &key_,
                &disposition);
            if (result != ERROR_SUCCESS)
                return result;
            return OpenBootSession();
        }
        return RegOpenKeyExW(HKEY_LOCAL_MACHINE, kRecoveryKeyPath, 0,
                             access, &key_);
    }

    LONG Read(RecoveryRecord *record, bool *present)
    {
        if (record == nullptr || present == nullptr)
            return ERROR_INVALID_PARAMETER;
        *present = false;
        std::memset(record, 0, sizeof(*record));

        DWORD type = 0;
        DWORD bytes = sizeof(*record);
        LONG result = RegQueryValueExW(
            key_, kRecoveryValueName, nullptr, &type,
            reinterpret_cast<LPBYTE>(record), &bytes);
        if (result == ERROR_FILE_NOT_FOUND)
            return ERROR_SUCCESS;
        if (result != ERROR_SUCCESS)
            return result;
        if (type != REG_BINARY || bytes != sizeof(*record) ||
            record->Magic != kRecoveryMagic ||
            record->Version != kRecoveryVersion || record->Reserved != 0 ||
            record->Reserved2 != 0 || record->State >
                static_cast<std::uint32_t>(RecoveryState::Quarantined)) {
            return ERROR_INVALID_DATA;
        }
        *present = true;
        return ERROR_SUCCESS;
    }

    LONG Write(const RecoveryRecord &record)
    {
        if (key_ == nullptr)
            return ERROR_INVALID_HANDLE;
        return RegSetValueExW(
            key_, kRecoveryValueName, 0, REG_BINARY,
            reinterpret_cast<const BYTE *>(&record), sizeof(record));
    }

    LONG Begin(RecoveryRecord *previous, bool *hadPrevious,
               RecoveryRecord *current)
    {
        LONG result = Open(true);
        if (result != ERROR_SUCCESS)
            return result;
        result = Read(previous, hadPrevious);
        if (result != ERROR_SUCCESS)
            return result;
        if (*hadPrevious && IsUnsafeRecoveryState(
                static_cast<RecoveryState>(previous->State)) &&
            std::memcmp(previous->BootIdentity, bootIdentity_,
                        sizeof(bootIdentity_)) == 0) {
            return ERROR_INVALID_STATE;
        }
        if (*hadPrevious && previous->Sequence == UINT64_MAX)
            return ERROR_ARITHMETIC_OVERFLOW;

        std::memset(current, 0, sizeof(*current));
        current->Magic = kRecoveryMagic;
        current->Version = kRecoveryVersion;
        current->State = static_cast<std::uint32_t>(RecoveryState::Starting);
        current->Sequence = *hadPrevious && previous->Sequence != UINT64_MAX
            ? previous->Sequence + 1
            : 1;
        std::memcpy(current->BootIdentity, bootIdentity_,
                    sizeof(current->BootIdentity));
        return Write(*current);
    }

private:
    LONG OpenBootSession()
    {
        DWORD disposition = 0;
        LONG result = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE, kBootSessionKeyPath, 0, nullptr,
            REG_OPTION_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr,
            &bootKey_, &disposition);
        if (result != ERROR_SUCCESS)
            return result;

        DWORD type = 0;
        DWORD bytes = sizeof(bootIdentity_);
        result = RegQueryValueExW(
            bootKey_, kBootSessionValueName, nullptr, &type,
            bootIdentity_, &bytes);
        if (result == ERROR_FILE_NOT_FOUND) {
            if (BCryptGenRandom(nullptr, bootIdentity_, sizeof(bootIdentity_),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
                return ERROR_GEN_FAILURE;
            }
            return RegSetValueExW(
                bootKey_, kBootSessionValueName, 0, REG_BINARY,
                bootIdentity_, sizeof(bootIdentity_));
        }
        if (result != ERROR_SUCCESS)
            return result;
        if (type != REG_BINARY || bytes != sizeof(bootIdentity_))
            return ERROR_INVALID_DATA;
        std::uint8_t aggregate = 0;
        for (std::uint8_t value : bootIdentity_)
            aggregate = static_cast<std::uint8_t>(aggregate | value);
        return aggregate == 0 ? ERROR_INVALID_DATA : ERROR_SUCCESS;
    }

    HKEY key_ = nullptr;
    HKEY bootKey_ = nullptr;
    std::uint8_t bootIdentity_[16]{};
};

class DeviceHandle final {
public:
    DeviceHandle() = default;
    DeviceHandle(const DeviceHandle &) = delete;
    DeviceHandle &operator=(const DeviceHandle &) = delete;

    ~DeviceHandle()
    {
        Close();
    }

    bool Open()
    {
        Close();
        handle_ = CreateFileW(
            kDevicePath,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        return handle_ != INVALID_HANDLE_VALUE;
    }

    void Close()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    bool IsOpen() const
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    bool GetMailbox(HVR1_MAILBOX_DESCRIPTOR *descriptor, DWORD *error)
    {
        DWORD returned = 0;
        if (DeviceIoControl(handle_, HVR1_IOCTL_GET_MAILBOX,
                            nullptr, 0, descriptor, sizeof(*descriptor),
                            &returned, nullptr) &&
            returned == sizeof(*descriptor)) {
            return true;
        }
        if (error != nullptr)
            *error = GetLastError();
        return false;
    }

    bool Query(DeviceStatus *status)
    {
        if (status == nullptr)
            return false;
        std::memset(&status->Value, 0, sizeof(status->Value));
        DWORD returned = 0;
        if (DeviceIoControl(handle_, HVR1_IOCTL_QUERY_STATUS,
                            nullptr, 0, &status->Value,
                            sizeof(status->Value), &returned, nullptr) &&
            returned == sizeof(status->Value)) {
            status->Win32Error = ERROR_SUCCESS;
            return true;
        }
        status->Win32Error = GetLastError();
        return false;
    }

    bool SubmitForSelfTest(const HVR1_MAILBOX_DESCRIPTOR &descriptor,
                           std::uint64_t sequence,
                           bool corruptAuthentication,
                           DeviceStatus *status)
    {
        if (status == nullptr || descriptor.Generation == 0 || sequence == 0)
            return false;

        std::vector<std::uint8_t> buffer(kMaxSubmitBytes, 0);
        auto *request = reinterpret_cast<HVR1_IOCTL_REQUEST *>(buffer.data());
        auto *mailbox = reinterpret_cast<HVR1_MAILBOX *>(
            buffer.data() + sizeof(HVR1_IOCTL_REQUEST));
        request->Size = sizeof(*request);
        request->Version = HVR1_PROTOCOL_VERSION;
        request->Operation = HVR1_IOCTL_OPERATION_SHUTDOWN;
        request->Flags = 0;
        request->ExpectedGeneration = descriptor.Generation;

        mailbox->Magic = HVR1_PROTOCOL_MAGIC;
        mailbox->Version = HVR1_PROTOCOL_VERSION;
        mailbox->HeaderSize = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->TotalSize = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->Operation = HVR1_MAILBOX_OPERATION_RUNTIME_FRAME;
        mailbox->Flags = 0;
        mailbox->Generation = descriptor.Generation;
        mailbox->Sequence = sequence;
        mailbox->PayloadOffset = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->PayloadSize = 0;
        mailbox->Status = 0;
        mailbox->Reserved = 0;
        if (!Hvr1ComputeMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce,
                mailbox, mailbox->AuthTag)) {
            status->Win32Error = ERROR_INVALID_DATA;
            return false;
        }
        if (corruptAuthentication)
            mailbox->AuthTag[0] ^= UINT8_C(0x80);

        DWORD returned = 0;
        BOOL ok = DeviceIoControl(
            handle_, HVR1_IOCTL_SUBMIT,
            buffer.data(), static_cast<DWORD>(buffer.size()),
            buffer.data(), static_cast<DWORD>(sizeof(HVR1_IOCTL_STATUS)),
            &returned, nullptr);
        std::memset(&status->Value, 0, sizeof(status->Value));
        if (returned >= sizeof(status->Value))
            std::memcpy(&status->Value, buffer.data(), sizeof(status->Value));
        status->Win32Error = ok ? ERROR_SUCCESS : GetLastError();
        return ok && returned >= sizeof(status->Value);
    }

    bool Submit(const HVR1_MAILBOX_DESCRIPTOR &descriptor,
                std::uint64_t sequence,
                DeviceStatus *status)
    {
        return SubmitForSelfTest(descriptor, sequence, false, status);
    }

    bool Scan(const HVR1_MAILBOX_DESCRIPTOR &descriptor,
              const HV_EFI_RUNTIME_SCAN_REQUEST &scanRequest,
              HV_EFI_RUNTIME_SCAN_RESPONSE *response,
              DWORD *error)
    {
        if (response == nullptr || error == nullptr ||
            descriptor.Generation == 0 || descriptor.NextSequence == 0)
            return false;

        const std::uint32_t mailboxBytes =
            HVR1_MAILBOX_HEADER_BYTES +
            static_cast<std::uint32_t>(sizeof(HV_EFI_RUNTIME_SCAN_MAILBOX));
        std::vector<std::uint8_t> buffer(
            sizeof(HVR1_IOCTL_REQUEST) + mailboxBytes, 0);
        auto *ioctlRequest =
            reinterpret_cast<HVR1_IOCTL_REQUEST *>(buffer.data());
        auto *mailbox = reinterpret_cast<HVR1_MAILBOX *>(
            buffer.data() + sizeof(HVR1_IOCTL_REQUEST));
        auto *scan = reinterpret_cast<HV_EFI_RUNTIME_SCAN_MAILBOX *>(
            mailbox->Payload);

        ioctlRequest->Size = sizeof(*ioctlRequest);
        ioctlRequest->Version = HVR1_PROTOCOL_VERSION;
        ioctlRequest->Operation = HVR1_IOCTL_OPERATION_SCAN;
        ioctlRequest->ExpectedGeneration = descriptor.Generation;
        mailbox->Magic = HVR1_PROTOCOL_MAGIC;
        mailbox->Version = HVR1_PROTOCOL_VERSION;
        mailbox->HeaderSize = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->TotalSize = mailboxBytes;
        mailbox->Operation = HVR1_MAILBOX_OPERATION_RUNTIME_SCAN;
        mailbox->Generation = descriptor.Generation;
        mailbox->Sequence = descriptor.NextSequence;
        mailbox->PayloadOffset = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->PayloadSize = sizeof(*scan);

        scan->Magic = HV_EFI_RUNTIME_SCAN_MAGIC;
        scan->Version = HV_EFI_RUNTIME_SCAN_VERSION;
        scan->HeaderSize = static_cast<std::uint16_t>(
            offsetof(HV_EFI_RUNTIME_SCAN_MAILBOX, Request));
        scan->State = HV_EFI_RUNTIME_SCAN_MAILBOX_REQUEST_READY;
        scan->Generation = descriptor.Generation;
        scan->Request = scanRequest;
        scan->Request.Sequence = descriptor.NextSequence;

        if (!Hvr1ComputeMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce,
                mailbox, mailbox->AuthTag)) {
            *error = ERROR_INVALID_DATA;
            return false;
        }

        DWORD returned = 0;
        BOOL ok = DeviceIoControl(
            handle_, HVR1_IOCTL_SUBMIT,
            buffer.data(), static_cast<DWORD>(buffer.size()),
            buffer.data(), static_cast<DWORD>(buffer.size()),
            &returned, nullptr);
        if (!ok) {
            *error = GetLastError();
            return false;
        }
        mailbox = reinterpret_cast<HVR1_MAILBOX *>(buffer.data());
        scan = reinterpret_cast<HV_EFI_RUNTIME_SCAN_MAILBOX *>(
            mailbox->Payload);
        if (returned != mailboxBytes ||
            mailbox->Magic != HVR1_PROTOCOL_MAGIC ||
            mailbox->TotalSize != mailboxBytes ||
            mailbox->Operation != HVR1_MAILBOX_OPERATION_RUNTIME_SCAN ||
            mailbox->Generation != descriptor.Generation ||
            mailbox->Sequence != descriptor.NextSequence ||
            mailbox->PayloadSize != sizeof(*scan) ||
            !Hvr1VerifyMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce, mailbox) ||
            scan->Magic != HV_EFI_RUNTIME_SCAN_MAGIC ||
            scan->Version != HV_EFI_RUNTIME_SCAN_VERSION ||
            scan->State != HV_EFI_RUNTIME_SCAN_MAILBOX_RESPONSE_READY ||
            scan->Generation != descriptor.Generation ||
            scan->Response.Size != sizeof(scan->Response) ||
            scan->Response.Version != HV_EFI_RUNTIME_SCAN_VERSION ||
            scan->Response.Generation != descriptor.Generation ||
            scan->Response.Sequence != descriptor.NextSequence ||
            scan->Response.ResultCount > HV_EFI_RUNTIME_SCAN_MAX_RESULTS ||
            scan->Response.Status != mailbox->Status) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        *response = scan->Response;
        *error = ERROR_SUCCESS;
        return true;
    }

    bool LocateProcess(
        const HVR1_MAILBOX_DESCRIPTOR &descriptor,
        const char *imageName,
        std::size_t imageNameLength,
        HV_EFI_RUNTIME_PROCESS_RESPONSE *response,
        DWORD *error)
    {
        if (imageName == nullptr || imageNameLength == 0 ||
            imageNameLength > 15 || response == nullptr || error == nullptr ||
            descriptor.Generation == 0 || descriptor.NextSequence == 0)
            return false;
        const std::uint32_t mailboxBytes = HVR1_MAILBOX_HEADER_BYTES +
            static_cast<std::uint32_t>(
                sizeof(HV_EFI_RUNTIME_PROCESS_MAILBOX));
        std::vector<std::uint8_t> buffer(
            sizeof(HVR1_IOCTL_REQUEST) + mailboxBytes, 0);
        auto *ioctlRequest =
            reinterpret_cast<HVR1_IOCTL_REQUEST *>(buffer.data());
        auto *mailbox = reinterpret_cast<HVR1_MAILBOX *>(
            buffer.data() + sizeof(HVR1_IOCTL_REQUEST));
        auto *process = reinterpret_cast<HV_EFI_RUNTIME_PROCESS_MAILBOX *>(
            mailbox->Payload);
        ioctlRequest->Size = sizeof(*ioctlRequest);
        ioctlRequest->Version = HVR1_PROTOCOL_VERSION;
        ioctlRequest->Operation = HVR1_IOCTL_OPERATION_PROCESS_LOCATE;
        ioctlRequest->ExpectedGeneration = descriptor.Generation;
        mailbox->Magic = HVR1_PROTOCOL_MAGIC;
        mailbox->Version = HVR1_PROTOCOL_VERSION;
        mailbox->HeaderSize = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->TotalSize = mailboxBytes;
        mailbox->Operation = HVR1_MAILBOX_OPERATION_PROCESS_LOCATE;
        mailbox->Generation = descriptor.Generation;
        mailbox->Sequence = descriptor.NextSequence;
        mailbox->PayloadOffset = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->PayloadSize = sizeof(*process);
        process->Magic = HV_EFI_RUNTIME_PROCESS_MAGIC;
        process->Version = HV_EFI_RUNTIME_PROCESS_VERSION;
        process->HeaderSize = static_cast<std::uint16_t>(
            offsetof(HV_EFI_RUNTIME_PROCESS_MAILBOX, Request));
        process->State = HV_EFI_RUNTIME_PROCESS_MAILBOX_REQUEST_READY;
        process->Generation = descriptor.Generation;
        process->Request.Size = sizeof(process->Request);
        process->Request.Version = HV_EFI_RUNTIME_PROCESS_VERSION;
        process->Request.NameLength =
            static_cast<std::uint16_t>(imageNameLength);
        process->Request.Sequence = descriptor.NextSequence;
        std::memcpy(process->Request.ImageFileName,
                    imageName, imageNameLength);
        if (!Hvr1ComputeMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce,
                mailbox, mailbox->AuthTag)) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(
            handle_, HVR1_IOCTL_SUBMIT,
            buffer.data(), static_cast<DWORD>(buffer.size()),
            buffer.data(), static_cast<DWORD>(buffer.size()),
            &returned, nullptr);
        if (!ok) {
            *error = GetLastError();
            DeviceStatus diagnostic{};
            if (Query(&diagnostic) && diagnostic.Value.Reserved != 0) {
                const std::uint32_t validation =
                    diagnostic.Value.Reserved & UINT32_C(0xff);
                const std::uint32_t rootStatus =
                    (diagnostic.Value.Reserved >> 8) & UINT32_C(0xff);
                const std::uint32_t mailboxState =
                    (diagnostic.Value.Reserved >> 16) & UINT32_C(0xff);
                std::fwprintf(
                    stderr,
                    L"HVR1 process response validation: %ls (%lu), "
                    L"root-status=%lu, mailbox-state=%lu\n",
                    ScanDiagnosticName(validation),
                    static_cast<unsigned long>(validation),
                    static_cast<unsigned long>(rootStatus),
                    static_cast<unsigned long>(mailboxState));
            }
            return false;
        }
        mailbox = reinterpret_cast<HVR1_MAILBOX *>(buffer.data());
        process = reinterpret_cast<HV_EFI_RUNTIME_PROCESS_MAILBOX *>(
            mailbox->Payload);
        if (returned != mailboxBytes ||
            mailbox->Magic != HVR1_PROTOCOL_MAGIC ||
            mailbox->TotalSize != mailboxBytes ||
            mailbox->Operation != HVR1_MAILBOX_OPERATION_PROCESS_LOCATE ||
            mailbox->Generation != descriptor.Generation ||
            mailbox->Sequence != descriptor.NextSequence ||
            mailbox->PayloadSize != sizeof(*process) ||
            !Hvr1VerifyMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce, mailbox) ||
            process->Magic != HV_EFI_RUNTIME_PROCESS_MAGIC ||
            process->Version != HV_EFI_RUNTIME_PROCESS_VERSION ||
            process->State != HV_EFI_RUNTIME_PROCESS_MAILBOX_RESPONSE_READY ||
            process->Generation != descriptor.Generation ||
            process->Response.Size != sizeof(process->Response) ||
            process->Response.Version != HV_EFI_RUNTIME_PROCESS_VERSION ||
            process->Response.Generation != descriptor.Generation ||
            process->Response.Sequence != descriptor.NextSequence ||
            process->Response.Status != mailbox->Status) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        *response = process->Response;
        *error = ERROR_SUCCESS;
        return true;
    }

    bool Shadow(const HVR1_MAILBOX_DESCRIPTOR &descriptor,
                const HV_EFI_RUNTIME_SHADOW_REQUEST &request,
                HV_EFI_RUNTIME_SHADOW_RESPONSE *response,
                DWORD *error)
    {
        if (response == nullptr || error == nullptr ||
            descriptor.Generation == 0 || descriptor.NextSequence == 0)
            return false;
        const std::uint32_t mailboxBytes = HVR1_MAILBOX_HEADER_BYTES +
            static_cast<std::uint32_t>(
                sizeof(HV_EFI_RUNTIME_SHADOW_MAILBOX));
        std::vector<std::uint8_t> buffer(
            sizeof(HVR1_IOCTL_REQUEST) + mailboxBytes, 0);
        auto *ioctlRequest =
            reinterpret_cast<HVR1_IOCTL_REQUEST *>(buffer.data());
        auto *mailbox = reinterpret_cast<HVR1_MAILBOX *>(
            buffer.data() + sizeof(HVR1_IOCTL_REQUEST));
        auto *shadow = reinterpret_cast<HV_EFI_RUNTIME_SHADOW_MAILBOX *>(
            mailbox->Payload);
        ioctlRequest->Size = sizeof(*ioctlRequest);
        ioctlRequest->Version = HVR1_PROTOCOL_VERSION;
        ioctlRequest->Operation = HVR1_IOCTL_OPERATION_SHADOW_EPT;
        ioctlRequest->ExpectedGeneration = descriptor.Generation;
        mailbox->Magic = HVR1_PROTOCOL_MAGIC;
        mailbox->Version = HVR1_PROTOCOL_VERSION;
        mailbox->HeaderSize = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->TotalSize = mailboxBytes;
        mailbox->Operation = HVR1_MAILBOX_OPERATION_SHADOW_EPT;
        mailbox->Generation = descriptor.Generation;
        mailbox->Sequence = descriptor.NextSequence;
        mailbox->PayloadOffset = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->PayloadSize = sizeof(*shadow);
        shadow->Magic = HV_EFI_RUNTIME_SHADOW_MAGIC;
        shadow->Version = HV_EFI_RUNTIME_SHADOW_VERSION;
        shadow->HeaderSize = static_cast<std::uint16_t>(
            offsetof(HV_EFI_RUNTIME_SHADOW_MAILBOX, Request));
        shadow->State = HV_EFI_RUNTIME_SHADOW_MAILBOX_REQUEST_READY;
        shadow->Generation = descriptor.Generation;
        shadow->Request = request;
        shadow->Request.Size = sizeof(shadow->Request);
        shadow->Request.Version = HV_EFI_RUNTIME_SHADOW_VERSION;
        shadow->Request.Sequence = descriptor.NextSequence;
        if (!Hvr1ComputeMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce,
                mailbox, mailbox->AuthTag)) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(
            handle_, HVR1_IOCTL_SUBMIT,
            buffer.data(), static_cast<DWORD>(buffer.size()),
            buffer.data(), static_cast<DWORD>(buffer.size()),
            &returned, nullptr);
        if (!ok) {
            *error = GetLastError();
            DeviceStatus diagnostic{};
            if (Query(&diagnostic) && diagnostic.Value.Reserved != 0) {
                const std::uint32_t validation =
                    diagnostic.Value.Reserved & UINT32_C(0xff);
                const std::uint32_t rootStatus =
                    (diagnostic.Value.Reserved >> 8) & UINT32_C(0xff);
                const std::uint32_t detail =
                    (diagnostic.Value.Reserved >> 16) & UINT32_C(0xff);
                std::fwprintf(
                    stderr,
                    L"HVR1 shadow response validation: %ls (%lu), "
                    L"root-status=%lu, detail=%lu\n",
                    ScanDiagnosticName(validation),
                    static_cast<unsigned long>(validation),
                    static_cast<unsigned long>(rootStatus),
                    static_cast<unsigned long>(detail));
            }
            return false;
        }
        mailbox = reinterpret_cast<HVR1_MAILBOX *>(buffer.data());
        shadow = reinterpret_cast<HV_EFI_RUNTIME_SHADOW_MAILBOX *>(
            mailbox->Payload);
        if (returned != mailboxBytes ||
            mailbox->Magic != HVR1_PROTOCOL_MAGIC ||
            mailbox->TotalSize != mailboxBytes ||
            mailbox->Operation != HVR1_MAILBOX_OPERATION_SHADOW_EPT ||
            mailbox->Generation != descriptor.Generation ||
            mailbox->Sequence != descriptor.NextSequence ||
            mailbox->PayloadSize != sizeof(*shadow) ||
            !Hvr1VerifyMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce, mailbox) ||
            shadow->Magic != HV_EFI_RUNTIME_SHADOW_MAGIC ||
            shadow->Version != HV_EFI_RUNTIME_SHADOW_VERSION ||
            shadow->State != HV_EFI_RUNTIME_SHADOW_MAILBOX_RESPONSE_READY ||
            shadow->Generation != descriptor.Generation ||
            shadow->Response.Size != sizeof(shadow->Response) ||
            shadow->Response.Version != HV_EFI_RUNTIME_SHADOW_VERSION ||
            shadow->Response.Generation != descriptor.Generation ||
            shadow->Response.Sequence != descriptor.NextSequence ||
            shadow->Response.Status != mailbox->Status) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        *response = shadow->Response;
        *error = ERROR_SUCCESS;
        return true;
    }

    bool Analysis(const HVR1_MAILBOX_DESCRIPTOR &descriptor,
                  const HV_EFI_ANALYSIS_REQUEST &request,
                  HV_EFI_ANALYSIS_RESPONSE *response,
                  DWORD *error)
    {
        if (response == nullptr || error == nullptr ||
            descriptor.Generation == 0 || descriptor.NextSequence == 0)
            return false;
        const std::uint32_t mailboxBytes = HVR1_MAILBOX_HEADER_BYTES +
            static_cast<std::uint32_t>(sizeof(HV_EFI_ANALYSIS_MAILBOX));
        std::vector<std::uint8_t> buffer(
            sizeof(HVR1_IOCTL_REQUEST) + mailboxBytes, 0);
        auto *ioctlRequest = reinterpret_cast<HVR1_IOCTL_REQUEST *>(
            buffer.data());
        auto *mailbox = reinterpret_cast<HVR1_MAILBOX *>(
            buffer.data() + sizeof(HVR1_IOCTL_REQUEST));
        auto *analysis = reinterpret_cast<HV_EFI_ANALYSIS_MAILBOX *>(
            mailbox->Payload);
        ioctlRequest->Size = sizeof(*ioctlRequest);
        ioctlRequest->Version = HVR1_PROTOCOL_VERSION;
        ioctlRequest->Operation = HVR1_IOCTL_OPERATION_ANALYSIS;
        ioctlRequest->ExpectedGeneration = descriptor.Generation;
        mailbox->Magic = HVR1_PROTOCOL_MAGIC;
        mailbox->Version = HVR1_PROTOCOL_VERSION;
        mailbox->HeaderSize = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->TotalSize = mailboxBytes;
        mailbox->Operation = HVR1_MAILBOX_OPERATION_ANALYSIS;
        mailbox->Generation = descriptor.Generation;
        mailbox->Sequence = descriptor.NextSequence;
        mailbox->PayloadOffset = HVR1_MAILBOX_HEADER_BYTES;
        mailbox->PayloadSize = sizeof(*analysis);
        analysis->Magic = HV_EFI_ANALYSIS_MAGIC;
        analysis->Version = HV_EFI_ANALYSIS_VERSION;
        analysis->HeaderSize = static_cast<std::uint16_t>(
            offsetof(HV_EFI_ANALYSIS_MAILBOX, Request));
        analysis->State = HV_EFI_ANALYSIS_MAILBOX_REQUEST_READY;
        analysis->Generation = descriptor.Generation;
        analysis->Request = request;
        analysis->Request.Size = sizeof(analysis->Request);
        analysis->Request.Version = HV_EFI_ANALYSIS_VERSION;
        analysis->Request.Sequence = descriptor.NextSequence;
        if (!Hvr1ComputeMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce,
                mailbox, mailbox->AuthTag)) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(
            handle_, HVR1_IOCTL_SUBMIT,
            buffer.data(), static_cast<DWORD>(buffer.size()),
            buffer.data(), static_cast<DWORD>(buffer.size()),
            &returned, nullptr);
        if (!ok) {
            *error = GetLastError();
            return false;
        }
        mailbox = reinterpret_cast<HVR1_MAILBOX *>(buffer.data());
        analysis = reinterpret_cast<HV_EFI_ANALYSIS_MAILBOX *>(
            mailbox->Payload);
        if (returned != mailboxBytes ||
            mailbox->Magic != HVR1_PROTOCOL_MAGIC ||
            mailbox->TotalSize != mailboxBytes ||
            mailbox->Operation != HVR1_MAILBOX_OPERATION_ANALYSIS ||
            mailbox->Generation != descriptor.Generation ||
            mailbox->Sequence != descriptor.NextSequence ||
            mailbox->PayloadSize != sizeof(*analysis) ||
            !Hvr1VerifyMailboxAuthTag(
                descriptor.SessionKey, descriptor.SessionNonce, mailbox) ||
            analysis->Magic != HV_EFI_ANALYSIS_MAGIC ||
            analysis->Version != HV_EFI_ANALYSIS_VERSION ||
            analysis->State != HV_EFI_ANALYSIS_MAILBOX_RESPONSE_READY ||
            analysis->Generation != descriptor.Generation ||
            analysis->Response.Size != sizeof(analysis->Response) ||
            analysis->Response.Version != HV_EFI_ANALYSIS_VERSION ||
            analysis->Response.Generation != descriptor.Generation ||
            analysis->Response.Sequence != descriptor.NextSequence ||
            analysis->Response.Status != mailbox->Status) {
            *error = ERROR_INVALID_DATA;
            return false;
        }
        *response = analysis->Response;
        *error = ERROR_SUCCESS;
        return true;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

bool IsOffline(const DeviceStatus &status)
{
    return status.Value.State == HVR1_COMPANION_STATE_OFFLINE &&
           status.Value.LastStatus == ERROR_SUCCESS &&
           status.Value.ExpectedCpuCount != 0 &&
           status.Value.ExpectedCpuCount == status.Value.AcknowledgedCpuCount;
}

void PrintStatus(const DeviceStatus &status)
{
    std::wprintf(L"device status: state=%lu last_status=0x%08lX "
                 L"generation=%llu cpus=%lu/%lu crash=%lu\n",
                 static_cast<unsigned long>(status.Value.State),
                 static_cast<unsigned long>(status.Value.LastStatus),
                 static_cast<unsigned long long>(status.Value.Generation),
                 static_cast<unsigned long>(status.Value.AcknowledgedCpuCount),
                 static_cast<unsigned long>(status.Value.ExpectedCpuCount),
                 static_cast<unsigned long>(status.Value.CrashRecorded));
    if (status.Win32Error != ERROR_SUCCESS)
        std::wprintf(L"device error: %lu\n",
                     static_cast<unsigned long>(status.Win32Error));
}

bool QueryAndPrint(DeviceHandle &device, DeviceStatus *status)
{
    if (!device.Query(status)) {
        ReportError(L"QUERY_STATUS", status->Win32Error);
        PrintStatus(*status);
        return false;
    }
    PrintStatus(*status);
    return true;
}

int ShutdownOnce(bool serviceMode, RecoveryStore *store,
                 RecoveryRecord *record, DeviceHandle *existingDevice = nullptr,
                 bool lifecycleAlreadyStarted = false,
                 bool emitDiagnostics = false)
{
    if (!lifecycleAlreadyStarted) {
        RecoveryRecord previous{};
        bool hadPrevious = false;
        LONG result = store->Begin(&previous, &hadPrevious, record);
        if (result == ERROR_INVALID_STATE) {
            std::wprintf(L"refusing unsafe reuse: previous lifecycle is %ls "
                         L"(generation=%llu sequence=%llu)\n",
                         RecoveryStateName(static_cast<RecoveryState>(
                             previous.State)),
                         static_cast<unsigned long long>(previous.Generation),
                         static_cast<unsigned long long>(previous.Sequence));
            return 2;
        }
        if (result != ERROR_SUCCESS) {
            ReportError(L"recovery begin", result);
            return 2;
        }
    }

    DeviceHandle localDevice;
    DeviceHandle *device = existingDevice != nullptr ? existingDevice
                                                       : &localDevice;
    if (!device->IsOpen() && !device->Open()) {
        DWORD error = GetLastError();
        ReportError(L"CreateFile(device)", error);
        record->State = static_cast<std::uint32_t>(RecoveryState::Quarantined);
        record->LastError = static_cast<std::int32_t>(error);
        (void)store->Write(*record);
        return 3;
    }

    HVR1_MAILBOX_DESCRIPTOR descriptor{};
    DWORD error = ERROR_SUCCESS;
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_GET_MAILBOX_BEGIN");
    if (!device->GetMailbox(&descriptor, &error) ||
        descriptor.Version != HVR1_PROTOCOL_VERSION ||
        descriptor.MailboxBytes != HVR1_MAILBOX_BYTES ||
        descriptor.MailboxHeaderBytes != HVR1_MAILBOX_HEADER_BYTES ||
        descriptor.Generation == 0 || descriptor.NextSequence == 0) {
        if (error == ERROR_SUCCESS)
            error = ERROR_INVALID_DATA;
        ReportError(L"GET_MAILBOX", error);
        record->State = static_cast<std::uint32_t>(RecoveryState::Quarantined);
        record->LastError = static_cast<std::int32_t>(error);
        (void)store->Write(*record);
        return 3;
    }
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_GET_MAILBOX_COMPLETE");
    record->Sequence = descriptor.NextSequence;

    LONG recordResult = ERROR_SUCCESS;
    DeviceStatus priorStatus{};
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_PRIOR_QUERY_BEGIN");
    if (device->Query(&priorStatus)) {
        if (IsOffline(priorStatus)) {
            record->State = static_cast<std::uint32_t>(RecoveryState::Offline);
            record->Generation = priorStatus.Value.Generation;
            record->ExpectedCpuCount = priorStatus.Value.ExpectedCpuCount;
            record->AcknowledgedCpuCount =
                priorStatus.Value.AcknowledgedCpuCount;
            record->LastError = 0;
            recordResult = store->Write(*record);
            if (recordResult != ERROR_SUCCESS) {
                ReportError(L"recovery existing offline",
                            static_cast<DWORD>(recordResult));
                return 5;
            }
            return 0;
        }
        if (priorStatus.Value.State == HVR1_COMPANION_STATE_QUARANTINED ||
            priorStatus.Value.State == HVR1_COMPANION_STATE_SHUTDOWN_PREPARING ||
            priorStatus.Value.State == HVR1_COMPANION_STATE_SHUTDOWN_COMMITTING) {
            record->State = static_cast<std::uint32_t>(
                RecoveryState::Quarantined);
            record->LastError = ERROR_INVALID_STATE;
            (void)store->Write(*record);
            Report(L"HVR1 device is not in a reusable state");
            return 4;
        }
    }
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_PRIOR_QUERY_COMPLETE");

    record->Generation = descriptor.Generation;
    record->State = static_cast<std::uint32_t>(RecoveryState::Detaching);
    recordResult = store->Write(*record);
    if (recordResult != ERROR_SUCCESS) {
        ReportError(L"recovery detaching", static_cast<DWORD>(recordResult));
        return 3;
    }
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_SUBMIT_BEGIN");

    DeviceStatus submitStatus{};
    bool submitted = device->Submit(descriptor, record->Sequence,
                                    &submitStatus);
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_SUBMIT_RETURNED");
    SecureZeroMemory(&descriptor, sizeof(descriptor));
    DeviceStatus finalStatus{};
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_FINAL_QUERY_BEGIN");
    bool queried = device->Query(&finalStatus);
    if (emitDiagnostics)
        SelfTestDiagnostic("SHUTDOWN_FINAL_QUERY_COMPLETE");
    if (!queried)
        finalStatus.Win32Error = GetLastError();
    if (!submitted || !queried || !IsOffline(finalStatus)) {
        if (!submitted)
            PrintStatus(submitStatus);
        if (!queried)
            ReportError(L"QUERY_STATUS after shutdown", finalStatus.Win32Error);
        else
            PrintStatus(finalStatus);
        record->State = static_cast<std::uint32_t>(RecoveryState::Quarantined);
        record->ExpectedCpuCount = finalStatus.Value.ExpectedCpuCount;
        record->AcknowledgedCpuCount = finalStatus.Value.AcknowledgedCpuCount;
        record->LastError = static_cast<std::int32_t>(
            finalStatus.Win32Error != ERROR_SUCCESS
                ? finalStatus.Win32Error
                : ERROR_BUSY);
        (void)store->Write(*record);
        Report(serviceMode
                   ? L"HVR1 service shutdown incomplete; staying fail-closed"
                   : L"HVR1 shutdown incomplete; lifecycle quarantined");
        return 4;
    }

    PrintStatus(finalStatus);
    record->State = static_cast<std::uint32_t>(RecoveryState::Offline);
    record->ExpectedCpuCount = finalStatus.Value.ExpectedCpuCount;
    record->AcknowledgedCpuCount = finalStatus.Value.AcknowledgedCpuCount;
    record->LastError = 0;
    recordResult = store->Write(*record);
    if (recordResult != ERROR_SUCCESS) {
        /* Detach succeeded, but losing the recovery record is unsafe for a
         * future restart, so report failure and leave the record DETACHING. */
        Report(L"HVR1 detached, but OFFLINE recovery record could not be saved");
        ReportError(L"recovery offline", static_cast<DWORD>(recordResult));
        return 5;
    }
    return 0;
}

struct ServiceContext {
    std::mutex lock;
    HANDLE stopEvent = nullptr;
    DeviceHandle device;
    SERVICE_STATUS_HANDLE statusHandle = nullptr;
    SERVICE_STATUS status{};
    RecoveryStore recovery;
    RecoveryRecord record{};
    HANDLE powerRequest = nullptr;
    bool detached = false;
    bool quarantined = false;

    ~ServiceContext()
    {
        ReleasePowerRequest();
    }

    bool AcquirePowerRequest()
    {
        if (powerRequest != nullptr)
            return true;
        REASON_CONTEXT reason{};
        reason.Version = POWER_REQUEST_CONTEXT_VERSION;
        reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        reason.Reason.SimpleReasonString =
            const_cast<PWSTR>(kPowerRequestReason);
        powerRequest = PowerCreateRequest(&reason);
        if (powerRequest == INVALID_HANDLE_VALUE) {
            powerRequest = nullptr;
            return false;
        }
        if (!PowerSetRequest(powerRequest, PowerRequestSystemRequired) ||
            !PowerSetRequest(powerRequest, PowerRequestExecutionRequired)) {
            (void)PowerClearRequest(powerRequest, PowerRequestSystemRequired);
            (void)PowerClearRequest(powerRequest,
                                    PowerRequestExecutionRequired);
            CloseHandle(powerRequest);
            powerRequest = nullptr;
            return false;
        }
        return true;
    }

    void ReleasePowerRequest()
    {
        if (powerRequest == nullptr)
            return;
        (void)PowerClearRequest(powerRequest, PowerRequestSystemRequired);
        (void)PowerClearRequest(powerRequest,
                                PowerRequestExecutionRequired);
        CloseHandle(powerRequest);
        powerRequest = nullptr;
    }
};

ServiceContext *g_serviceContext = nullptr;

void SetServiceStatus(ServiceContext *context, DWORD state,
                      DWORD win32ExitCode = ERROR_SUCCESS,
                      DWORD waitHint = 0)
{
    std::lock_guard<std::mutex> guard(context->lock);
    context->status.dwCurrentState = state;
    context->status.dwWin32ExitCode = win32ExitCode;
    context->status.dwWaitHint = waitHint;
    (void)::SetServiceStatus(context->statusHandle, &context->status);
}

DWORD ServiceDetach(ServiceContext *context, bool stopAfter)
{
    {
        std::lock_guard<std::mutex> guard(context->lock);
        if (context->detached) {
            if (stopAfter)
                SetEvent(context->stopEvent);
            return ERROR_SUCCESS;
        }
        if (context->quarantined)
            return ERROR_BUSY;

        int result = ShutdownOnce(true, &context->recovery, &context->record,
                                  &context->device, true);
        if (result != 0) {
            context->quarantined = true;
            return ERROR_BUSY;
        }
        context->detached = true;
        context->ReleasePowerRequest();
        context->status.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
    }
    SetServiceStatus(context, SERVICE_RUNNING);
    if (stopAfter)
        SetEvent(context->stopEvent);
    return ERROR_SUCCESS;
}

DWORD WINAPI Hvr1ServiceControl(
    DWORD control, DWORD eventType, LPVOID eventData, LPVOID contextValue)
{
    UNREFERENCED_PARAMETER(eventData);
    auto *context = static_cast<ServiceContext *>(contextValue);
    if (context == nullptr)
        return ERROR_INVALID_HANDLE;

    if (control == SERVICE_CONTROL_INTERROGATE)
        return ERROR_SUCCESS;

    if (control == SERVICE_CONTROL_STOP) {
        std::lock_guard<std::mutex> guard(context->lock);
        if (!context->detached)
            return ERROR_BUSY;
        SetEvent(context->stopEvent);
        return ERROR_SUCCESS;
    }

    bool suspend = control == SERVICE_CONTROL_POWEREVENT &&
        (eventType == PBT_APMSUSPEND || eventType == PBT_APMSTANDBY);
    bool shutdown = control == SERVICE_CONTROL_PRESHUTDOWN ||
        control == SERVICE_CONTROL_SHUTDOWN;
    if (!suspend && !shutdown)
        return ERROR_SUCCESS;

    /* This callback is intentionally synchronous.  Windows does not offer a
     * vetoable query-suspend callback to a normal service. */
    return ServiceDetach(context, shutdown) == ERROR_SUCCESS
        ? ERROR_SUCCESS
        : ERROR_BUSY;
}

void WINAPI Hvr1ServiceMain(DWORD argc, LPWSTR *argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    ServiceContext context;
    g_serviceContext = &context;
    context.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (context.stopEvent == nullptr)
        return;

    context.statusHandle = RegisterServiceCtrlHandlerExW(
        kServiceName, Hvr1ServiceControl, &context);
    if (context.statusHandle == nullptr) {
        CloseHandle(context.stopEvent);
        return;
    }

    std::memset(&context.status, 0, sizeof(context.status));
    context.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    context.status.dwControlsAccepted = SERVICE_ACCEPT_POWEREVENT |
        SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_SHUTDOWN;
    context.status.dwWin32ExitCode = ERROR_SUCCESS;
    SetServiceStatus(&context, SERVICE_START_PENDING, ERROR_SUCCESS, 30000);

    RecoveryRecord previous{};
    bool hadPrevious = false;
    LONG recoveryResult = context.recovery.Begin(
        &previous, &hadPrevious, &context.record);
    if (recoveryResult == ERROR_INVALID_STATE) {
        wchar_t message[256]{};
        (void)swprintf_s(
            message, _countof(message),
            L"HVR1 service refusing restart after previous %ls lifecycle "
            L"(generation=%llu sequence=%llu)",
            RecoveryStateName(static_cast<RecoveryState>(previous.State)),
            static_cast<unsigned long long>(previous.Generation),
            static_cast<unsigned long long>(previous.Sequence));
        Report(message);
        std::fwprintf(stderr, L"%ls\n", message);
        SetServiceStatus(&context, SERVICE_STOPPED, ERROR_INVALID_STATE);
        CloseHandle(context.stopEvent);
        g_serviceContext = nullptr;
        return;
    }
    if (recoveryResult != ERROR_SUCCESS) {
        ReportError(L"service recovery begin",
                    static_cast<DWORD>(recoveryResult));
        SetServiceStatus(&context, SERVICE_STOPPED,
                         static_cast<DWORD>(recoveryResult));
        CloseHandle(context.stopEvent);
        g_serviceContext = nullptr;
        return;
    }
    if (hadPrevious)
        Report(IsUnsafeRecoveryState(static_cast<RecoveryState>(previous.State))
                   ? L"HVR1 service recovered a stale lifecycle from a prior boot"
                   : L"HVR1 service found a previous clean/offline lifecycle");

    if (!context.AcquirePowerRequest()) {
        DWORD error = GetLastError();
        if (error == ERROR_SUCCESS)
            error = ERROR_GEN_FAILURE;
        ReportError(L"PowerCreateRequest/PowerSetRequest", error);
        context.record.State = static_cast<std::uint32_t>(
            RecoveryState::Quarantined);
        context.record.LastError = static_cast<std::int32_t>(error);
        (void)context.recovery.Write(context.record);
        SetServiceStatus(&context, SERVICE_STOPPED, error);
        CloseHandle(context.stopEvent);
        g_serviceContext = nullptr;
        return;
    }

    if (!context.device.Open()) {
        DWORD error = GetLastError();
        ReportError(L"service CreateFile(device)", error);
        context.record.State = static_cast<std::uint32_t>(
            RecoveryState::Quarantined);
        context.record.LastError = static_cast<std::int32_t>(error);
        (void)context.recovery.Write(context.record);
        SetServiceStatus(&context, SERVICE_STOPPED, error);
        CloseHandle(context.stopEvent);
        g_serviceContext = nullptr;
        return;
    }
    /* The context-owned handle remains open and exclusive for the service
     * lifetime. */
    DeviceStatus initialStatus{};
    if (!context.device.Query(&initialStatus)) {
        DWORD error = initialStatus.Win32Error;
        ReportError(L"service QUERY_STATUS", error);
        context.record.State = static_cast<std::uint32_t>(
            RecoveryState::Quarantined);
        context.record.LastError = static_cast<std::int32_t>(error);
        (void)context.recovery.Write(context.record);
        SetServiceStatus(&context, SERVICE_STOPPED, error);
        context.device.Close();
        CloseHandle(context.stopEvent);
        g_serviceContext = nullptr;
        return;
    }
    if (initialStatus.Value.State == HVR1_COMPANION_STATE_QUARANTINED ||
        (initialStatus.Value.State != HVR1_COMPANION_STATE_INITIALIZED &&
         initialStatus.Value.State != HVR1_COMPANION_STATE_SESSION_READY &&
         initialStatus.Value.State != HVR1_COMPANION_STATE_OFFLINE)) {
        Report(L"HVR1 service refusing non-reusable device state");
        context.record.State = static_cast<std::uint32_t>(
            RecoveryState::Quarantined);
        context.record.LastError = ERROR_INVALID_STATE;
        (void)context.recovery.Write(context.record);
        SetServiceStatus(&context, SERVICE_STOPPED, ERROR_INVALID_STATE);
        context.device.Close();
        CloseHandle(context.stopEvent);
        g_serviceContext = nullptr;
        return;
    }
    if (IsOffline(initialStatus)) {
        context.record.State = static_cast<std::uint32_t>(RecoveryState::Offline);
        context.record.Generation = initialStatus.Value.Generation;
        context.record.ExpectedCpuCount = initialStatus.Value.ExpectedCpuCount;
        context.record.AcknowledgedCpuCount = initialStatus.Value.AcknowledgedCpuCount;
        context.record.LastError = 0;
        if (context.recovery.Write(context.record) != ERROR_SUCCESS) {
            Report(L"HVR1 service could not persist existing OFFLINE state");
            SetServiceStatus(&context, SERVICE_STOPPED, ERROR_WRITE_FAULT);
            context.device.Close();
            CloseHandle(context.stopEvent);
            g_serviceContext = nullptr;
            return;
        }
        context.detached = true;
        context.ReleasePowerRequest();
        context.status.dwControlsAccepted |= SERVICE_ACCEPT_STOP;
    }
    SetServiceStatus(&context, SERVICE_RUNNING);
    DWORD waitResult = WaitForSingleObject(context.stopEvent, INFINITE);
    UNREFERENCED_PARAMETER(waitResult);
    SetServiceStatus(&context, SERVICE_STOP_PENDING, ERROR_SUCCESS, 30000);
    context.device.Close();
    CloseHandle(context.stopEvent);
    g_serviceContext = nullptr;
    SetServiceStatus(&context, SERVICE_STOPPED);
}

int RunStatus()
{
    RecoveryStore store;
    RecoveryRecord record{};
    bool present = false;
    LONG result = store.Open(false);
    if (result == ERROR_SUCCESS)
        result = store.Read(&record, &present);
    if (result == ERROR_SUCCESS && present) {
        std::wprintf(L"recovery: state=%ls generation=%llu sequence=%llu "
                     L"cpus=%lu/%lu error=%ld\n",
                     RecoveryStateName(static_cast<RecoveryState>(
                         record.State)),
                     static_cast<unsigned long long>(record.Generation),
                     static_cast<unsigned long long>(record.Sequence),
                     static_cast<unsigned long>(record.AcknowledgedCpuCount),
                     static_cast<unsigned long>(record.ExpectedCpuCount),
                     static_cast<long>(record.LastError));
    } else if (result == ERROR_FILE_NOT_FOUND ||
               (result == ERROR_SUCCESS && !present)) {
        std::wprintf(L"recovery: no lifecycle record\n");
    } else if (result != ERROR_SUCCESS) {
        ReportError(L"recovery status", static_cast<DWORD>(result));
        return 2;
    }
    if (present && IsUnsafeRecoveryState(static_cast<RecoveryState>(
            record.State))) {
        std::wprintf(L"recovery: refusing unsafe reuse\n");
    }

    DeviceHandle device;
    if (!device.Open()) {
        ReportError(L"CreateFile(device)", GetLastError());
        return present && IsUnsafeRecoveryState(static_cast<RecoveryState>(
            record.State)) ? 2 : 3;
    }
    DeviceStatus status{};
    bool ok = QueryAndPrint(device, &status);
    if (present && IsUnsafeRecoveryState(static_cast<RecoveryState>(
            record.State)))
        return 2;
    return ok ? 0 : 3;
}

int HexNibble(wchar_t value)
{
    if (value >= L'0' && value <= L'9')
        return value - L'0';
    value = static_cast<wchar_t>(std::towupper(value));
    if (value >= L'A' && value <= L'F')
        return value - L'A' + 10;
    return -1;
}

bool ParsePattern(const wchar_t *text,
                  HV_EFI_RUNTIME_SCAN_REQUEST *request)
{
    if (text == nullptr || request == nullptr)
        return false;
    std::uint32_t length = 0;
    const wchar_t *cursor = text;
    while (*cursor != L'\0') {
        while (std::iswspace(*cursor))
            ++cursor;
        if (*cursor == L'\0')
            break;
        const wchar_t *token = cursor;
        while (*cursor != L'\0' && !std::iswspace(*cursor))
            ++cursor;
        std::size_t tokenLength = static_cast<std::size_t>(cursor - token);
        if (length >= HV_EFI_RUNTIME_SCAN_MAX_PATTERN_BYTES)
            return false;
        if ((tokenLength == 1 && token[0] == L'?') ||
            (tokenLength == 2 && token[0] == L'?' && token[1] == L'?')) {
            request->WildcardMask |= UINT64_C(1) << length;
            request->Pattern[length++] = 0;
            continue;
        }
        if (tokenLength != 2)
            return false;
        int high = HexNibble(token[0]);
        int low = HexNibble(token[1]);
        if (high < 0 || low < 0)
            return false;
        request->Pattern[length++] = static_cast<std::uint8_t>(
            (high << 4) | low);
    }
    request->PatternLength = length;
    return length != 0;
}

bool ParseAddress(const wchar_t *text, std::uint64_t *value)
{
    if (text == nullptr || value == nullptr || *text == L'\0' || *text == L'-')
        return false;
    wchar_t *end = nullptr;
    errno = 0;
    unsigned long long parsed = std::wcstoull(text, &end, 0);
    if (errno == ERANGE || end == text || *end != L'\0')
        return false;
    *value = static_cast<std::uint64_t>(parsed);
    return true;
}

const wchar_t *ScanStatusName(std::uint32_t status)
{
    switch (status) {
    case HV_EFI_RUNTIME_SCAN_SUCCESS: return L"success";
    case HV_EFI_RUNTIME_SCAN_NOT_FOUND: return L"not-found";
    case HV_EFI_RUNTIME_SCAN_MORE_DATA: return L"more-data";
    case HV_EFI_RUNTIME_SCAN_INVALID_REQUEST: return L"invalid-request";
    case HV_EFI_RUNTIME_SCAN_BUSY: return L"busy";
    case HV_EFI_RUNTIME_SCAN_REPLAY_REJECTED: return L"replay-rejected";
    case HV_EFI_RUNTIME_SCAN_GENERATION_MISMATCH: return L"generation-mismatch";
    case HV_EFI_RUNTIME_SCAN_RANGE_DENIED: return L"range-denied";
    case HV_EFI_RUNTIME_SCAN_RESULT_CAPACITY: return L"result-capacity";
    case HV_EFI_RUNTIME_SCAN_MULTIPLE_MATCHES: return L"multiple-matches";
    case HV_EFI_RUNTIME_SCAN_MAPPING_FAILED: return L"mapping-failed";
    case HV_EFI_RUNTIME_SCAN_SCANNER_FAILED: return L"scanner-failed";
    case HV_EFI_RUNTIME_SCAN_INVARIANT_VIOLATION: return L"invariant-violation";
    default: return L"unknown";
    }
}

const wchar_t *ScanDiagnosticName(std::uint32_t diagnostic)
{
    switch (diagnostic) {
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_NONE: return L"none";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATUS: return L"status";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_MAGIC: return L"magic";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_VERSION: return L"version";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_HEADER_SIZE: return L"header-size";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_STATE: return L"state";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_GENERATION: return L"generation";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_REQUEST_SEQUENCE:
        return L"request-sequence";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SIZE:
        return L"response-size";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_VERSION:
        return L"response-version";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_STATUS:
        return L"response-status";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_GENERATION:
        return L"response-generation";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESPONSE_SEQUENCE:
        return L"response-sequence";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_RESULT_COUNT:
        return L"result-count";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_SHADOW_RENDEZVOUS:
        return L"shadow-rendezvous";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_SHADOW_CPU_STATUS:
        return L"shadow-cpu-status";
    case HVR1_SCAN_RESPONSE_DIAGNOSTIC_SHADOW_RANGE_COUNT:
        return L"shadow-range-count";
    default: return L"unknown";
    }
}

int RunScan(int argc, wchar_t **argv)
{
    if (argc < 5 || argc > 6)
        return 1;
    HV_EFI_RUNTIME_SCAN_REQUEST request{};
    request.Size = sizeof(request);
    request.Version = HV_EFI_RUNTIME_SCAN_VERSION;
    request.CallbackType = HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH;
    request.MaxResults = 1;
    if (!ParsePattern(argv[2], &request) ||
        !ParseAddress(argv[3], &request.StartAddress) ||
        !ParseAddress(argv[4], &request.EndAddress) ||
        (request.EndAddress != 0 &&
         request.StartAddress >= request.EndAddress)) {
        std::fwprintf(stderr, L"invalid scan pattern or physical range\n");
        return 1;
    }
    if (argc == 6) {
        if (_wcsicmp(argv[5], L"all") == 0) {
            request.CallbackType = HV_EFI_RUNTIME_SCAN_CALLBACK_ALL;
            request.MaxResults = HV_EFI_RUNTIME_SCAN_MAX_RESULTS;
        } else if (_wcsicmp(argv[5], L"single") == 0) {
            request.CallbackType = HV_EFI_RUNTIME_SCAN_CALLBACK_SINGLE;
            request.MaxResults = 2;
        } else if (_wcsicmp(argv[5], L"first") != 0) {
            std::fwprintf(stderr, L"scan mode must be first, all, or single\n");
            return 1;
        }
    }

    DeviceHandle device;
    if (!device.Open()) {
        ReportError(L"scan CreateFile(device)", GetLastError());
        return 3;
    }
    std::uint64_t totalMatches = 0;
    for (;;) {
        HVR1_MAILBOX_DESCRIPTOR descriptor{};
        DWORD error = ERROR_SUCCESS;
        if (!device.GetMailbox(&descriptor, &error) ||
            descriptor.Version != HVR1_PROTOCOL_VERSION ||
            descriptor.Generation == 0 || descriptor.NextSequence == 0) {
            if (error == ERROR_SUCCESS)
                error = ERROR_INVALID_DATA;
            ReportError(L"scan GET_MAILBOX", error);
            SecureZeroMemory(&descriptor, sizeof(descriptor));
            return 3;
        }
        HV_EFI_RUNTIME_SCAN_RESPONSE response{};
        if (!device.Scan(descriptor, request, &response, &error)) {
            SecureZeroMemory(&descriptor, sizeof(descriptor));
            ReportError(L"authenticated runtime scan", error);
            DeviceStatus diagnostic{};
            if (device.Query(&diagnostic) && diagnostic.Value.Reserved != 0) {
                const std::uint32_t validation =
                    diagnostic.Value.Reserved & UINT32_C(0xff);
                const std::uint32_t rootStatus =
                    (diagnostic.Value.Reserved >> 8) & UINT32_C(0xff);
                const std::uint32_t mailboxState =
                    (diagnostic.Value.Reserved >> 16) & UINT32_C(0xff);
                std::fwprintf(
                    stderr,
                    L"HVR1 scan response validation: %ls (%lu), "
                    L"root-status=%lu, mailbox-state=%lu\n",
                    ScanDiagnosticName(validation),
                    static_cast<unsigned long>(validation),
                    static_cast<unsigned long>(rootStatus),
                    static_cast<unsigned long>(mailboxState));
            }
            return 4;
        }
        SecureZeroMemory(&descriptor, sizeof(descriptor));
        std::wprintf(L"scan: status=%ls scanned=0x%llX next=0x%llX results=%lu\n",
                     ScanStatusName(response.Status),
                     static_cast<unsigned long long>(response.ScannedBytes),
                     static_cast<unsigned long long>(response.NextAddress),
                     static_cast<unsigned long>(response.ResultCount));
        for (std::uint32_t index = 0; index < response.ResultCount; ++index) {
            std::wprintf(L"0x%016llX\n",
                         static_cast<unsigned long long>(
                             response.Results[index].PhysicalAddress));
        }
        if (totalMatches >
            (std::numeric_limits<std::uint64_t>::max)() -
                response.ResultCount) {
            std::fwprintf(stderr, L"scanner match count overflow\n");
            return 4;
        }
        totalMatches += response.ResultCount;
        if (request.CallbackType ==
                HV_EFI_RUNTIME_SCAN_CALLBACK_SINGLE &&
            totalMatches > 1) {
            return 2;
        }
        if (response.Status != HV_EFI_RUNTIME_SCAN_MORE_DATA &&
            response.Status != HV_EFI_RUNTIME_SCAN_RESULT_CAPACITY) {
            if (response.Status == HV_EFI_RUNTIME_SCAN_SUCCESS)
                return totalMatches != 0 ? 0 : 2;
            if (response.Status == HV_EFI_RUNTIME_SCAN_NOT_FOUND &&
                request.CallbackType !=
                    HV_EFI_RUNTIME_SCAN_CALLBACK_FIRST_MATCH &&
                totalMatches != 0) {
                return 0;
            }
            return 2;
        }
        if (response.NextAddress <= request.StartAddress ||
            (request.EndAddress != 0 &&
             response.NextAddress >= request.EndAddress)) {
            std::fwprintf(stderr, L"invalid scanner continuation address\n");
            return 4;
        }
        request.StartAddress = response.NextAddress;
    }
}

int RunShutdown()
{
    RecoveryStore store;
    RecoveryRecord record{};
    return ShutdownOnce(false, &store, &record);
}

int RunAuthenticatedSelfTestShutdown()
{
    SelfTestDiagnostic("SELFTEST_ENTERED");
    RecoveryStore store;
    RecoveryRecord previous{};
    RecoveryRecord record{};
    bool hadPrevious = false;
    LONG recoveryResult = store.Begin(&previous, &hadPrevious, &record);
    if (recoveryResult != ERROR_SUCCESS) {
        ReportError(L"self-test recovery begin",
                    static_cast<DWORD>(recoveryResult));
        return 2;
    }
    SelfTestDiagnostic("RECOVERY_BEGIN_COMPLETE");
    if (!hadPrevious ||
        previous.State != static_cast<std::uint32_t>(RecoveryState::Starting)) {
        std::wprintf(L"HVR1_CRASH_RECOVERY_PRIOR_BOOT=FAIL\n");
        return 2;
    }
    std::wprintf(L"HVR1_CRASH_RECOVERY_PRIOR_BOOT=PASS\n");

    ServiceContext powerGuard;
    if (!powerGuard.AcquirePowerRequest()) {
        ReportError(L"self-test power request", GetLastError());
        return 2;
    }
    SelfTestDiagnostic("POWER_REQUEST_HELD");
    std::wprintf(L"HVR1_POWER_REQUEST_HELD=PASS\n");

    DeviceHandle device;
    if (!device.Open()) {
        ReportError(L"self-test CreateFile(device)", GetLastError());
        return 3;
    }
    SelfTestDiagnostic("DEVICE_OPEN_COMPLETE");

    HVR1_MAILBOX_DESCRIPTOR descriptor{};
    DWORD descriptorError = ERROR_SUCCESS;
    SelfTestDiagnostic("INITIAL_GET_MAILBOX_BEGIN");
    if (!device.GetMailbox(&descriptor, &descriptorError) ||
        descriptor.Version != HVR1_PROTOCOL_VERSION ||
        descriptor.MailboxBytes != HVR1_MAILBOX_BYTES ||
        descriptor.MailboxHeaderBytes != HVR1_MAILBOX_HEADER_BYTES ||
        descriptor.Generation == 0 || descriptor.NextSequence == 0) {
        if (descriptorError == ERROR_SUCCESS)
            descriptorError = ERROR_INVALID_DATA;
        ReportError(L"self-test GET_MAILBOX", descriptorError);
        SecureZeroMemory(&descriptor, sizeof(descriptor));
        return 3;
    }
    SelfTestDiagnostic("INITIAL_GET_MAILBOX_COMPLETE");
    record.Sequence = descriptor.NextSequence;

    DeviceStatus rejectedStatus{};
    SelfTestDiagnostic("INVALID_SUBMIT_BEGIN");
    bool invalidAccepted = device.SubmitForSelfTest(
        descriptor, record.Sequence, true, &rejectedStatus);
    SelfTestDiagnostic("INVALID_SUBMIT_RETURNED");
    SecureZeroMemory(&descriptor, sizeof(descriptor));
    DeviceStatus afterReject{};
    SelfTestDiagnostic("AFTER_REJECT_QUERY_BEGIN");
    if (invalidAccepted || rejectedStatus.Win32Error != ERROR_ACCESS_DENIED ||
        !device.Query(&afterReject) ||
        (afterReject.Value.State != HVR1_COMPANION_STATE_INITIALIZED &&
         afterReject.Value.State != HVR1_COMPANION_STATE_SESSION_READY) ||
        afterReject.Value.ExpectedCpuCount != 0 ||
        afterReject.Value.AcknowledgedCpuCount != 0) {
        std::wprintf(L"HVR1_AUTH_INVALID_TAG_REJECTED=FAIL error=%lu\n",
                     static_cast<unsigned long>(rejectedStatus.Win32Error));
        return 4;
    }
    SelfTestDiagnostic("AFTER_REJECT_QUERY_COMPLETE");
    std::wprintf(L"HVR1_AUTH_INVALID_TAG_REJECTED=PASS\n");

    SelfTestDiagnostic("VALID_SHUTDOWN_BEGIN");
    int shutdownResult = ShutdownOnce(false, &store, &record, &device, true,
                                      true);
    if (shutdownResult != 0) {
        std::wprintf(L"HVR1_AUTH_VALID_SHUTDOWN=FAIL result=%d\n",
                     shutdownResult);
        return shutdownResult;
    }
    std::wprintf(L"HVR1_AUTH_VALID_SHUTDOWN=PASS\n");

    HVR1_MAILBOX_DESCRIPTOR replayDescriptor{};
    descriptorError = ERROR_SUCCESS;
    if (!device.GetMailbox(&replayDescriptor, &descriptorError)) {
        ReportError(L"self-test replay GET_MAILBOX", descriptorError);
        SecureZeroMemory(&replayDescriptor, sizeof(replayDescriptor));
        return 5;
    }
    DeviceStatus replayStatus{};
    bool replayAccepted = device.SubmitForSelfTest(
        replayDescriptor, record.Sequence, false, &replayStatus);
    SecureZeroMemory(&replayDescriptor, sizeof(replayDescriptor));
    DeviceStatus finalStatus{};
    if (replayAccepted || replayStatus.Win32Error != ERROR_INVALID_PARAMETER ||
        !device.Query(&finalStatus) || !IsOffline(finalStatus)) {
        std::wprintf(L"HVR1_AUTH_REPLAY_REJECTED=FAIL error=%lu\n",
                     static_cast<unsigned long>(replayStatus.Win32Error));
        return 5;
    }
    std::wprintf(L"HVR1_AUTH_REPLAY_REJECTED=PASS\n");
    powerGuard.ReleasePowerRequest();
    std::wprintf(L"HVR1_POWER_REQUEST_RELEASED_AFTER_OFFLINE=PASS\n");
    std::wprintf(L"HVR1_AUTH_SELFTEST=PASS cpus=%lu/%lu generation=%llu\n",
                 static_cast<unsigned long>(
                     finalStatus.Value.AcknowledgedCpuCount),
                 static_cast<unsigned long>(finalStatus.Value.ExpectedCpuCount),
                 static_cast<unsigned long long>(finalStatus.Value.Generation));
    return 0;
}

int RunArmRecoverySelfTest()
{
    RecoveryStore store;
    RecoveryRecord previous{};
    RecoveryRecord current{};
    bool hadPrevious = false;
    LONG result = store.Begin(&previous, &hadPrevious, &current);
    if (result != ERROR_SUCCESS) {
        ReportError(L"arm recovery self-test", static_cast<DWORD>(result));
        return 2;
    }
    if (current.State !=
            static_cast<std::uint32_t>(RecoveryState::Starting) ||
        current.Sequence == 0) {
        return 3;
    }
    std::wprintf(L"HVR1_CRASH_RECOVERY_ARMED=PASS sequence=%llu\n",
                 static_cast<unsigned long long>(current.Sequence));
    return 0;
}

int RunLocateProcess(int argc, wchar_t **argv)
{
    if (argc != 3) {
        std::fwprintf(stderr, L"usage: hvr1_runtime_service.exe process IMAGE\n");
        return 1;
    }
    const std::size_t length = std::wcslen(argv[2]);
    if (length == 0 || length > 15) {
        std::fwprintf(stderr, L"process image name must contain 1-15 ASCII bytes\n");
        return 1;
    }
    char imageName[HV_EFI_RUNTIME_PROCESS_NAME_BYTES]{};
    for (std::size_t index = 0; index < length; ++index) {
        if (argv[2][index] < 0x20 || argv[2][index] > 0x7e) {
            std::fwprintf(stderr, L"process image name must be printable ASCII\n");
            return 1;
        }
        imageName[index] = static_cast<char>(argv[2][index]);
    }
    DeviceHandle device;
    if (!device.Open()) {
        ReportError(L"process CreateFile(device)", GetLastError());
        return 2;
    }
    HVR1_MAILBOX_DESCRIPTOR descriptor{};
    DWORD error = ERROR_SUCCESS;
    if (!device.GetMailbox(&descriptor, &error)) {
        ReportError(L"process GET_MAILBOX", error);
        return 2;
    }
    HV_EFI_RUNTIME_PROCESS_RESPONSE response{};
    const bool located = device.LocateProcess(
        descriptor, imageName, length, &response, &error);
    SecureZeroMemory(&descriptor, sizeof(descriptor));
    SecureZeroMemory(imageName, sizeof(imageName));
    if (!located) {
        ReportError(L"authenticated process locate", error);
        return 2;
    }
    if (response.Status == HV_EFI_RUNTIME_PROCESS_NOT_FOUND) {
        std::wprintf(L"process: not-found enumerated=%lu stored=%lu\n",
                     static_cast<unsigned long>(response.EnumeratedCount),
                     static_cast<unsigned long>(response.StoredCount));
        return 3;
    }
    if (response.Status != HV_EFI_RUNTIME_PROCESS_SUCCESS) {
        std::wprintf(L"process: root-status=%lu enumerated=%lu stored=%lu\n",
                     static_cast<unsigned long>(response.Status),
                     static_cast<unsigned long>(response.EnumeratedCount),
                     static_cast<unsigned long>(response.StoredCount));
        return 4;
    }
    std::wprintf(
        L"process: pid=%llu cr3=0x%llX eprocess=0x%llX "
        L"enumerated=%lu stored=%lu flags=0x%08lX\n",
        static_cast<unsigned long long>(response.ProcessId),
        static_cast<unsigned long long>(response.DirectoryTableBase),
        static_cast<unsigned long long>(response.EprocessVirtualAddress),
        static_cast<unsigned long>(response.EnumeratedCount),
        static_cast<unsigned long>(response.StoredCount),
        static_cast<unsigned long>(response.Flags));
    return 0;
}

int RunShadow(int argc, wchar_t **argv,
              HV_EFI_RUNTIME_SHADOW_OPERATION operation)
{
    HV_EFI_RUNTIME_SHADOW_REQUEST request{};
    request.Size = sizeof(request);
    request.Version = HV_EFI_RUNTIME_SHADOW_VERSION;
    request.Operation = static_cast<std::uint16_t>(operation);
    if (operation == HV_EFI_RUNTIME_SHADOW_BIND) {
        if (argc < 3 || ((argc - 3) % 2) != 0 ||
            (argc - 3) / 2 >
                static_cast<int>(HV_EFI_SHADOW_EPT_MAX_RANGES) ||
            !ParseAddress(argv[2], &request.ProcessId) ||
            request.ProcessId == 0) {
            std::fwprintf(stderr,
                L"usage: hvr1_runtime_service.exe shadow-bind PID "
                L"[GPA_START SIZE]...\n");
            return 1;
        }
        request.RangeCount = static_cast<std::uint32_t>((argc - 3) / 2);
        for (std::uint32_t index = 0; index < request.RangeCount; ++index) {
            if (!ParseAddress(argv[3 + index * 2],
                              &request.Ranges[index].PhysicalBase) ||
                !ParseAddress(argv[4 + index * 2],
                              &request.Ranges[index].Size)) {
                std::fwprintf(stderr, L"invalid GPA range\n");
                return 1;
            }
        }
    } else if (argc != 2) {
        std::fwprintf(stderr, L"shadow-unbind/status take no arguments\n");
        return 1;
    }

    DeviceHandle device;
    if (!device.Open()) {
        ReportError(L"shadow CreateFile(device)", GetLastError());
        return 2;
    }
    HVR1_MAILBOX_DESCRIPTOR descriptor{};
    DWORD error = ERROR_SUCCESS;
    if (!device.GetMailbox(&descriptor, &error)) {
        ReportError(L"shadow GET_MAILBOX", error);
        return 2;
    }
    HV_EFI_RUNTIME_SHADOW_RESPONSE response{};
    const bool completed = device.Shadow(
        descriptor, request, &response, &error);
    SecureZeroMemory(&descriptor, sizeof(descriptor));
    if (!completed) {
        ReportError(L"authenticated shadow control", error);
        return 2;
    }
    std::wprintf(
        L"shadow: status=%lu state=%lu pid=%llu ranges=%lu active=%lu "
        L"tables=%lu pending=%lu violations=%llu redirects=%llu sync=%llu\n",
        static_cast<unsigned long>(response.Status),
        static_cast<unsigned long>(response.State),
        static_cast<unsigned long long>(response.ProcessId),
        static_cast<unsigned long>(response.RangeCount),
        static_cast<unsigned long>(response.Active),
        static_cast<unsigned long>(response.UsedTablePages),
        static_cast<unsigned long>(response.PendingSynchronization),
        static_cast<unsigned long long>(response.ViolationCount),
        static_cast<unsigned long long>(response.WriteRedirectCount),
        static_cast<unsigned long long>(response.SynchronizationCount));
    return response.Status == HV_EFI_RUNTIME_SHADOW_SUCCESS ? 0 : 3;
}

bool ParseAnalysisPattern(const wchar_t *text,
                          HV_EFI_ANALYSIS_MEMORY_SIGNATURE *signature)
{
    HV_EFI_RUNTIME_SCAN_REQUEST parsed{};
    if (signature == nullptr || !ParsePattern(text, &parsed)) return false;
    signature->Length = parsed.PatternLength;
    std::memcpy(signature->Pattern, parsed.Pattern, parsed.PatternLength);
    for (std::uint32_t index = 0; index < parsed.PatternLength; ++index)
        signature->Mask[index] =
            (parsed.WildcardMask & (UINT64_C(1) << index)) != 0 ? 0 : 0xff;
    return true;
}

bool CopyAsciiIdentifier(const wchar_t *text, char *output,
                         std::size_t outputBytes)
{
    if (text == nullptr || output == nullptr || outputBytes < 2) return false;
    const std::size_t length = std::wcslen(text);
    if (length == 0 || length >= outputBytes) return false;
    for (std::size_t index = 0; index < length; ++index) {
        if (text[index] < 0x20 || text[index] > 0x7e) return false;
        output[index] = static_cast<char>(text[index]);
    }
    return true;
}

std::uint32_t ParseFieldType(const wchar_t *text)
{
    if (_wcsicmp(text, L"f32") == 0) return HV_EFI_ANALYSIS_FIELD_FLOAT32;
    if (_wcsicmp(text, L"f64") == 0) return HV_EFI_ANALYSIS_FIELD_FLOAT64;
    if (_wcsicmp(text, L"i32") == 0) return HV_EFI_ANALYSIS_FIELD_INT32;
    if (_wcsicmp(text, L"i64") == 0) return HV_EFI_ANALYSIS_FIELD_INT64;
    if (_wcsicmp(text, L"vec3") == 0) return HV_EFI_ANALYSIS_FIELD_VECTOR3;
    if (_wcsicmp(text, L"vec4") == 0) return HV_EFI_ANALYSIS_FIELD_VECTOR4;
    if (_wcsicmp(text, L"mat4") == 0) return HV_EFI_ANALYSIS_FIELD_MATRIX4X4;
    if (_wcsicmp(text, L"ptr") == 0) return HV_EFI_ANALYSIS_FIELD_POINTER;
    if (_wcsicmp(text, L"ascii") == 0) return HV_EFI_ANALYSIS_FIELD_ASCII_Z;
    if (_wcsicmp(text, L"utf8") == 0) return HV_EFI_ANALYSIS_FIELD_UTF8_Z;
    if (_wcsicmp(text, L"lp8") == 0)
        return HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED8;
    if (_wcsicmp(text, L"lp16") == 0)
        return HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED16;
    if (_wcsicmp(text, L"lp32") == 0)
        return HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED32;
    return 0;
}

void PrintAnalysis(const HV_EFI_ANALYSIS_RESPONSE &response,
                   bool includeRegions)
{
    std::wprintf(
        L"analysis: status=%lu active=%lu pid=%llu scanned=0x%llX "
        L"regions=%lu results=%lu nodes=%lu fields=%lu truncated=%lu\n",
        static_cast<unsigned long>(response.Status),
        static_cast<unsigned long>(response.SessionActive),
        static_cast<unsigned long long>(response.ProcessId),
        static_cast<unsigned long long>(response.ScannedBytes),
        static_cast<unsigned long>(response.TotalRegionCount),
        static_cast<unsigned long>(response.TotalResultCount),
        static_cast<unsigned long>(response.TotalNodeCount),
        static_cast<unsigned long>(response.TotalFieldCount),
        static_cast<unsigned long>(response.RegionsTruncated));
    if (includeRegions) {
        for (std::uint32_t index = 0;
             index < response.ReturnedRegionCount; ++index) {
            const auto &region = response.Regions[index];
            std::wprintf(
                L"region va=0x%016llX size=0x%llX type=%lu prot=0x%lX\n",
                static_cast<unsigned long long>(region.BaseVirtualAddress),
                static_cast<unsigned long long>(region.Size),
                static_cast<unsigned long>(region.RegionType),
                static_cast<unsigned long>(region.Protection));
        }
    }
    for (std::uint32_t index = 0;
         index < response.ReturnedResultCount; ++index) {
        const auto &result = response.Results[index];
        std::wprintf(L"match %hs va=0x%016llX pa=0x%016llX confidence=%lu\n",
            result.MatchedSignature,
            static_cast<unsigned long long>(result.GuestVirtualAddress),
            static_cast<unsigned long long>(result.HostPhysicalAddress),
            static_cast<unsigned long>(result.MatchConfidence));
    }
    for (std::uint32_t index = 0;
         index < response.ReturnedNodeCount; ++index)
        std::wprintf(L"node 0x%016llX\n",
            static_cast<unsigned long long>(response.Nodes[index]));
    for (std::uint32_t index = 0;
         index < response.ReturnedFieldCount; ++index) {
        const auto &field = response.Fields[index];
        std::wprintf(L"field %hs type=%lu bytes=%lu raw=",
            field.FieldName, static_cast<unsigned long>(field.Type),
            static_cast<unsigned long>(field.ValueBytes));
        for (std::uint32_t byte = 0; byte < field.ValueBytes; ++byte)
            std::wprintf(L"%02X", field.Value.Bytes[byte]);
        std::wprintf(L"\n");
    }
}

int RunAnalysis(int argc, wchar_t **argv)
{
    HV_EFI_ANALYSIS_REQUEST request{};
    request.Size = sizeof(request);
    request.Version = HV_EFI_ANALYSIS_VERSION;
    request.RegionFilter = HV_EFI_ANALYSIS_REGION_FILTER_ALL;
    if (_wcsicmp(argv[1], L"analysis-start") == 0) {
        request.Operation = HV_EFI_ANALYSIS_START;
        if (argc != 3 || !ParseAddress(argv[2], &request.ProcessId)) return 1;
    } else if (_wcsicmp(argv[1], L"analysis-configure") == 0) {
        request.Operation = HV_EFI_ANALYSIS_CONFIGURE;
        request.SignatureCount = 1;
        if ((argc != 4 && argc != 5) ||
            !CopyAsciiIdentifier(argv[2], request.Signatures[0].Identifier,
                                 sizeof(request.Signatures[0].Identifier)) ||
            !ParseAnalysisPattern(argv[3], &request.Signatures[0])) return 1;
        request.Signatures[0].Flags = HV_EFI_ANALYSIS_SIGNATURE_READABLE;
        if (argc == 5) {
            std::uint64_t filter = 0;
            if (!ParseAddress(argv[4], &filter) || filter > UINT32_MAX)
                return 1;
            request.RegionFilter = static_cast<std::uint32_t>(filter);
        }
    } else if (_wcsicmp(argv[1], L"analysis-scan") == 0) {
        request.Operation = HV_EFI_ANALYSIS_SCAN;
        request.ResultLimit = HV_EFI_ANALYSIS_MAX_RESULTS;
        if (argc < 3 || argc > 4) return 1;
        if (_wcsicmp(argv[2], L"first") == 0)
            request.ScanMode = HV_EFI_ANALYSIS_SCAN_FIRST;
        else if (_wcsicmp(argv[2], L"single") == 0)
            request.ScanMode = HV_EFI_ANALYSIS_SCAN_SINGLE;
        else if (_wcsicmp(argv[2], L"all") == 0)
            request.ScanMode = HV_EFI_ANALYSIS_SCAN_ALL;
        else return 1;
        if (argc == 4 && !ParseAddress(argv[3], &request.MaxBytes)) return 1;
    } else if (_wcsicmp(argv[1], L"analysis-traverse") == 0) {
        request.Operation = HV_EFI_ANALYSIS_TRAVERSE;
        request.NodeLimit = HV_EFI_ANALYSIS_MAX_NODES;
        if (argc < 5 || !ParseAddress(argv[2], &request.Address)) return 1;
        std::uint64_t value0 = 0;
        std::uint64_t value1 = 0;
        std::uint64_t value2 = 0;
        if (_wcsicmp(argv[3], L"slist") == 0 && argc == 5 &&
            ParseAddress(argv[4], &value0)) {
            request.StructureType = HV_EFI_ANALYSIS_TRAVERSE_SINGLY_LINKED;
            request.NextOffset = static_cast<std::uint32_t>(value0);
        } else if ((_wcsicmp(argv[3], L"dlist-fwd") == 0 ||
                    _wcsicmp(argv[3], L"dlist-back") == 0) && argc == 6 &&
                   ParseAddress(argv[4], &value0) &&
                   ParseAddress(argv[5], &value1)) {
            request.StructureType = _wcsicmp(argv[3], L"dlist-fwd") == 0
                ? HV_EFI_ANALYSIS_TRAVERSE_DOUBLY_FORWARD
                : HV_EFI_ANALYSIS_TRAVERSE_DOUBLY_BACKWARD;
            request.NextOffset = static_cast<std::uint32_t>(value0);
            request.PreviousOffset = static_cast<std::uint32_t>(value1);
        } else if ((_wcsicmp(argv[3], L"array") == 0 ||
                    _wcsicmp(argv[3], L"ptr-array") == 0) && argc == 6 &&
                   ParseAddress(argv[4], &value0) &&
                   ParseAddress(argv[5], &value1)) {
            request.StructureType = _wcsicmp(argv[3], L"array") == 0
                ? HV_EFI_ANALYSIS_TRAVERSE_CONTIGUOUS_ARRAY
                : HV_EFI_ANALYSIS_TRAVERSE_POINTER_ARRAY;
            request.ElementCount = static_cast<std::uint32_t>(value0);
            request.Stride = static_cast<std::uint32_t>(value1);
        } else if (_wcsicmp(argv[3], L"array2d") == 0 && argc == 7 &&
                   ParseAddress(argv[4], &value0) &&
                   ParseAddress(argv[5], &value1) &&
                   ParseAddress(argv[6], &value2)) {
            request.StructureType = HV_EFI_ANALYSIS_TRAVERSE_ARRAY_2D;
            request.ElementCount = static_cast<std::uint32_t>(value0);
            request.SecondaryCount = static_cast<std::uint32_t>(value1);
            request.Stride = static_cast<std::uint32_t>(value2);
        } else if ((_wcsicmp(argv[3], L"tree") == 0 ||
                    _wcsicmp(argv[3], L"graph") == 0) && argc == 6 &&
                   ParseAddress(argv[4], &value0) &&
                   ParseAddress(argv[5], &value1)) {
            request.StructureType = _wcsicmp(argv[3], L"tree") == 0
                ? HV_EFI_ANALYSIS_TRAVERSE_BINARY_TREE
                : HV_EFI_ANALYSIS_TRAVERSE_GRAPH_BINARY;
            request.Child0Offset = static_cast<std::uint32_t>(value0);
            request.Child1Offset = static_cast<std::uint32_t>(value1);
        } else return 1;
    } else if (_wcsicmp(argv[1], L"analysis-extract") == 0) {
        request.Operation = HV_EFI_ANALYSIS_EXTRACT;
        if (argc < 4 || argc > 6 ||
            !ParseAddress(argv[2], &request.Address) ||
            (request.FieldType = ParseFieldType(argv[3])) == 0) return 1;
        if (argc >= 5 &&
            !CopyAsciiIdentifier(argv[4], request.FieldName,
                                 sizeof(request.FieldName))) return 1;
        if (argc == 6) {
            std::uint64_t bytes = 0;
            if (!ParseAddress(argv[5], &bytes) || bytes > UINT32_MAX) return 1;
            request.ElementCount = static_cast<std::uint32_t>(bytes);
        }
    } else if (_wcsicmp(argv[1], L"analysis-results") == 0) {
        request.Operation = HV_EFI_ANALYSIS_RESULTS;
        if (argc > 3) return 1;
        if (argc == 3) {
            std::uint64_t offset = 0;
            if (!ParseAddress(argv[2], &offset) || offset > UINT32_MAX)
                return 1;
            request.ResultOffset = static_cast<std::uint32_t>(offset);
            request.RegionOffset = static_cast<std::uint32_t>(offset);
        }
    } else if (_wcsicmp(argv[1], L"analysis-stop") == 0) {
        request.Operation = HV_EFI_ANALYSIS_STOP;
        if (argc != 2) return 1;
    } else return 1;

    DeviceHandle device;
    if (!device.Open()) return 2;
    HVR1_MAILBOX_DESCRIPTOR descriptor{};
    DWORD error = ERROR_SUCCESS;
    if (!device.GetMailbox(&descriptor, &error)) return 2;
    HV_EFI_ANALYSIS_RESPONSE response{};
    const bool completed = device.Analysis(
        descriptor, request, &response, &error);
    SecureZeroMemory(&descriptor, sizeof(descriptor));
    if (!completed) {
        ReportError(L"authenticated analysis control", error);
        return 2;
    }
    PrintAnalysis(response,
        request.Operation == HV_EFI_ANALYSIS_RESULTS);
    return response.Status == HV_EFI_ANALYSIS_SUCCESS ||
           response.Status == HV_EFI_ANALYSIS_MORE_DATA ||
           response.Status == HV_EFI_ANALYSIS_NOT_FOUND ||
           response.Status == HV_EFI_ANALYSIS_LIMIT_REACHED ? 0 : 3;
}

int RunService()
{
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), Hvr1ServiceMain },
        { nullptr, nullptr }
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        ReportError(L"StartServiceCtrlDispatcher", GetLastError());
        return 1;
    }
    return 0;
}

void PrintUsage()
{
    std::wprintf(L"usage: hvr1_runtime_service.exe "
                 L"service|status|shutdown|arm-recovery-test|"
                 L"selftest-shutdown|process IMAGE|shadow-bind PID "
                 L"[GPA_START SIZE]...|shadow-unbind|shadow-status|"
                 L"analysis-start PID|analysis-configure ID \"PATTERN\" "
                 L"[REGION_MASK]|analysis-scan MODE [MAX_BYTES]|"
                 L"analysis-traverse ADDRESS TYPE ARGS...|"
                 L"analysis-extract ADDRESS TYPE [NAME] [MAX_BYTES]|"
                 L"analysis-results [OFFSET]|analysis-stop|"
                 L"scan \"PATTERN\" START END "
                 L"[first|all|single]\n");
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    if (argc >= 2 && _wcsnicmp(argv[1], L"analysis-", 9) == 0)
        return RunAnalysis(argc, argv);
    if (argc >= 2 && _wcsicmp(argv[1], L"scan") == 0)
        return RunScan(argc, argv);
    if (argc >= 2 && _wcsicmp(argv[1], L"process") == 0)
        return RunLocateProcess(argc, argv);
    if (argc >= 2 && _wcsicmp(argv[1], L"shadow-bind") == 0)
        return RunShadow(argc, argv, HV_EFI_RUNTIME_SHADOW_BIND);
    if (argc >= 2 && _wcsicmp(argv[1], L"shadow-unbind") == 0)
        return RunShadow(argc, argv, HV_EFI_RUNTIME_SHADOW_UNBIND);
    if (argc >= 2 && _wcsicmp(argv[1], L"shadow-status") == 0)
        return RunShadow(argc, argv, HV_EFI_RUNTIME_SHADOW_QUERY_STATUS);
    if (argc != 2) {
        PrintUsage();
        return 1;
    }
    if (_wcsicmp(argv[1], L"service") == 0)
        return RunService();
    if (_wcsicmp(argv[1], L"status") == 0)
        return RunStatus();
    if (_wcsicmp(argv[1], L"shutdown") == 0)
        return RunShutdown();
    if (_wcsicmp(argv[1], L"selftest-shutdown") == 0)
        return RunAuthenticatedSelfTestShutdown();
    if (_wcsicmp(argv[1], L"arm-recovery-test") == 0)
        return RunArmRecoverySelfTest();
    PrintUsage();
    return 1;
}
