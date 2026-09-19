/*
 * replay_uefi.c - UEFI adapter for the portable hybrid replay core.
 *
 * The adapter deliberately keeps event classification and digest selection in
 * replay.c.  It supplies only the firmware side effects: exact event-data
 * HashLogExtendEvent calls for reproducible events and TPM2_PCR_Extend calls
 * for raw events.  It has no libc or host/VM dependency.
 */
#include "replay_uefi.h"

#include <stddef.h>

#include "tcg2.h"
#include "sha256.h"

static void copy_bytes(UINT8 *destination, const UINT8 *source, UINTN size)
{
    UINTN i;

    for (i = 0; i < size; i++) {
        destination[i] = source[i];
    }
}

static UINTN string_length(const CHAR16 *value)
{
    UINTN length = 0;

    if (value == NULL) {
        return 0;
    }
    while (value[length] != 0) {
        if (length >= 32767u) {
            return 0;
        }
        length++;
    }
    return length;
}

static int event_type_uses_recorded_data(UINT32 event_type)
{
    switch (event_type) {
    case TCGLOG_EVT_EFI_VARIABLE_DRIVER_CONFIG:
    case TCGLOG_EVT_EFI_VARIABLE_BOOT:
    case TCGLOG_EVT_EFI_VARIABLE_BOOT2:
    case TCGLOG_EVT_EFI_GPT_EVENT:
    case TCGLOG_EVT_EFI_ACTION:
        return 1;
    default:
        return 0;
    }
}

static int event_type_uses_boot_manager(UINT32 event_type)
{
    return event_type == TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION;
}

static int event_type_supported(UINT32 event_type)
{
    return event_type_uses_recorded_data(event_type) ||
           event_type_uses_boot_manager(event_type);
}

static EFI_STATUS image_path_status_to_efi(
    tcglog_ib_image_path_status_t status)
{
    switch (status) {
    case TCGLOG_IB_IMAGE_PATH_ERR_ARGUMENT:
    case TCGLOG_IB_IMAGE_PATH_ERR_NOT_IMAGE:
        return EFI_INVALID_PARAMETER;
    case TCGLOG_IB_IMAGE_PATH_ERR_TRAVERSAL:
        return EFI_SECURITY_VIOLATION;
    case TCGLOG_IB_IMAGE_PATH_ERR_TOO_LONG:
        return EFI_BAD_BUFFER_SIZE;
    case TCGLOG_IB_IMAGE_PATH_ERR_TRUNCATED:
    case TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED:
    case TCGLOG_IB_IMAGE_PATH_ERR_NO_FILEPATH:
    default:
        return EFI_VOLUME_CORRUPTED;
    }
}

static int bytes_equal(const UINT8 *left, const UINT8 *right, UINTN size)
{
    UINTN i;
    UINT8 difference = 0;

    for (i = 0; i < size; i++) {
        difference |= (UINT8)(left[i] ^ right[i]);
    }
    return difference == 0u;
}

static int recorded_sha256_matches(const tcglog_event_t *event,
                                   const UINT8 *data, size_t data_size)
{
    const UINT8 *recorded;
    UINT8 calculated[SHA256_DIGEST_SIZE];
    size_t recorded_size = 0;

    recorded = tcglog_event_digest(
        event, TCGLOG_ALG_SHA256, &recorded_size);
    return recorded != NULL && recorded_size == SHA256_DIGEST_SIZE &&
           sha256_hash(data, data_size, calculated) == SHA256_OK &&
           bytes_equal(recorded, calculated, SHA256_DIGEST_SIZE);
}

static replay_status_t fail_status(replay_uefi_context_t *context,
                                    EFI_STATUS status)
{
    if (context != NULL) {
        context->last_status = status;
    }
    return REPLAY_ERR_CALLBACK;
}

static void release_pending_image(replay_uefi_context_t *context)
{
    if (context == NULL) {
        return;
    }
    if (context->pending_image.bytes != NULL) {
        efi_file_release(&context->pending_image);
    }
    context->pending_image.bytes = NULL;
    context->pending_image.size = 0u;
    context->pending_image_event = 0u;
}

