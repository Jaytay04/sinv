/* replay_uefi.h - Freestanding UEFI callbacks for the hybrid replay core. */
#ifndef REPLAY_UEFI_H
#define REPLAY_UEFI_H

#include "Uefi.h"
#include "Tcg2Protocol.h"

#include "replay.h"
#include "efi_file.h"
#include "tcglog_ib.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Event data is copied into an EFI_TCG2_EVENT before it is passed to the
 * protocol.  This cap is deliberately independent from EFI_FILE_MAX_READ_SIZE:
 * an EFI event payload is normally small, while an image supplied to
 * HashLogExtendEvent may be considerably larger.
 */
#define REPLAY_UEFI_MAX_EVENT_DATA (1024u * 1024u)

typedef struct {
    EFI_HANDLE image_handle;
    EFI_TCG2_PROTOCOL *tcg2;
    /* Borrowed, immutable path.  Callers should use a static/fixed path. */
    const CHAR16 *boot_manager_path;

    /* The file buffer is owned by this context between resolve_data and the
     * matching hash_extend callback.  replay_uefi_release() handles an
     * interrupted replay as well. */
    efi_file_buffer_t pending_image;
    UINT32 pending_image_event;

    UINT32 hash_extensions;
    UINT32 raw_extensions;
    EFI_STATUS last_status;
} replay_uefi_context_t;

/*
 * Initialize a context and callback table for replay_run().  The supplied
 * ImageHandle is the loaded loader image, so efi_file_read() resolves the
 * boot-manager path from the loader's ESP.  BootManagerPath is borrowed and
 * must remain valid until replay_uefi_release() has run.
 */
EFI_STATUS replay_uefi_init(replay_uefi_context_t *context,
                            EFI_HANDLE image_handle,
                            EFI_TCG2_PROTOCOL *tcg2,
                            const CHAR16 *boot_manager_path,
                            replay_callbacks_t *callbacks);

/* Release a pending pre-relocation image buffer, if any. */
void replay_uefi_release(replay_uefi_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* REPLAY_UEFI_H */
