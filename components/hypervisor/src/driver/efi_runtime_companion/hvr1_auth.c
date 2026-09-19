#if defined(HVR1_KERNEL_MODE)
#include <ntddk.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <bcrypt.h>

#include "hvr1_auth.h"

static void
Hvr1ClearBytes(void *memory, SIZE_T bytes)
{
    volatile UCHAR *cursor = (volatile UCHAR *)memory;
    while (bytes-- != 0)
        *cursor++ = 0;
}

int
Hvr1ComputeMailboxAuthTag(
    const uint8_t key[HVR1_AUTH_KEY_BYTES],
    const uint8_t nonce[HVR1_AUTH_NONCE_BYTES],
    const HVR1_MAILBOX *mailbox,
    uint8_t tag[HVR1_MAILBOX_TAG_BYTES])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    UCHAR canonical[HVR1_AUTH_NONCE_BYTES + HVR1_MAILBOX_TAG_OFFSET];
    NTSTATUS status;

    if (key == NULL || nonce == NULL || mailbox == NULL || tag == NULL)
        return 0;
    RtlCopyMemory(canonical, nonce, HVR1_AUTH_NONCE_BYTES);
    RtlCopyMemory(canonical + HVR1_AUTH_NONCE_BYTES, mailbox,
                  HVR1_MAILBOX_TAG_OFFSET);
    RtlZeroMemory(tag, HVR1_MAILBOX_TAG_BYTES);
    status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, NULL,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (status >= 0) {
        status = BCryptHash(
            algorithm,
            (PUCHAR)key,
            HVR1_AUTH_KEY_BYTES,
            canonical,
            (ULONG)sizeof(canonical),
            tag,
            HVR1_MAILBOX_TAG_BYTES);
    }
    if (algorithm != NULL)
        (VOID)BCryptCloseAlgorithmProvider(algorithm, 0);
    Hvr1ClearBytes(canonical, sizeof(canonical));
    if (status < 0) {
        Hvr1ClearBytes(tag, HVR1_MAILBOX_TAG_BYTES);
        return 0;
    }
    return 1;
}

int
Hvr1VerifyMailboxAuthTag(
    const uint8_t key[HVR1_AUTH_KEY_BYTES],
    const uint8_t nonce[HVR1_AUTH_NONCE_BYTES],
    const HVR1_MAILBOX *mailbox)
{
    UCHAR expected[HVR1_MAILBOX_TAG_BYTES];
    ULONG index;
    volatile UCHAR difference = 0;
    int computed;

    if (mailbox == NULL)
        return 0;
    computed = Hvr1ComputeMailboxAuthTag(key, nonce, mailbox, expected);
    if (!computed)
        return 0;
    for (index = 0; index < HVR1_MAILBOX_TAG_BYTES; ++index) {
        difference = (UCHAR)(difference |
            (expected[index] ^ mailbox->AuthTag[index]));
    }
    Hvr1ClearBytes(expected, sizeof(expected));
    return difference == 0;
}