static replay_status_t resolve_data(const tcglog_event_t *event,
                                    replay_data_t *out, void *opaque)
{
    replay_uefi_context_t *context = (replay_uefi_context_t *)opaque;
    const uint16_t *recorded_path = NULL;
    size_t recorded_path_chars = 0;
    const CHAR16 *source_path;
    EFI_STATUS status;
    tcglog_ib_image_path_status_t path_status;

    if (context == NULL) {
        return REPLAY_ERR_CALLBACK;
    }
    release_pending_image(context);
    if (event == NULL || out == NULL ||
        !event_type_supported(event->event_type) || event->pcr_index > 23u) {
        return fail_status(context, EFI_INVALID_PARAMETER);
    }
    out->bytes = NULL;
    out->size = 0;

    if (event_type_uses_recorded_data(event->event_type)) {
        if (event->event_data == NULL || event->event_size == 0u ||
            event->event_size > REPLAY_UEFI_MAX_EVENT_DATA) {
            return fail_status(context, EFI_BAD_BUFFER_SIZE);
        }
        out->bytes = event->event_data;
        out->size = (size_t)event->event_size;
        context->last_status = EFI_SUCCESS;
        return REPLAY_OK;
    }

    path_status = tcglog_ib_extract_image_path(event, &recorded_path,
                                               &recorded_path_chars);
    if (path_status != TCGLOG_IB_IMAGE_PATH_OK) {
        return fail_status(context, image_path_status_to_efi(path_status));
    }
    /* Only the canonical boot-manager event is redirected to the configured
     * one-shot source (.bak when available, otherwise the current target).
     * Every other image event must resolve the exact path recorded in its
     * EFI_IMAGE_LOAD_EVENT, so an unrelated BSA cannot hash bootmgfw bytes. */
    source_path = tcglog_ib_image_path_is_boot_manager(
        recorded_path, recorded_path_chars) ? context->boot_manager_path :
        (const CHAR16 *)(const void *)recorded_path;
    if (source_path == NULL || string_length(source_path) == 0u) {
        return fail_status(context, EFI_INVALID_PARAMETER);
    }
    status = efi_file_read(context->image_handle, source_path,
                           &context->pending_image);
    if (EFI_ERROR(status)) {
        return fail_status(context, status);
    }
    if (context->pending_image.bytes == NULL ||
        context->pending_image.size == 0u ||
        context->pending_image.size > (UINTN)EFI_FILE_MAX_READ_SIZE) {
        release_pending_image(context);
        return fail_status(context, EFI_BAD_BUFFER_SIZE);
    }
    context->pending_image_event = event->event_type;
    out->bytes = context->pending_image.bytes;
    out->size = (size_t)context->pending_image.size;
    context->last_status = EFI_SUCCESS;
    return REPLAY_OK;
}

static replay_status_t hash_extend(const tcglog_event_t *event,
                                   const UINT8 *data, size_t data_size,
                                   void *opaque)
{
    replay_uefi_context_t *context = (replay_uefi_context_t *)opaque;
    EFI_TCG2_EVENT *efi_event = NULL;
    UINTN allocation_size;
    UINT64 flags = 0;
    EFI_STATUS status;
    int image_event;

    if (context == NULL || event == NULL || data == NULL || data_size == 0u ||
        data_size > (size_t)(~(UINTN)0) || event->pcr_index > 23u ||
        !event_type_supported(event->event_type)) {
        release_pending_image(context);
        return fail_status(context, EFI_INVALID_PARAMETER);
    }

    image_event = event_type_uses_boot_manager(event->event_type);
    if (image_event) {
        const uint16_t *recorded_path = NULL;
        size_t recorded_path_chars = 0;
        tcglog_ib_image_path_status_t path_status;

        path_status = tcglog_ib_extract_image_path(event, &recorded_path,
                                                   &recorded_path_chars);
        if (path_status != TCGLOG_IB_IMAGE_PATH_OK) {
            release_pending_image(context);
            return fail_status(context, image_path_status_to_efi(path_status));
        }
        if (context->pending_image.bytes == NULL ||
            context->pending_image_event != event->event_type ||
            data != context->pending_image.bytes ||
            data_size != (size_t)context->pending_image.size) {
            release_pending_image(context);
            return fail_status(context, EFI_INVALID_PARAMETER);
        }
        flags = EFI_TCG2_PE_COFF_IMAGE;
    } else if (context->pending_image.bytes != NULL) {
        /* A direct recorded-data event must never inherit a prior image. */
        release_pending_image(context);
        return fail_status(context, EFI_INVALID_PARAMETER);
    } else if (data != event->event_data ||
               data_size != (size_t)event->event_size) {
        return fail_status(context, EFI_INVALID_PARAMETER);
    }
    if (!image_event && !recorded_sha256_matches(event, data, data_size)) {
        return fail_status(context, EFI_SECURITY_VIOLATION);
    }

    if (event->event_data == NULL || event->event_size == 0u ||
        event->event_size > REPLAY_UEFI_MAX_EVENT_DATA ||
        (size_t)event->event_size > (size_t)(~(UINTN)0) -
            offsetof(EFI_TCG2_EVENT, Event)) {
        release_pending_image(context);
        return fail_status(context, EFI_BAD_BUFFER_SIZE);
    }
    allocation_size = offsetof(EFI_TCG2_EVENT, Event) +
                      (UINTN)event->event_size;
    if (allocation_size > (UINTN)0xffffffffu || gBS == NULL ||
        gBS->AllocatePool == NULL || context->tcg2 == NULL ||
        context->tcg2->HashLogExtendEvent == NULL ||
        context->hash_extensions == 0xffffffffu) {
        release_pending_image(context);
        return fail_status(context, context->hash_extensions == 0xffffffffu ?
                           EFI_DEVICE_ERROR : EFI_OUT_OF_RESOURCES);
    }

    status = gBS->AllocatePool(EfiLoaderData, allocation_size,
                               (VOID **)&efi_event);
    if (EFI_ERROR(status) || efi_event == NULL) {
        release_pending_image(context);
        return fail_status(context, EFI_ERROR(status) ? status :
                           EFI_OUT_OF_RESOURCES);
    }
    efi_event->Size = (UINT32)allocation_size;
    efi_event->Header.HeaderSize = (UINT32)sizeof(EFI_TCG2_EVENT_HEADER);
    efi_event->Header.HeaderVersion = 1u;
    efi_event->Header.PCRIndex = event->pcr_index;
    efi_event->Header.EventType = event->event_type;
    copy_bytes(efi_event->Event, event->event_data, event->event_size);

    status = context->tcg2->HashLogExtendEvent(
        context->tcg2, flags, (EFI_PHYSICAL_ADDRESS)(UINTN)data,
        (UINT64)data_size, efi_event);
    gBS->FreePool(efi_event);
    efi_event = NULL;
    release_pending_image(context);
    if (EFI_ERROR(status)) {
        return fail_status(context, status);
    }
    context->hash_extensions++;
    context->last_status = EFI_SUCCESS;
    return REPLAY_OK;
}

