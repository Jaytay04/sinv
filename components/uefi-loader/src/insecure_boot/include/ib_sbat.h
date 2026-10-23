/*
 * The `.sbat` section shim 15.3 and later require of everything they launch.
 *
 * Shim refuses a second stage that carries no `.sbat` section, and revokes ones
 * it carries by component name and generation.  The section claims the minimum
 * generations there are -- one for the SBAT mechanism itself and one for this
 * loader -- under a name no revocation list will ever name.
 *
 * Nothing reads this data at run time: the linker places it under the name shim
 * looks up, so it is there for the boot rather than for the program.  See
 * `crates/ib-loader/src/sbat.rs`.
 */
#ifndef IB_SBAT_H
#define IB_SBAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Name the linker and shim use for the section. */
#define IB_SBAT_SECTION ".sbat"

/* The text the section holds.  `sizeof` on the literal includes the one
 * trailing NUL the Rust `TEXT` carries explicitly. */
#define IB_SBAT_TEXT                                                   \
    "sbat,1,SBAT Version,sbat,1,"                                      \
    "https://github.com/rhboot/shim/blob/main/SBAT_SBAT.md\n"          \
    "insecure-boot,1,insecure-boot,insecure-boot,1,https://example.com/\n"

/* The section image.  It has static storage duration and is never modified. */
extern const char ib_sbat_section[];
extern const size_t ib_sbat_section_length;

#ifdef __cplusplus
}
#endif

#endif /* IB_SBAT_H */
