#include "hvr1_auth.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int Require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

} // namespace

int main()
{
    std::uint8_t key[HVR1_AUTH_KEY_BYTES]{};
    std::uint8_t nonce[HVR1_AUTH_NONCE_BYTES]{};
    std::uint8_t tag[HVR1_MAILBOX_TAG_BYTES]{};
    for (std::uint32_t index = 0; index < HVR1_AUTH_KEY_BYTES; ++index)
        key[index] = static_cast<std::uint8_t>(index);
    for (std::uint32_t index = 0; index < HVR1_AUTH_NONCE_BYTES; ++index)
        nonce[index] = static_cast<std::uint8_t>(0xa0u + index);

    HVR1_MAILBOX mailbox{};
    mailbox.Magic = HVR1_PROTOCOL_MAGIC;
    mailbox.Version = HVR1_PROTOCOL_VERSION;
    mailbox.HeaderSize = HVR1_MAILBOX_HEADER_BYTES;
    mailbox.TotalSize = HVR1_MAILBOX_HEADER_BYTES;
    mailbox.Operation = HVR1_MAILBOX_OPERATION_RUNTIME_FRAME;
    mailbox.Generation = UINT64_C(0x1122334455667788);
    mailbox.Sequence = UINT64_C(0x0102030405060708);
    mailbox.PayloadOffset = HVR1_MAILBOX_HEADER_BYTES;

    int failures = 0;
    failures += Require(
        Hvr1ComputeMailboxAuthTag(key, nonce, &mailbox, tag) != 0,
        "known mailbox tag computes");
    const std::uint8_t expected[HVR1_MAILBOX_TAG_BYTES] = {
        0xab, 0x52, 0x5c, 0x61, 0x55, 0xcf, 0x4b, 0xeb,
        0x3d, 0xbf, 0x92, 0x63, 0x5b, 0xa3, 0x2e, 0xd8,
        0x5c, 0xbe, 0xc7, 0x27, 0x15, 0x1f, 0x3a, 0xe9,
        0x13, 0x49, 0x5e, 0xe8, 0x9d, 0x3f, 0x48, 0x71
    };
    failures += Require(std::memcmp(tag, expected, sizeof(tag)) == 0,
                        "tag matches independent HMAC-SHA-256 vector");
    std::memcpy(mailbox.AuthTag, tag, sizeof(tag));
    failures += Require(
        Hvr1VerifyMailboxAuthTag(key, nonce, &mailbox) != 0,
        "valid tag verifies");
    mailbox.Sequence ^= UINT64_C(1);
    failures += Require(
        Hvr1VerifyMailboxAuthTag(key, nonce, &mailbox) == 0,
        "header mutation is rejected");
    mailbox.Sequence ^= UINT64_C(1);
    nonce[0] ^= 1;
    failures += Require(
        Hvr1VerifyMailboxAuthTag(key, nonce, &mailbox) == 0,
        "nonce mutation is rejected");

    if (failures == 0)
        std::puts("HVR1 authentication tests: PASS");
    return failures == 0 ? 0 : 1;
}
