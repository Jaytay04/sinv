#include "user_vmcall_dispatch.h"
#include "efi_hypervisor.h"

/*
 * Minimal freestanding helpers — no libc dependency.
 */
static void HvEfiUserZero(void *buffer, uint64_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    while (size-- != 0) {
        *bytes++ = 0;
    }
}

static void HvEfiUserCopy(void *dst, const void *src, uint64_t size)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (size-- != 0) {
        *d++ = *s++;
    }
}

static uint32_t HvEfiUserLoad32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t HvEfiUserLoad64(const uint8_t *bytes)
{
    return (uint64_t)bytes[0] |
           ((uint64_t)bytes[1] << 8) |
           ((uint64_t)bytes[2] << 16) |
           ((uint64_t)bytes[3] << 24) |
           ((uint64_t)bytes[4] << 32) |
           ((uint64_t)bytes[5] << 40) |
           ((uint64_t)bytes[6] << 48) |
           ((uint64_t)bytes[7] << 56);
}

static void HvEfiUserStore32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void HvEfiUserStore64(uint8_t *bytes, uint64_t value)
{
    for (uint32_t i = 0; i < 8; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8));
    }
}

/*
 * Write a response frame into the mailbox response region.
 */
static void HvEfiUserWriteResponse(
    uint8_t *response_base,
    uint32_t status,
    uint32_t opcode,
    uint64_t sequence,
    const void *payload,
    uint32_t payload_size)
{
    if (payload_size > HV_EFI_USER_MAILBOX_RESPONSE_OFFSET -
                           HV_EFI_USER_RESPONSE_MAGIC) {
        payload_size = HV_EFI_USER_MAILBOX_RESPONSE_OFFSET -
                       HV_EFI_USER_RESPONSE_MAGIC;
    }

    HvEfiUserZero(response_base, HV_EFI_USER_MAILBOX_RESPONSE_OFFSET);

    HvEfiUserStore64(response_base + 0, HV_EFI_USER_RESPONSE_MAGIC);
    HvEfiUserStore32(response_base + 8, status);
    HvEfiUserStore32(response_base + 12, opcode);
    HvEfiUserStore64(response_base + 16, sequence);
    HvEfiUserStore32(response_base + 24, payload_size);

    if (payload != NULL && payload_size > 0) {
        HvEfiUserCopy(response_base + 32, payload, payload_size);
    }
}

int HvEfiUserVmcallInit(uint64_t mailbox_pa)
{
    if (mailbox_pa == 0) {
        return 0;
    }

    uint8_t *mailbox = (uint8_t *)(uintptr_t)mailbox_pa;
    HvEfiUserZero(mailbox, HV_EFI_USER_MAILBOX_PAGE_SIZE);

    return 1;
}

uint32_t HvEfiUserVmcallDispatch(
    uint64_t mailbox_pa,
    uint64_t *sequence)
{
    if (mailbox_pa == 0 || sequence == NULL) {
        return HV_EFI_USER_STATUS_HYPERVISOR_OFFLINE;
    }

    uint8_t *mailbox = (uint8_t *)(uintptr_t)mailbox_pa;
    uint8_t *request_base = mailbox + HV_EFI_USER_MAILBOX_REQUEST_OFFSET;
    uint8_t *response_base = mailbox + HV_EFI_USER_MAILBOX_RESPONSE_OFFSET;

    /* Validate request magic */
    uint64_t req_magic = HvEfiUserLoad64(request_base);
    if (req_magic != HV_EFI_USER_REQUEST_MAGIC) {
        HvEfiUserWriteResponse(response_base,
                               HV_EFI_USER_STATUS_MALFORMED_FRAME,
                               0, *sequence, NULL, 0);
        return HV_EFI_USER_STATUS_MALFORMED_FRAME;
    }

    /* Parse request header */
    uint32_t opcode = HvEfiUserLoad32(request_base + 8);
    uint32_t flags = HvEfiUserLoad32(request_base + 12);
    uint64_t req_sequence = HvEfiUserLoad64(request_base + 16);
    uint32_t payload_size = HvEfiUserLoad32(request_base + 24);

    (void)flags; /* Reserved for future use */

    /* Validate sequence (must be >= current sequence) */
    if (req_sequence < *sequence) {
        HvEfiUserWriteResponse(response_base,
                               HV_EFI_USER_STATUS_INVALID_SEQUENCE,
                               opcode, req_sequence, NULL, 0);
        return HV_EFI_USER_STATUS_INVALID_SEQUENCE;
    }

    /* Dispatch opcode */
    uint32_t status = HV_EFI_USER_STATUS_OK;
    uint8_t response_payload[64];
    uint32_t response_payload_size = 0;

    switch (opcode) {
    case HV_EFI_USER_OPCODE_STATUS:
        /* Return hypervisor status */
        HvEfiUserStore32(response_payload + 0, HV_EFI_USER_STATUS_OK);
        HvEfiUserStore32(response_payload + 4, 1); /* hypervisor active */
        response_payload_size = 8;
        break;

    case HV_EFI_USER_OPCODE_HEALTH:
        /* Return health check data */
        HvEfiUserStore32(response_payload + 0, HV_EFI_USER_STATUS_OK);
        HvEfiUserStore64(response_payload + 4, mailbox_pa);
        response_payload_size = 12;
        break;

    default:
        status = HV_EFI_USER_STATUS_INVALID_OPCODE;
        break;
    }

    if (status == HV_EFI_USER_STATUS_OK) {
        *sequence = req_sequence + 1;
    }

    HvEfiUserWriteResponse(response_base, status, opcode,
                           req_sequence,
                           response_payload_size > 0 ? response_payload : NULL,
                           response_payload_size);

    return status;
}
