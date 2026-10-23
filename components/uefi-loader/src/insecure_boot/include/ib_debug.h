/*
 * Reproductions of the Rust `Debug`/`Display` renderings the loader prints.
 *
 * The Rust original prints several values with `{:?}`: `uefi::Status`,
 * `uefi::Guid`, `uefi::runtime::VariableVendor`, and the `bitflags`
 * `VariableAttributes`.  Their formats come from the pinned dependencies
 * (`uefi-raw 0.16.0`'s `newtype_enum!`, `uguid`, and `bitflags 2.13.1`), so this
 * header only carries the three renderings the loader actually names.
 *
 * Everything here is pure: no firmware is touched.
 */
#ifndef IB_DEBUG_H
#define IB_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "ib_uefi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `uefi::Status`: the variant name when the value is a known status, otherwise
 * `Status(<decimal>)`.  `Display` delegates to `Debug`, so both spellings are
 * the same. */
void ib_status_debug(ib_efi_status status, char *buffer, size_t capacity);

/* `uefi::Guid` (`uguid::Guid`): the derived `Debug` form, since the type only
 * hand-writes `Display`:
 *   `Guid { time_low: <dec>, time_mid: [<b4>, <b5>], time_high_and_version:
 *   [<b6>, <b7>], clock_seq_high_and_reserved: <b8>, clock_seq_low: <b9>,
 *   node: [<b10>, ..., <b15>] }`
 * with the bytes in the GUID's little-endian field order.  NOT the dashed
 * string. */
void ib_guid_debug(const ib_efi_guid_t *guid, char *buffer, size_t capacity);

/* `uefi::runtime::VariableVendor`: `VariableVendor(<guid debug>)`, the inner
 * GUID rendered the way `ib_guid_debug` renders it. */
void ib_variable_vendor_debug(const ib_efi_guid_t *guid, char *buffer,
                              size_t capacity);

/* `VariableAttributes` as `bitflags 2.13.1` renders it:
 * `VariableAttributes(NON_VOLATILE | RUNTIME_ACCESS)`, `VariableAttributes(0x0)`
 * when empty, and any unknown bits appended as `0x<hex>`. */
void ib_variable_attributes_debug(uint32_t attributes, char *buffer,
                                  size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* IB_DEBUG_H */
