/* See ib_sbat.h. */
#include "ib_sbat.h"

#if defined(_MSC_VER)

#pragma section(".sbat", read)
__declspec(allocate(".sbat"))
const char ib_sbat_section[] = IB_SBAT_TEXT;

#else

__attribute__((section(IB_SBAT_SECTION), used))
const char ib_sbat_section[] = IB_SBAT_TEXT;

#endif

/* `sizeof` the literal: the Rust `TEXT` ends with an explicit NUL, and the C
 * literal's implicit terminator is that same byte. */
const size_t ib_sbat_section_length = sizeof(ib_sbat_section);
