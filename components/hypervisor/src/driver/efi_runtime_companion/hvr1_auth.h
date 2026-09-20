#ifndef HYPERSINV_HVR1_AUTH_H
#define HYPERSINV_HVR1_AUTH_H

#include "hvr1_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Computes HMAC-SHA-256(key, nonce || mailbox[0..AuthTag) || payload).
 * The caller must provide a structurally bounded canonical mailbox. */
int Hvr1ComputeMailboxAuthTag(
    const uint8_t key[HVR1_AUTH_KEY_BYTES],
    const uint8_t nonce[HVR1_AUTH_NONCE_BYTES],
    const HVR1_MAILBOX *mailbox,
    uint8_t tag[HVR1_MAILBOX_TAG_BYTES]);

/* Constant-time comparison against mailbox->AuthTag. */
int Hvr1VerifyMailboxAuthTag(
    const uint8_t key[HVR1_AUTH_KEY_BYTES],
    const uint8_t nonce[HVR1_AUTH_NONCE_BYTES],
    const HVR1_MAILBOX *mailbox);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_HVR1_AUTH_H */
