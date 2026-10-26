/* Console chunking: a newline at the end of a full chunk must not run past the
 * terminator slot. The writer keeps 240 UTF-16 units including the terminator,
 * so a CR+LF pair needs two free slots before it is written. */
#include <stdio.h>
#include <string.h>

#include "ib_console.h"
#include "ib_uefi.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static ib_efi_system_table fixture_table;
static ib_efi_simple_text_output_protocol fixture_out;

static unsigned fixture_chars;
static unsigned fixture_flushes;
static int fixture_unterminated;
static ib_char16 fixture_capture[1024];

static ib_efi_status IB_EFIAPI fixture_output_string(
    ib_efi_simple_text_output_protocol *protocol, ib_char16 *text) {
    unsigned length = 0;

    (void)protocol;
    /* The writer's buffer is 240 units; anything without a terminator inside
     * that window means it wrote past the end of its own array. */
    while (length < 240u && text[length] != 0) {
        length++;
    }
    if (length >= 240u) {
        fixture_unterminated = 1;
        return IB_EFI_DEVICE_ERROR;
    }
    /* Every flush must fit the writer's own chunk, terminator included. */
    if (length > 239u) {
        fixture_unterminated = 1;
        return IB_EFI_DEVICE_ERROR;
    }
    if (fixture_chars + length <= 1024u) {
        memcpy(&fixture_capture[fixture_chars], text,
               length * sizeof(ib_char16));
    }
    fixture_chars += length;
    fixture_flushes++;
    return IB_EFI_SUCCESS;
}

ib_efi_system_table *ib_efi_get_system_table(void) {
    return &fixture_table;
}

void ib_console_write(const char *text);   /* the unit under test */

static void fixture_reset(void) {
    memset(&fixture_table, 0, sizeof(fixture_table));
    fixture_out.output_string = fixture_output_string;
    fixture_table.con_out = &fixture_out;
    fixture_chars = 0;
    fixture_flushes = 0;
    fixture_unterminated = 0;
    memset(fixture_capture, 0, sizeof(fixture_capture));
}

static int test_newline_at_chunk_boundary(void) {
    char line[260];

    fixture_reset();
    memset(line, 'a', sizeof(line) - 1);
    /* 238 characters, then the newline that has to expand to CR+LF. */
    line[238] = '\n';
    memcpy(line + 239, "tail", 4);
    line[243] = '\0';

    ib_console_write(line);

    CHECK(fixture_unterminated == 0);
    CHECK(fixture_chars == 238u + 2u + 4u);
    CHECK(fixture_flushes >= 2u);
    /* Exact sequence: 238 'a', CR, LF, then "tail". */
    {
        unsigned index;
        for (index = 0; index < 238u; index++) {
            CHECK(fixture_capture[index] == 'a');
        }
        CHECK(fixture_capture[238] == '\r');
        CHECK(fixture_capture[239] == '\n');
        CHECK(fixture_capture[240] == 't');
        CHECK(fixture_capture[243] == 'l');
    }
    return 0;
}

static int test_long_line_without_newline(void) {
    char line[300];

    fixture_reset();
    memset(line, 'b', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    ib_console_write(line);

    CHECK(fixture_unterminated == 0);
    CHECK(fixture_chars == sizeof(line) - 1);
    {
        unsigned index;
        for (index = 0; index < fixture_chars; index++) {
            CHECK(fixture_capture[index] == 'b');
        }
    }
    return 0;
}

int main(void) {
    if (test_newline_at_chunk_boundary() != 0) {
        return 1;
    }
    if (test_long_line_without_newline() != 0) {
        return 1;
    }
    puts("console_write_test: PASS");
    return 0;
}
