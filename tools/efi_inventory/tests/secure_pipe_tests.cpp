#include "secure_pipe.h"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace hypersinv::efi_inventory;

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int main() {
    using namespace hypersinv::efi_inventory::testing;

    PipeTestKey key{};
    PipeTestNonce nonce{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index + 1);
        nonce[index] = static_cast<std::uint8_t>(0xa0u + index);
    }

    const std::string request = "inventory.read";
    PipeTestTag clientTag{};
    if (!ComputeClientTag(key, nonce, request, clientTag) ||
        !VerifyClientTag(key, nonce, request, clientTag)) {
        return Fail("valid client challenge tag");
    }
    if (VerifyClientTag(key, nonce, request + "!", clientTag)) {
        return Fail("changed request must fail client authentication");
    }
    PipeTestNonce changedNonce = nonce;
    changedNonce[0] ^= 0x1u;
    if (VerifyClientTag(key, changedNonce, request, clientTag)) {
        return Fail("changed challenge must fail client authentication");
    }

    const std::string response = "{\"status\":\"ok\"}";
    PipeTestTag serverTag{};
    if (!ComputeResponseTag(key, nonce, clientTag, response, serverTag) ||
        !VerifyResponseTag(key, nonce, clientTag, response, serverTag)) {
        return Fail("valid server response tag");
    }
    if (VerifyResponseTag(key, nonce, clientTag, response + "!", serverTag)) {
        return Fail("changed response must fail server authentication");
    }
    if (VerifyResponseTag(key, nonce, serverTag, response, serverTag)) {
        return Fail("response tag must bind the client request tag");
    }

    std::string maximumRequest(kMaximumPipeRequestBytes, 'r');
    PipeTestTag maximumRequestTag{};
    if (!ComputeClientTag(key, nonce, maximumRequest, maximumRequestTag)) {
        return Fail("64 byte request is accepted");
    }
    maximumRequest.push_back('x');
    if (ComputeClientTag(key, nonce, maximumRequest, maximumRequestTag)) {
        return Fail("requests larger than 64 bytes are rejected");
    }

    std::string maximumResponse(kMaximumPipeResponseBytes, 's');
    PipeTestTag maximumResponseTag{};
    if (!ComputeResponseTag(key,
                            nonce,
                            clientTag,
                            maximumResponse,
                            maximumResponseTag)) {
        return Fail("1 MiB response is accepted");
    }
    maximumResponse.push_back('x');
    if (ComputeResponseTag(key,
                           nonce,
                           clientTag,
                           maximumResponse,
                           maximumResponseTag)) {
        return Fail("responses larger than 1 MiB are rejected");
    }

    std::puts("PASS: secure companion pipe protocol authentication and bounds");
    return 0;
}
