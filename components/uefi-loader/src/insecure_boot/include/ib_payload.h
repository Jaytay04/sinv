/*
 * Mapping and running the payload image by hand.
 *
 * The payload is unsigned, so the firmware's `LoadImage` would refuse it under
 * Secure Boot.  The loader maps it itself: parse the PE headers, allocate
 * `SizeOfImage` bytes, copy the headers and sections in, apply the base
 * relocations, then call the entry point the way the firmware called this
 * loader.  See `crates/ib-loader/src/payload.rs`.
 */
#ifndef IB_PAYLOAD_H
#define IB_PAYLOAD_H

#include <stddef.h>

#include "ib_loader_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maps `pe` into memory, runs its entry point, and frees it again.  The status
 * the payload returns is reported; the run is over once control comes back.
 *
 * # Errors
 *
 * Fails if `pe` is not a PE32+ image this can map, if the firmware refuses the
 * allocation, or if the image's relocations point outside it. */
ib_loader_code ib_payload_run(const unsigned char *pe, size_t pe_len,
                              ib_loader_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* IB_PAYLOAD_H */
