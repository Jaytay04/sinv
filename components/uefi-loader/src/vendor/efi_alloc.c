/*
 * efi_alloc.c — tcglog UEFI allocator hooks shared by all EFI targets.
 *
 * The shared tcglog library (and the tcglog.ib container) allocate via the
 * TCGLOG_ALLOC/REALLOC/FREE macros, which map here when TCGLOG_UEFI is defined.
 * Implemented over EFI Boot Services AllocatePool/FreePool so the same
 * src/common sources link into every freestanding EFI image (smoke app and
 * loader.efi) without libc.
 *
 * Each allocation carries a small header recording its size, so realloc can
 * copy exactly min(old_size, new_size) bytes — the C standard's realloc
 * contract — instead of overreading a grown block. These hooks consume gST/gBS,
 * which the owning entry point populates before any allocation happens.
 */
#include <stddef.h>

#include "Uefi.h"

/* Header stored immediately before each returned block. */
typedef struct {
    size_t size;
} efi_alloc_hdr_t;

/* ------------------------------------------------------------------------- */
/* tcglog UEFI allocator hooks                                                */
/* ------------------------------------------------------------------------- */
void *tcglog_efi_alloc(size_t size)
{
    efi_alloc_hdr_t *h = NULL;
    if (gBS == NULL ||
        EFI_ERROR(gBS->AllocatePool(EfiLoaderData, size + sizeof(*h),
                                    (VOID **)&h))) {
        return NULL;
    }
    h->size = size;
    return (void *)(h + 1);
}

void *tcglog_efi_realloc(void *ptr, size_t size)
{
    efi_alloc_hdr_t *old;
    unsigned char *dst;
    const unsigned char *src;
    size_t copy;
    size_t i;

    if (gBS == NULL) {
        return NULL;
    }
    if (ptr == NULL) {
        return tcglog_efi_alloc(size);
    }

    old = ((efi_alloc_hdr_t *)ptr) - 1;
    dst = (unsigned char *)tcglog_efi_alloc(size);
    if (dst == NULL) {
        return NULL;
    }

    /* Copy the min of the old and new sizes (realloc contract). */
    copy = old->size < size ? old->size : size;
    src = (const unsigned char *)ptr;
    for (i = 0; i < copy; i++) {
        dst[i] = src[i];
    }
    gBS->FreePool(old);
    return dst;
}

void tcglog_efi_free(void *ptr)
{
    if (ptr != NULL && gBS != NULL) {
        gBS->FreePool(((efi_alloc_hdr_t *)ptr) - 1);
    }
}
