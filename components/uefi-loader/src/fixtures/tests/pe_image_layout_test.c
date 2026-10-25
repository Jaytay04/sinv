/*
 * Enforces the PE section rule shim's loader applies before it will start a
 * second-stage image (shim/pe.c, handle_image):
 *
 *   a section that is not marked IMAGE_SCN_CNT_UNINITIALIZED_DATA must keep
 *   both its VirtualAddress and its PointerToRawData at or past SizeOfHeaders,
 *   otherwise shim refuses the image with "Section N is inside image headers"
 *   and returns EFI_UNSUPPORTED.
 *
 * MSVC leaves a zero-length `.data` behind when nothing in the image needs
 * initialised writable data, with PointerToRawData 0, and marks it as
 * initialised data.  That image boots on firmware that loads it directly and
 * never loads at all behind shim, which is why this check runs against the
 * built image instead of the sources.
 *
 * Usage: pe_image_layout_test <image.efi> [...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PE_SCN_CNT_UNINITIALIZED_DATA 0x00000080u

static unsigned u16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned u32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int check_image(const char *path)
{
    FILE *file = fopen(path, "rb");
    unsigned char header[0x40];
    unsigned char *image = NULL;
    long size;
    unsigned pe_offset;
    unsigned sections;
    unsigned optional_size;
    unsigned magic;
    unsigned headers_size;
    unsigned section_table;
    unsigned index;
    int failures = 0;

    if (file == NULL) {
        printf("%s: cannot open\n", path);
        return 1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 1;
    }
    size = ftell(file);
    if (size < 0x100 || fseek(file, 0, SEEK_SET) != 0) {
        printf("%s: too small\n", path);
        fclose(file);
        return 1;
    }
    image = malloc((size_t)size);
    if (image == NULL || fread(image, 1, (size_t)size, file) != (size_t)size) {
        printf("%s: cannot read\n", path);
        free(image);
        fclose(file);
        return 1;
    }
    fclose(file);

    if (size < 0x40 || image[0] != 'M' || image[1] != 'Z') {
        printf("%s: not a DOS image\n", path);
        free(image);
        return 1;
    }
    memcpy(header, image, sizeof(header));
    pe_offset = u32(header + 0x3c);
    if (pe_offset + 24u > (unsigned)size || memcmp(image + pe_offset, "PE\0\0", 4) != 0) {
        printf("%s: no PE header at 0x%x\n", path, pe_offset);
        free(image);
        return 1;
    }
    sections = u16(image + pe_offset + 6);
    optional_size = u16(image + pe_offset + 20);
    if (optional_size < 64 || pe_offset + 24u + optional_size > (unsigned)size) {
        printf("%s: optional header does not fit\n", path);
        free(image);
        return 1;
    }
    magic = u16(image + pe_offset + 24);
    if (magic != 0x20b) {
        printf("%s: PE32+ optional header expected, found 0x%x\n", path, magic);
        free(image);
        return 1;
    }
    headers_size = u32(image + pe_offset + 24 + 60);
    section_table = pe_offset + 24u + optional_size;
    if (sections == 0 || section_table + sections * 40u > (unsigned)size) {
        printf("%s: section table does not fit\n", path);
        free(image);
        return 1;
    }
    if (section_table + sections * 40u > headers_size) {
        printf("%s: SizeOfHeaders 0x%x does not cover the section table\n", path, headers_size);
        failures++;
    }

    for (index = 0; index < sections; index++) {
        const unsigned char *section = image + section_table + index * 40u;
        char name[9];
        unsigned virtual_address = u32(section + 12);
        unsigned raw_size = u32(section + 16);
        unsigned raw_pointer = u32(section + 20);
        unsigned characteristics = u32(section + 36);
        int bss = (characteristics & PE_SCN_CNT_UNINITIALIZED_DATA) != 0;

        memcpy(name, section, 8);
        name[8] = '\0';
        if (!bss &&
            (virtual_address < headers_size || raw_pointer < headers_size)) {
            printf("%s: section %u (%s) is inside the image headers "
                   "(VirtualAddress 0x%x, PointerToRawData 0x%x, SizeOfHeaders 0x%x)\n",
                   path, index, name, virtual_address, raw_pointer, headers_size);
            failures++;
        }
        if (raw_size != 0 && (raw_pointer > (unsigned)size || raw_size > (unsigned)size - raw_pointer)) {
            printf("%s: section %u (%s) raw data runs past the end of the file\n",
                   path, index, name);
            failures++;
        }
    }

    free(image);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    int index;
    int failures = 0;

    if (argc < 2) {
        printf("pe_image_layout_test: an image path is required\n");
        return 2;
    }
    for (index = 1; index < argc; index++) {
        failures += check_image(argv[index]);
    }
    if (failures != 0) {
        printf("pe_image_layout_test: FAIL\n");
        return 1;
    }
    printf("pe_image_layout_test: PASS\n");
    return 0;
}