static replay_status_t raw_extend(const tcglog_event_t *event,
                                  const replay_digest_t *digests,
                                  uint32_t digest_count, void *opaque)
{
    replay_uefi_context_t *context = (replay_uefi_context_t *)opaque;
    tcg2_digest_value_t values[REPLAY_MAX_DIGESTS];
    uint32_t i;
    EFI_STATUS status;

    if (context == NULL) {
        return REPLAY_ERR_CALLBACK;
    }
    release_pending_image(context);
    if (event == NULL || digests == NULL ||
        digest_count == 0u || digest_count > REPLAY_MAX_DIGESTS ||
        event->pcr_index > 23u || context->tcg2 == NULL) {
        return fail_status(context, EFI_INVALID_PARAMETER);
    }
    for (i = 0; i < digest_count; i++) {
        if (digests[i].bytes == NULL || digests[i].size == 0u ||
            digests[i].size > 0xffffu) {
            return fail_status(context, EFI_BAD_BUFFER_SIZE);
        }
        values[i].algorithm = digests[i].alg;
        values[i].bytes = digests[i].bytes;
        values[i].size = (UINT16)digests[i].size;
    }
    if (context->raw_extensions == 0xffffffffu) {
        return fail_status(context, EFI_DEVICE_ERROR);
    }
    status = tcg2_pcr_extend(context->tcg2, event->pcr_index, values,
                             digest_count);
    if (EFI_ERROR(status)) {
        return fail_status(context, status);
    }
    context->raw_extensions++;
    context->last_status = EFI_SUCCESS;
    return REPLAY_OK;
}

EFI_STATUS replay_uefi_init(replay_uefi_context_t *context,
                            EFI_HANDLE image_handle,
                            EFI_TCG2_PROTOCOL *tcg2,
                            const CHAR16 *boot_manager_path,
                            replay_callbacks_t *callbacks)
{
    if (context == NULL || callbacks == NULL || image_handle == NULL ||
        tcg2 == NULL || tcg2->HashLogExtendEvent == NULL ||
        tcg2->SubmitCommand == NULL || boot_manager_path == NULL ||
        string_length(boot_manager_path) == 0u || gBS == NULL ||
        gBS->AllocatePool == NULL || gBS->FreePool == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    context->image_handle = image_handle;
    context->tcg2 = tcg2;
    context->boot_manager_path = boot_manager_path;
    context->pending_image.bytes = NULL;
    context->pending_image.size = 0;
    context->pending_image_event = 0u;
    context->hash_extensions = 0u;
    context->raw_extensions = 0u;
    context->last_status = EFI_SUCCESS;

    callbacks->resolve_data = resolve_data;
    callbacks->hash_extend = hash_extend;
    callbacks->raw_extend = raw_extend;
    callbacks->context = context;
    return EFI_SUCCESS;
}

void replay_uefi_release(replay_uefi_context_t *context)
{
    release_pending_image(context);
}
