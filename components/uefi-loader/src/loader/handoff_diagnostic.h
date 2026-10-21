/* Bounded, optional handoff progress records appended to schema 1 status. */
#ifndef HANDOFF_DIAGNOSTIC_H
#define HANDOFF_DIAGNOSTIC_H

#include "Uefi.h"

#define HANDOFF_DIAGNOSTIC_MAX_SIZE 4096u

/* Appends complete ASCII records with LF endings, without a trailing NUL.
 * Failure leaves both Buffer and Used unchanged. ShimPhase is optional. */
int handoff_diagnostic_append(UINT8 *Buffer, UINTN Capacity, UINTN *Used,
    const char *Target, const char *Phase, BOOLEAN Completed, EFI_STATUS Status,
    const char *ShimPhase, EFI_STATUS ShimStatus);

#endif
