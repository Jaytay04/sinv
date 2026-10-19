#include "secure_pipe.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace hypersinv::efi_inventory {
namespace {

constexpr std::array<std::uint8_t, 4> kHelloMagic{'H', 'S', 'P', '1'};
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::size_t kNonceBytes = 32;
constexpr std::size_t kTagBytes = 32;
constexpr std::size_t kHelloBytes = 8 + kNonceBytes;
constexpr DWORD kPipeBufferBytes = 4096;
constexpr wchar_t kKeyFileName[] = L"companion.key";
constexpr wchar_t kRestrictedAcl[] = L"D:P(A;;FA;;;SY)(A;;FA;;;BA)";
constexpr DWORD kKeyProvisionRetryCount = 40;
constexpr DWORD kKeyProvisionRetryDelayMs = 25;

using Key = std::array<std::uint8_t, kPipeKeyBytes>;
using Nonce = std::array<std::uint8_t, kNonceBytes>;
using Tag = std::array<std::uint8_t, kTagBytes>;

class LocalFreeGuard final {
public:
    explicit LocalFreeGuard(void* value = nullptr) noexcept : value_(value) {}
    LocalFreeGuard(const LocalFreeGuard&) = delete;
    LocalFreeGuard& operator=(const LocalFreeGuard&) = delete;
    ~LocalFreeGuard() {
        if (value_ != nullptr) {
            LocalFree(value_);
        }
    }
    void* get() const noexcept { return value_; }

private:
    void* value_;
};

class CoTaskMemGuard final {
public:
    explicit CoTaskMemGuard(void* value = nullptr) noexcept : value_(value) {}
    CoTaskMemGuard(const CoTaskMemGuard&) = delete;
    CoTaskMemGuard& operator=(const CoTaskMemGuard&) = delete;
    ~CoTaskMemGuard() {
        if (value_ != nullptr) {
            CoTaskMemFree(value_);
        }
    }

private:
    void* value_;
};

class HandleGuard final {
public:
    explicit HandleGuard(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        : handle_(handle) {}
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
    HandleGuard(HandleGuard&& other) noexcept : handle_(other.release()) {}
    HandleGuard& operator=(HandleGuard&& other) noexcept {
        if (this != &other) {
            Close();
            handle_ = other.release();
        }
        return *this;
    }
    ~HandleGuard() { Close(); }

    HANDLE get() const noexcept { return handle_; }
    bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() noexcept {
        const HANDLE released = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return released;
    }
    void Close() noexcept {
        if (valid()) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE handle_;
};

void SetWin32Error(std::wstring& error,
                   const wchar_t* operation,
                   DWORD status = GetLastError()) {
    error = operation;
    error += L" failed (Win32 error ";
    error += std::to_wstring(status);
    error += L")";
}

void SetNtError(std::wstring& error,
                const wchar_t* operation,
                NTSTATUS status) {
    error = operation;
    error += L" failed (NTSTATUS ";
    error += std::to_wstring(static_cast<unsigned long>(status));
    error += L")";
}

bool MakeRestrictedSecurityAttributes(PSECURITY_DESCRIPTOR& descriptor,
                                      SECURITY_ATTRIBUTES& attributes,
                                      std::wstring& error) {
    descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kRestrictedAcl,
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        SetWin32Error(error, L"ConvertStringSecurityDescriptorToSecurityDescriptorW");
        return false;
    }
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    return true;
}

bool IsExpectedRestrictedDacl(HANDLE handle,
                              std::wstring& error,
                              const wchar_t* objectName) {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD queryStatus = GetSecurityInfo(
        handle,
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        &dacl,
        nullptr,
        &descriptor);
    LocalFreeGuard descriptorGuard(descriptor);
    if (queryStatus != ERROR_SUCCESS) {
        SetWin32Error(error, L"GetSecurityInfo", queryStatus);
        return false;
    }
    if (dacl == nullptr) {
        error = objectName;
        error += L" has a null DACL";
        return false;
    }

    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) {
        SetWin32Error(error, L"GetSecurityDescriptorControl");
        return false;
    }
    if ((control & SE_DACL_PROTECTED) == 0 || dacl->AceCount != 2) {
        error = objectName;
        error += L" does not have the required protected SYSTEM/Administrators DACL";
        return false;
    }

