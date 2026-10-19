#ifndef HYPERSINV_EFI_INVENTORY_SECURE_PIPE_H
#define HYPERSINV_EFI_INVENTORY_SECURE_PIPE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace hypersinv::efi_inventory {

inline constexpr wchar_t kCompanionPipeName[] =
    L"\\\\.\\pipe\\HypersinvEfiRuntimeCompanion";
inline constexpr std::size_t kMaximumPipeRequestBytes = 64;
inline constexpr std::size_t kMaximumPipeResponseBytes = 1024 * 1024;
inline constexpr std::size_t kPipeKeyBytes = 32;

// Creates the protected key on first use. Existing keys are only read and
// validated; they are never replaced or regenerated automatically.
bool EnsurePipeKey(std::wstring& error);

// Accepts one authenticated request per connection and then waits for the
// next client. This is a blocking service loop; process shutdown ends it.
bool RunPipeServer(
    const std::function<std::string(const std::string&)>& handler,
    std::wstring& error);

// Connects to the local companion, submits one request, and authenticates the
// response. The request and response sizes are bounded by the constants above.
bool SendPipeRequest(const std::string& request,
                     std::string& response,
                     std::wstring& error);

#if defined(HYPERSINV_EFI_INVENTORY_TESTING)
namespace testing {

using PipeTestKey = std::array<std::uint8_t, kPipeKeyBytes>;
using PipeTestNonce = std::array<std::uint8_t, 32>;
using PipeTestTag = std::array<std::uint8_t, 32>;

bool ComputeClientTag(const PipeTestKey& key,
                      const PipeTestNonce& nonce,
                      const std::string& request,
                      PipeTestTag& tag);
bool VerifyClientTag(const PipeTestKey& key,
                     const PipeTestNonce& nonce,
                     const std::string& request,
                     const PipeTestTag& tag);
bool ComputeResponseTag(const PipeTestKey& key,
                        const PipeTestNonce& nonce,
                        const PipeTestTag& clientTag,
                        const std::string& response,
                        PipeTestTag& tag);
bool VerifyResponseTag(const PipeTestKey& key,
                       const PipeTestNonce& nonce,
                       const PipeTestTag& clientTag,
                       const std::string& response,
                       const PipeTestTag& tag);

} // namespace testing
#endif

} // namespace hypersinv::efi_inventory

#endif // HYPERSINV_EFI_INVENTORY_SECURE_PIPE_H