    BYTE systemSidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD systemSidBytes = sizeof(systemSidBuffer);
    BYTE adminsSidBuffer[SECURITY_MAX_SID_SIZE]{};
    DWORD adminsSidBytes = sizeof(adminsSidBuffer);
    if (!CreateWellKnownSid(WinLocalSystemSid,
                            nullptr,
                            systemSidBuffer,
                            &systemSidBytes) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid,
                            nullptr,
                            adminsSidBuffer,
                            &adminsSidBytes)) {
        SetWin32Error(error, L"CreateWellKnownSid");
        return false;
    }

    bool sawSystem = false;
    bool sawAdministrators = false;
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* rawAce = nullptr;
        if (!GetAce(dacl, index, &rawAce)) {
            SetWin32Error(error, L"GetAce");
            return false;
        }
        const auto* header = static_cast<const ACE_HEADER*>(rawAce);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE || header->AceFlags != 0) {
            error = objectName;
            error += L" has an unexpected access-control entry";
            return false;
        }
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
        const PSID sid = const_cast<DWORD*>(&ace->SidStart);
        if (ace->Mask != FILE_ALL_ACCESS) {
            error = objectName;
            error += L" grants a non-canonical access mask";
            return false;
        }
        if (EqualSid(sid, systemSidBuffer)) {
            if (sawSystem) {
                error = objectName;
                error += L" contains duplicate SYSTEM access entries";
                return false;
            }
            sawSystem = true;
        } else if (EqualSid(sid, adminsSidBuffer)) {
            if (sawAdministrators) {
                error = objectName;
                error += L" contains duplicate Administrators access entries";
                return false;
            }
            sawAdministrators = true;
        } else {
            error = objectName;
            error += L" grants access to an unexpected principal";
            return false;
        }
    }
    if (!sawSystem || !sawAdministrators) {
        error = objectName;
        error += L" is missing SYSTEM or Administrators access";
        return false;
    }
    return true;
}

bool GetAttributes(HANDLE handle, DWORD& attributes) {
    FILE_ATTRIBUTE_TAG_INFO tagInfo{};
    if (!GetFileInformationByHandleEx(handle,
                                      FileAttributeTagInfo,
                                      &tagInfo,
                                      sizeof(tagInfo))) {
        return false;
    }
    attributes = tagInfo.FileAttributes;
    return true;
}

bool EnsureSecureDirectory(const std::wstring& path, std::wstring& error) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES securityAttributes{};
    if (!MakeRestrictedSecurityAttributes(descriptor, securityAttributes, error)) {
        return false;
    }
    LocalFreeGuard descriptorGuard(descriptor);

    if (!CreateDirectoryW(path.c_str(), &securityAttributes)) {
        const DWORD createStatus = GetLastError();
        if (createStatus != ERROR_ALREADY_EXISTS) {
            SetWin32Error(error, L"CreateDirectoryW", createStatus);
            return false;
        }
    }

    HandleGuard directory(CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES | READ_CONTROL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr));
    if (!directory.valid()) {
        SetWin32Error(error, L"Open key directory");
        return false;
    }
    DWORD attributes = 0;
    if (!GetAttributes(directory.get(), attributes)) {
        SetWin32Error(error, L"GetFileInformationByHandleEx(directory)");
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        error = L"Key directory is not a plain directory: ";
        error += path;
        return false;
    }
    return IsExpectedRestrictedDacl(directory.get(), error, L"Key directory");
}

bool GetKeyFilePath(std::wstring& directoryPath,
                    std::wstring& keyPath,
                    std::wstring& error) {
    PWSTR programData = nullptr;
    const HRESULT result = SHGetKnownFolderPath(
        FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &programData);
    CoTaskMemGuard programDataGuard(programData);
    if (FAILED(result) || programData == nullptr) {
        error = L"SHGetKnownFolderPath(FOLDERID_ProgramData) failed (HRESULT ";
        error += std::to_wstring(static_cast<unsigned long>(result));
        error += L")";
        return false;
    }
    std::wstring root(programData);
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) {
        root.pop_back();
    }
    const std::wstring hypersinvPath = root + L"\\Hypersinv";
    directoryPath = hypersinvPath + L"\\EfiInventory";
    keyPath = directoryPath + L"\\" + kKeyFileName;
    return true;
}

bool EnsureKeyDirectory(std::wstring& keyPath, std::wstring& error) {
    std::wstring inventoryPath;
    if (!GetKeyFilePath(inventoryPath, keyPath, error)) {
        return false;
    }
    const std::size_t separator = inventoryPath.find_last_of(L'\\');
    if (separator == std::wstring::npos) {
        error = L"Could not resolve the companion key directory";
        return false;
    }
    const std::wstring hypersinvPath = inventoryPath.substr(0, separator);
    if (!EnsureSecureDirectory(hypersinvPath, error) ||
        !EnsureSecureDirectory(inventoryPath, error)) {
        return false;
    }
    return true;
}

bool ReadExact(HANDLE handle, std::uint8_t* buffer, std::size_t bytes) {
    std::size_t completed = 0;
    while (completed < bytes) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes - completed, std::numeric_limits<DWORD>::max()));
        DWORD transferred = 0;
        if (!ReadFile(handle,
                      buffer + completed,
                      request,
                      &transferred,
                      nullptr) ||
            transferred == 0) {
            return false;
        }
        completed += transferred;
    }
    return true;
}

bool WriteExact(HANDLE handle, const std::uint8_t* buffer, std::size_t bytes) {
    std::size_t completed = 0;
    while (completed < bytes) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            bytes - completed, std::numeric_limits<DWORD>::max()));
        DWORD transferred = 0;
        if (!WriteFile(handle,
                       buffer + completed,
                       request,
                       &transferred,
                       nullptr) ||
            transferred == 0) {
            return false;
        }
        completed += transferred;
    }
    return true;
}

bool ReadPipeKey(const std::wstring& path, Key& key, std::wstring& error) {
    HandleGuard file;
    DWORD openStatus = ERROR_SUCCESS;
    bool opened = false;
    for (DWORD attempt = 0; attempt <= kKeyProvisionRetryCount; ++attempt) {
        HANDLE raw = CreateFileW(path.c_str(),
                                 GENERIC_READ | READ_CONTROL,
                                 FILE_SHARE_READ,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT |
                                     FILE_FLAG_SEQUENTIAL_SCAN,
                                 nullptr);
        if (raw != INVALID_HANDLE_VALUE) {
            file = HandleGuard(raw);
            opened = true;
            break;
        }
        openStatus = GetLastError();
        if (openStatus != ERROR_SHARING_VIOLATION &&
            openStatus != ERROR_LOCK_VIOLATION) {
            break;
        }
        Sleep(kKeyProvisionRetryDelayMs);
    }
    if (!opened) {
        SetWin32Error(error, L"Open companion key", openStatus);
        return false;
    }

    BY_HANDLE_FILE_INFORMATION fileInfo{};
    if (!GetFileInformationByHandle(file.get(), &fileInfo)) {
        SetWin32Error(error, L"GetFileInformationByHandle(key)");
        return false;
    }
    if ((fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        error = L"Companion key is not a plain file";
        return false;
    }
    if (!IsExpectedRestrictedDacl(file.get(), error, L"Companion key")) {
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) {
        SetWin32Error(error, L"GetFileSizeEx(companion key)");
        return false;
    }
    if (size.QuadPart != static_cast<LONGLONG>(key.size())) {
        error = L"Companion key must contain exactly 32 bytes; refusing to replace it";
        return false;
    }
    if (!ReadExact(file.get(), key.data(), key.size())) {
        SetWin32Error(error, L"ReadFile(companion key)");
        SecureZeroMemory(key.data(), key.size());
        return false;
    }
    return true;
}

bool ProvisionPipeKey(const std::wstring& path, std::wstring& error) {
    Key generated{};
    const NTSTATUS randomStatus = BCryptGenRandom(
        nullptr,
        generated.data(),
        static_cast<ULONG>(generated.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (randomStatus < 0) {
        SetNtError(error, L"BCryptGenRandom(companion key)", randomStatus);
        return false;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES securityAttributes{};
    if (!MakeRestrictedSecurityAttributes(descriptor, securityAttributes, error)) {
        SecureZeroMemory(generated.data(), generated.size());
        return false;
    }
    LocalFreeGuard descriptorGuard(descriptor);

    HandleGuard file(CreateFileW(path.c_str(),
                                 GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                 0,
                                 &securityAttributes,
                                 CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL |
                                     FILE_FLAG_WRITE_THROUGH |
                                     FILE_FLAG_OPEN_REPARSE_POINT,
                                 nullptr));
    if (!file.valid()) {
        const DWORD statusCode = GetLastError();
        SecureZeroMemory(generated.data(), generated.size());
        if (statusCode == ERROR_FILE_EXISTS ||
            statusCode == ERROR_ALREADY_EXISTS) {
            return true;
        }
        SetWin32Error(error, L"Create companion key without overwrite", statusCode);
        return false;
    }

    bool success = IsExpectedRestrictedDacl(file.get(), error, L"New companion key");
    if (success && !WriteExact(file.get(), generated.data(), generated.size())) {
        SetWin32Error(error, L"WriteFile(new companion key)");
        success = false;
    }
    if (success && !FlushFileBuffers(file.get())) {
        SetWin32Error(error, L"FlushFileBuffers(companion key)");
        success = false;
    }
    SecureZeroMemory(generated.data(), generated.size());
    file.Close();
    if (!success) {
        // This handle was created with CREATE_NEW by this call, so removing its
        // incomplete file cannot replace or delete a pre-existing key.
        if (!DeleteFileW(path.c_str())) {
            error += L"; incomplete newly-created key could not be removed (Win32 error ";
            error += std::to_wstring(GetLastError());
            error += L")";
        }
    }
    return success;
}

bool EnsurePipeKeyInternal(std::wstring& path, std::wstring& error) {
    if (!EnsureKeyDirectory(path, error)) {
        return false;
    }
    if (!ProvisionPipeKey(path, error)) {
        return false;
    }
    Key key{};
    const bool valid = ReadPipeKey(path, key, error);
    SecureZeroMemory(key.data(), key.size());
    return valid;
}

void WriteU32(std::uint8_t* destination, std::uint32_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value & 0xffu);
    destination[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    destination[2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    destination[3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::uint32_t ReadU32(const std::uint8_t* source) noexcept {
    return static_cast<std::uint32_t>(source[0]) |
        (static_cast<std::uint32_t>(source[1]) << 8) |
        (static_cast<std::uint32_t>(source[2]) << 16) |
        (static_cast<std::uint32_t>(source[3]) << 24);
}

void SecureClear(Tag& tag) noexcept {
    SecureZeroMemory(tag.data(), tag.size());
}

bool ComputeHmac(const Key& key,
                 const std::string_view domain,
                 const Nonce& nonce,
                 const Tag* clientTag,
                 const std::string& message,
                 std::size_t maximumLength,
                 Tag& output,
                 std::wstring* error) {
    SecureClear(output);
    if (message.size() > maximumLength ||
        message.size() > std::numeric_limits<std::uint32_t>::max()) {
        if (error != nullptr) {
            *error = L"Message exceeds the protocol size limit";
        }
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::uint8_t> hashObject;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status >= 0) {
        DWORD objectBytes = 0;
        DWORD resultBytes = 0;
        status = BCryptGetProperty(algorithm,
                                  BCRYPT_OBJECT_LENGTH,
                                  reinterpret_cast<PUCHAR>(&objectBytes),
                                  sizeof(objectBytes),
                                  &resultBytes,
                                  0);
        if (status >= 0 && resultBytes != sizeof(objectBytes)) {
            status = STATUS_INVALID_PARAMETER;
        }
        if (status >= 0) {
            try {
                hashObject.resize(objectBytes);
            } catch (const std::bad_alloc&) {
                status = STATUS_NO_MEMORY;
            }
        }
    }
    if (status >= 0) {
        status = BCryptCreateHash(algorithm,
                                 &hash,
                                 hashObject.data(),
                                 static_cast<ULONG>(hashObject.size()),
                                 const_cast<PUCHAR>(key.data()),
                                 static_cast<ULONG>(key.size()),
                                 0);
    }
    const auto hashPart = [&status, hash](const std::uint8_t* data,
                                         std::size_t bytes) {
        if (status < 0 || bytes == 0) {
            return;
        }
        if (bytes > std::numeric_limits<ULONG>::max()) {
            status = STATUS_INVALID_PARAMETER;
            return;
        }
        status = BCryptHashData(hash,
                                const_cast<PUCHAR>(data),
                                static_cast<ULONG>(bytes),
                                0);
    };
    hashPart(reinterpret_cast<const std::uint8_t*>(domain.data()), domain.size());
    hashPart(nonce.data(), nonce.size());
    if (clientTag != nullptr) {
        hashPart(clientTag->data(), clientTag->size());
    }
    std::array<std::uint8_t, sizeof(std::uint32_t)> encodedLength{};
    WriteU32(encodedLength.data(), static_cast<std::uint32_t>(message.size()));
    hashPart(encodedLength.data(), encodedLength.size());
    hashPart(reinterpret_cast<const std::uint8_t*>(message.data()), message.size());
    if (status >= 0) {
        status = BCryptFinishHash(hash,
                                  output.data(),
                                  static_cast<ULONG>(output.size()),
                                  0);
    }

    if (hash != nullptr) {
        BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (!hashObject.empty()) {
        SecureZeroMemory(hashObject.data(), hashObject.size());
    }
    if (status < 0) {
        SecureClear(output);
        if (error != nullptr) {
            SetNtError(*error, L"BCrypt HMAC-SHA-256", status);
        }
        return false;
    }
    return true;
}

bool ConstantTimeEqual(const Tag& expected, const Tag& supplied) noexcept {
    volatile std::uint8_t difference = 0;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        difference = static_cast<std::uint8_t>(
            difference | (expected[index] ^ supplied[index]));
    }
    return difference == 0;
}

bool MakeClientTag(const Key& key,
                   const Nonce& nonce,
                   const std::string& request,
                   Tag& tag,
                   std::wstring* error) {
    return ComputeHmac(key,
                       "Hypersinv-EfiInventory-client-v1",
                       nonce,
                       nullptr,
                       request,
                       kMaximumPipeRequestBytes,
                       tag,
                       error);
}

bool MakeServerTag(const Key& key,
                   const Nonce& nonce,
                   const Tag& clientTag,
                   const std::string& response,
                   Tag& tag,
                   std::wstring* error) {
    return ComputeHmac(key,
                       "Hypersinv-EfiInventory-server-v1",
                       nonce,
                       &clientTag,
                       response,
                       kMaximumPipeResponseBytes,
                       tag,
                       error);
}

bool ReadPipeKeyForUse(Key& key, std::wstring& error) {
    std::wstring keyPath;
    if (!EnsureKeyDirectory(keyPath, error)) {
        return false;
    }
    return ReadPipeKey(keyPath, key, error);
}

bool ReadClientIdentity(HANDLE pipe, Nonce& nonce, std::wstring& error) {
    std::array<std::uint8_t, kHelloBytes> hello{};
    const NTSTATUS randomStatus = BCryptGenRandom(
        nullptr,
        nonce.data(),
        static_cast<ULONG>(nonce.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (randomStatus < 0) {
        SetNtError(error, L"BCryptGenRandom(challenge)", randomStatus);
        return false;
    }
    std::copy(kHelloMagic.begin(), kHelloMagic.end(), hello.begin());
    hello[4] = kProtocolVersion;
    hello[5] = 0;
    hello[6] = 0;
    hello[7] = 0;
    std::copy(nonce.begin(), nonce.end(), hello.begin() + 8);
    if (!WriteExact(pipe, hello.data(), hello.size())) {
        SetWin32Error(error, L"WritePipe(challenge)");
        return false;
    }
    return true;
}

bool IsPrivilegedPipeClient(HANDLE pipe, bool& revertFailed) {
    revertFailed = false;
    if (!ImpersonateNamedPipeClient(pipe)) {
        return false;
    }
    HANDLE rawToken = nullptr;
    const bool opened = OpenThreadToken(GetCurrentThread(),
                                        TOKEN_QUERY,
                                        TRUE,
                                        &rawToken) != FALSE;
    HandleGuard token(rawToken);
    bool allowed = false;
    if (opened) {
        BYTE systemSidBuffer[SECURITY_MAX_SID_SIZE]{};
        DWORD systemSidBytes = sizeof(systemSidBuffer);
        BYTE adminsSidBuffer[SECURITY_MAX_SID_SIZE]{};
        DWORD adminsSidBytes = sizeof(adminsSidBuffer);
        if (CreateWellKnownSid(WinLocalSystemSid,
                               nullptr,
                               systemSidBuffer,
                               &systemSidBytes) &&
            CreateWellKnownSid(WinBuiltinAdministratorsSid,
                               nullptr,
                               adminsSidBuffer,
                               &adminsSidBytes)) {
            BOOL isSystem = FALSE;
            BOOL isAdministrator = FALSE;
            allowed = CheckTokenMembership(token.get(), systemSidBuffer, &isSystem) &&
                CheckTokenMembership(token.get(), adminsSidBuffer, &isAdministrator) &&
                (isSystem || isAdministrator);
        }
    }
    revertFailed = RevertToSelf() == FALSE;
    return allowed && !revertFailed;
}

void DisconnectPipe(HANDLE pipe) noexcept {
    DisconnectNamedPipe(pipe);
}

bool HandleOneConnection(HANDLE pipe,
                         const Key& key,
                         const std::function<std::string(const std::string&)>& handler,
                         std::wstring& serverError) {
    Nonce nonce{};
    std::wstring ignoredError;
    if (!ReadClientIdentity(pipe, nonce, ignoredError)) {
        DisconnectPipe(pipe);
        return true;
    }

    std::array<std::uint8_t, sizeof(std::uint32_t)> lengthBytes{};
    if (!ReadExact(pipe, lengthBytes.data(), lengthBytes.size())) {
        DisconnectPipe(pipe);
        return true;
    }
    const std::uint32_t requestLength = ReadU32(lengthBytes.data());
    if (requestLength > kMaximumPipeRequestBytes) {
        DisconnectPipe(pipe);
        return true;
    }
    std::string request(requestLength, '\0');
    if (requestLength != 0 &&
        !ReadExact(pipe,
                   reinterpret_cast<std::uint8_t*>(request.data()),
                   request.size())) {
        DisconnectPipe(pipe);
        return true;
    }
    Tag suppliedClientTag{};
    if (!ReadExact(pipe, suppliedClientTag.data(), suppliedClientTag.size())) {
        DisconnectPipe(pipe);
        return true;
    }
    // Windows binds the impersonation context to a client message. Read the
    // complete bounded frame before checking that token, then authenticate the
    // frame before invoking the request handler.
    bool revertFailed = false;
    if (!IsPrivilegedPipeClient(pipe, revertFailed)) {
        SecureClear(suppliedClientTag);
        DisconnectPipe(pipe);
        if (revertFailed) {
            SetWin32Error(serverError, L"RevertToSelf after pipe client check");
        }
        return !revertFailed;
    }
    Tag expectedClientTag{};
    const bool computed = MakeClientTag(key, nonce, request, expectedClientTag,
                                        &ignoredError);
    const bool authenticated = computed &&
        ConstantTimeEqual(expectedClientTag, suppliedClientTag);
    SecureClear(expectedClientTag);
    if (!authenticated) {
        SecureClear(suppliedClientTag);
        DisconnectPipe(pipe);
        return true;
    }

    std::string response;
    try {
        response = handler(request);
    } catch (const std::exception&) {
        response = "{\"error\":\"request_handler_failed\"}";
    } catch (...) {
        response = "{\"error\":\"request_handler_failed\"}";
    }
    if (response.size() > kMaximumPipeResponseBytes) {
        response = "{\"error\":\"response_too_large\"}";
    }

    Tag serverTag{};
    if (!MakeServerTag(key, nonce, suppliedClientTag, response, serverTag,
                       &ignoredError)) {
        SecureClear(suppliedClientTag);
        DisconnectPipe(pipe);
        return true;
    }
    std::array<std::uint8_t, sizeof(std::uint32_t)> responseLengthBytes{};
    WriteU32(responseLengthBytes.data(),
             static_cast<std::uint32_t>(response.size()));
    const bool sent = WriteExact(pipe,
                                 responseLengthBytes.data(),
                                 responseLengthBytes.size()) &&
        (response.empty() || WriteExact(
            pipe,
            reinterpret_cast<const std::uint8_t*>(response.data()),
            response.size())) &&
        WriteExact(pipe, serverTag.data(), serverTag.size());
    SecureClear(suppliedClientTag);
    SecureClear(serverTag);
    DisconnectPipe(pipe);
    return sent;
}

bool ReadChallenge(HANDLE pipe, Nonce& nonce, std::wstring& error) {
    std::array<std::uint8_t, kHelloBytes> hello{};
    if (!ReadExact(pipe, hello.data(), hello.size())) {
        SetWin32Error(error, L"ReadPipe(challenge)");
        return false;
    }
    if (!std::equal(kHelloMagic.begin(), kHelloMagic.end(), hello.begin()) ||
        hello[4] != kProtocolVersion || hello[5] != 0 ||
        hello[6] != 0 || hello[7] != 0) {
        error = L"Companion returned an unsupported challenge frame";
        return false;
    }
    std::copy(hello.begin() + 8, hello.end(), nonce.begin());
    return true;
}

bool SendRequestFrame(HANDLE pipe,
                      const Key& key,
                      const Nonce& nonce,
                      const std::string& request,
                      Tag& clientTag,
                      std::wstring& error) {
    if (!MakeClientTag(key, nonce, request, clientTag, &error)) {
        return false;
    }
    std::array<std::uint8_t, sizeof(std::uint32_t)> lengthBytes{};
    WriteU32(lengthBytes.data(), static_cast<std::uint32_t>(request.size()));
    if (!WriteExact(pipe, lengthBytes.data(), lengthBytes.size()) ||
        (!request.empty() && !WriteExact(
            pipe,
            reinterpret_cast<const std::uint8_t*>(request.data()),
            request.size())) ||
        !WriteExact(pipe, clientTag.data(), clientTag.size())) {
        SetWin32Error(error, L"WritePipe(request)");
        SecureClear(clientTag);
        return false;
    }
    return true;
}

bool ReceiveResponseFrame(HANDLE pipe,
                          const Key& key,
                          const Nonce& nonce,
                          const Tag& clientTag,
                          std::string& response,
                          std::wstring& error) {
    std::array<std::uint8_t, sizeof(std::uint32_t)> lengthBytes{};
    if (!ReadExact(pipe, lengthBytes.data(), lengthBytes.size())) {
        SetWin32Error(error, L"ReadPipe(response length)");
        return false;
    }
    const std::uint32_t responseLength = ReadU32(lengthBytes.data());
    if (responseLength > kMaximumPipeResponseBytes) {
        error = L"Companion response exceeds the 1 MiB limit";
        return false;
    }
    response.assign(responseLength, '\0');
    if (responseLength != 0 &&
        !ReadExact(pipe,
                   reinterpret_cast<std::uint8_t*>(response.data()),
                   response.size())) {
        SetWin32Error(error, L"ReadPipe(response body)");
        response.clear();
        return false;
    }
    Tag suppliedTag{};
    if (!ReadExact(pipe, suppliedTag.data(), suppliedTag.size())) {
        SetWin32Error(error, L"ReadPipe(response authentication tag)");
        response.clear();
        return false;
    }
    Tag expectedTag{};
    const bool computed = MakeServerTag(key, nonce, clientTag, response,
                                        expectedTag, &error);
    const bool authenticated = computed && ConstantTimeEqual(expectedTag, suppliedTag);
    SecureClear(expectedTag);
    SecureClear(suppliedTag);
    if (!authenticated) {
        response.clear();
        if (error.empty()) {
            error = L"Companion response authentication failed";
        }
        return false;
    }
    return true;
}

} // namespace

bool EnsurePipeKey(std::wstring& error) {
    error.clear();
    std::wstring path;
    return EnsurePipeKeyInternal(path, error);
}

bool RunPipeServer(
    const std::function<std::string(const std::string&)>& handler,
    std::wstring& error) {
    error.clear();
    if (!handler) {
        error = L"Pipe server requires a request handler";
        return false;
    }
    if (!EnsurePipeKey(error)) {
        return false;
    }
    Key key{};
    if (!ReadPipeKeyForUse(key, error)) {
        return false;
    }

    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES securityAttributes{};
    if (!MakeRestrictedSecurityAttributes(descriptor, securityAttributes, error)) {
        SecureZeroMemory(key.data(), key.size());
        return false;
    }
    LocalFreeGuard descriptorGuard(descriptor);

    // The pipe ACL and client token check are the primary authorization gate.
    // The shared HMAC key adds challenge/response integrity for the protocol;
    // it cannot protect against an administrator or SYSTEM compromise.
    for (;;) {
        HandleGuard pipe(CreateNamedPipeW(
            kCompanionPipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            kPipeBufferBytes,
            kPipeBufferBytes,
            0,
            &securityAttributes));
        if (!pipe.valid()) {
            SetWin32Error(error, L"CreateNamedPipeW");
            SecureZeroMemory(key.data(), key.size());
            return false;
        }
        const BOOL connected = ConnectNamedPipe(pipe.get(), nullptr);
        const DWORD connectStatus = connected ? ERROR_SUCCESS : GetLastError();
        if (!connected && connectStatus != ERROR_PIPE_CONNECTED) {
            if (connectStatus == ERROR_NO_DATA ||
                connectStatus == ERROR_OPERATION_ABORTED) {
                continue;
            }
            SetWin32Error(error, L"ConnectNamedPipe", connectStatus);
            SecureZeroMemory(key.data(), key.size());
            return false;
        }
        // A malformed, unauthenticated, or disconnected client is isolated to
        // its own connection; the service continues accepting later clients.
        if (!HandleOneConnection(pipe.get(), key, handler, error)) {
            SecureZeroMemory(key.data(), key.size());
            return false;
        }
    }
}

bool SendPipeRequest(const std::string& request,
                     std::string& response,
                     std::wstring& error) {
    error.clear();
    response.clear();
    if (request.size() > kMaximumPipeRequestBytes) {
        error = L"Pipe request exceeds the 64 byte limit";
        return false;
    }
    if (!EnsurePipeKey(error)) {
        return false;
    }
    Key key{};
    if (!ReadPipeKeyForUse(key, error)) {
        return false;
    }

    if (!WaitNamedPipeW(kCompanionPipeName, 5000)) {
        SetWin32Error(error, L"WaitNamedPipeW");
        SecureZeroMemory(key.data(), key.size());
        return false;
    }
    HandleGuard pipe(CreateFileW(kCompanionPipeName,
                                 GENERIC_READ | GENERIC_WRITE,
                                 0,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr));
    if (!pipe.valid()) {
        SetWin32Error(error, L"Open companion named pipe");
        SecureZeroMemory(key.data(), key.size());
        return false;
    }
    DWORD mode = PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr)) {
        SetWin32Error(error, L"SetNamedPipeHandleState");
        SecureZeroMemory(key.data(), key.size());
        return false;
    }

    Nonce nonce{};
    Tag clientTag{};
    const bool success = ReadChallenge(pipe.get(), nonce, error) &&
        SendRequestFrame(pipe.get(), key, nonce, request, clientTag, error) &&
        ReceiveResponseFrame(pipe.get(), key, nonce, clientTag,
                             response, error);
    SecureZeroMemory(key.data(), key.size());
    SecureZeroMemory(nonce.data(), nonce.size());
    SecureClear(clientTag);
    if (!success) {
        response.clear();
    }
    return success;
}

#if defined(HYPERSINV_EFI_INVENTORY_TESTING)
namespace testing {

bool ComputeClientTag(const PipeTestKey& key,
                      const PipeTestNonce& nonce,
                      const std::string& request,
                      PipeTestTag& tag) {
    return MakeClientTag(key, nonce, request, tag, nullptr);
}

bool VerifyClientTag(const PipeTestKey& key,
                     const PipeTestNonce& nonce,
                     const std::string& request,
                     const PipeTestTag& tag) {
    PipeTestTag expected{};
    const bool computed = ComputeClientTag(key, nonce, request, expected);
    const bool valid = computed && ConstantTimeEqual(expected, tag);
    SecureClear(expected);
    return valid;
}

bool ComputeResponseTag(const PipeTestKey& key,
                        const PipeTestNonce& nonce,
                        const PipeTestTag& clientTag,
                        const std::string& response,
                        PipeTestTag& tag) {
    return MakeServerTag(key, nonce, clientTag, response, tag, nullptr);
}

bool VerifyResponseTag(const PipeTestKey& key,
                       const PipeTestNonce& nonce,
                       const PipeTestTag& clientTag,
                       const std::string& response,
                       const PipeTestTag& tag) {
    PipeTestTag expected{};
    const bool computed = ComputeResponseTag(key, nonce, clientTag,
                                             response, expected);
    const bool valid = computed && ConstantTimeEqual(expected, tag);
    SecureClear(expected);
    return valid;
}

} // namespace testing
#endif

} // namespace hypersinv::efi_inventory
