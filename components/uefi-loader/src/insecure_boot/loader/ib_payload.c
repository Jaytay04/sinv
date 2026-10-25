/* See ib_payload.h. */
#include "ib_payload.h"

#include "ib_boot.h"
#include "ib_console.h"
#include "ib_debug.h"
#include "ib_uefi.h"
#include "ib_util.h"

/* Signature a DOS header starts with. */
#define IB_PAYLOAD_DOS_MAGIC0 'M'
#define IB_PAYLOAD_DOS_MAGIC1 'Z'

#define IB_PAYLOAD_PE_OFFSET_AT      0x3cu
#define IB_PAYLOAD_COFF_HEADER_LEN   20u
#define IB_PAYLOAD_SECTION_COUNT_AT  2u
#define IB_PAYLOAD_OPTIONAL_LEN_AT   16u
#define IB_PAYLOAD_OPTIONAL_MAGIC_64 0x020bu
#define IB_PAYLOAD_ENTRY_AT          16u
#define IB_PAYLOAD_IMAGE_BASE_AT     24u
#define IB_PAYLOAD_SIZE_OF_IMAGE_AT  56u
#define IB_PAYLOAD_SIZE_OF_HEADERS_AT 60u
#define IB_PAYLOAD_RVA_COUNT_AT      108u
#define IB_PAYLOAD_DIRECTORY_LEN     8u
#define IB_PAYLOAD_RELOCATION_INDEX  5u
#define IB_PAYLOAD_SECTION_HEADER_LEN 40u
#define IB_PAYLOAD_VIRTUAL_SIZE_AT   8u
#define IB_PAYLOAD_VIRTUAL_ADDRESS_AT 12u
#define IB_PAYLOAD_RAW_SIZE_AT       16u
#define IB_PAYLOAD_RAW_OFFSET_AT     20u
#define IB_PAYLOAD_RELOCATION_HEADER_LEN 8u
#define IB_PAYLOAD_RELOCATION_DIR64  10u
#define IB_PAYLOAD_RELOCATION_ABSOLUTE 0u

typedef struct {
    size_t raw;
    size_t raw_len;
    size_t mapped;
    size_t mapped_len;
} ib_payload_section_t;

typedef struct {
    size_t entry;
    ib_u64 base;
    size_t size;
    size_t headers;
    ib_payload_section_t *sections;
    size_t section_count;
    size_t reloc_start;
    size_t reloc_end;
    int has_relocations;
    size_t pages;
} ib_payload_image_t;

typedef ib_efi_status(IB_EFIAPI *ib_payload_entry_fn)(ib_efi_handle handle,
                                                      void *system_table);

static ib_loader_code ib_payload_malformed(ib_loader_error_t *error) {
    ib_loader_error_str(error, IB_LOADER_ERR_MALFORMED_PAYLOAD, NULL);
    return IB_LOADER_ERR_MALFORMED_PAYLOAD;
}

/* The `len` bytes of `image` at `at`, which have to be inside it. */
static ib_loader_code ib_payload_bytes(const ib_u8 *image, size_t image_len,
                                       size_t at, size_t len,
                                       const ib_u8 **out) {
    size_t end;

    if (at > (size_t)-1 - len) {
        return IB_LOADER_ERR_MALFORMED_PAYLOAD;
    }
    end = at + len;
    if (end > image_len) {
        return IB_LOADER_ERR_MALFORMED_PAYLOAD;
    }
    *out = image + at;
    return IB_LOADER_OK;
}

static ib_loader_code ib_payload_u16(const ib_u8 *image, size_t image_len,
                                     size_t at, ib_u16 *out) {
    const ib_u8 *raw;
    ib_loader_code code = ib_payload_bytes(image, image_len, at, 2, &raw);

    if (code != IB_LOADER_OK) {
        return code;
    }
    *out = (ib_u16)((ib_u16)raw[0] | ((ib_u16)raw[1] << 8));
    return IB_LOADER_OK;
}

static ib_loader_code ib_payload_u32(const ib_u8 *image, size_t image_len,
                                     size_t at, ib_u32 *out) {
    const ib_u8 *raw;
    ib_loader_code code = ib_payload_bytes(image, image_len, at, 4, &raw);

    if (code != IB_LOADER_OK) {
        return code;
    }
    *out = (ib_u32)raw[0] | ((ib_u32)raw[1] << 8) | ((ib_u32)raw[2] << 16) |
           ((ib_u32)raw[3] << 24);
    return IB_LOADER_OK;
}

static ib_loader_code ib_payload_u64(const ib_u8 *image, size_t image_len,
                                     size_t at, ib_u64 *out) {
    ib_u32 low;
    ib_u32 high;
    ib_loader_code code = ib_payload_u32(image, image_len, at, &low);

    if (code != IB_LOADER_OK) {
        return code;
    }
    code = ib_payload_u32(image, image_len, at + 4, &high);
    if (code != IB_LOADER_OK) {
        return code;
    }
    *out = (ib_u64)low | ((ib_u64)high << 32);
    return IB_LOADER_OK;
}

/* Parses a PE32+ image's headers into everything mapping it needs. */
static ib_loader_code ib_payload_parse(const ib_u8 *pe, size_t pe_len,
                                       ib_payload_image_t *image,
                                       ib_loader_error_t *error) {
    const ib_u8 *magic;
    ib_u32 value32;
    ib_u16 optional_magic;
    ib_u16 section_count;
    ib_u16 optional_len;
    size_t signature;
    size_t coff;
    size_t optional;
    size_t directories;
    size_t table;
    size_t index;
    ib_loader_code code;

    image->sections = NULL;
    image->section_count = 0;
    image->has_relocations = 0;
    image->reloc_start = 0;
    image->reloc_end = 0;

    code = ib_payload_bytes(pe, pe_len, 0, 2, &magic);
    if (code != IB_LOADER_OK || magic[0] != IB_PAYLOAD_DOS_MAGIC0 ||
        magic[1] != IB_PAYLOAD_DOS_MAGIC1) {
        return ib_payload_malformed(error);
    }

    code = ib_payload_u32(pe, pe_len, IB_PAYLOAD_PE_OFFSET_AT, &value32);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    signature = (size_t)value32;

    code = ib_payload_bytes(pe, pe_len, signature, 4, &magic);
    if (code != IB_LOADER_OK || magic[0] != 'P' || magic[1] != 'E' ||
        magic[2] != 0 || magic[3] != 0) {
        return ib_payload_malformed(error);
    }

    coff = signature + 4;
    optional = coff + IB_PAYLOAD_COFF_HEADER_LEN;

    code = ib_payload_u16(pe, pe_len, optional, &optional_magic);
    if (code != IB_LOADER_OK || optional_magic != IB_PAYLOAD_OPTIONAL_MAGIC_64) {
        return ib_payload_malformed(error);
    }

    code = ib_payload_u32(pe, pe_len, optional + IB_PAYLOAD_ENTRY_AT, &value32);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    image->entry = (size_t)value32;

    code = ib_payload_u64(pe, pe_len, optional + IB_PAYLOAD_IMAGE_BASE_AT,
                          &image->base);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }

    code = ib_payload_u32(pe, pe_len, optional + IB_PAYLOAD_SIZE_OF_IMAGE_AT,
                          &value32);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    image->size = (size_t)value32;

    code = ib_payload_u32(pe, pe_len, optional + IB_PAYLOAD_SIZE_OF_HEADERS_AT,
                          &value32);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    image->headers = (size_t)value32;

    if (image->size == 0 || image->size < image->headers ||
        image->headers > pe_len || image->entry >= image->size) {
        return ib_payload_malformed(error);
    }

    directories = optional + IB_PAYLOAD_RVA_COUNT_AT + 4;

    code = ib_payload_u32(pe, pe_len, optional + IB_PAYLOAD_RVA_COUNT_AT,
                          &value32);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    if ((size_t)value32 > IB_PAYLOAD_RELOCATION_INDEX) {
        size_t at = directories + IB_PAYLOAD_RELOCATION_INDEX * IB_PAYLOAD_DIRECTORY_LEN;
        ib_u32 address;
        ib_u32 length;

        code = ib_payload_u32(pe, pe_len, at, &address);
        if (code != IB_LOADER_OK) {
            return ib_payload_malformed(error);
        }
        code = ib_payload_u32(pe, pe_len, at + 4, &length);
        if (code != IB_LOADER_OK) {
            return ib_payload_malformed(error);
        }

        image->has_relocations = 1;
        image->reloc_start = (size_t)address;
        image->reloc_end = (size_t)address + (size_t)length;
        if (image->reloc_end > image->size) {
            return ib_payload_malformed(error);
        }
    }

    code = ib_payload_u16(pe, pe_len, coff + IB_PAYLOAD_SECTION_COUNT_AT,
                          &section_count);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    code = ib_payload_u16(pe, pe_len, coff + IB_PAYLOAD_OPTIONAL_LEN_AT,
                          &optional_len);
    if (code != IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    table = optional + (size_t)optional_len;

    image->sections = ib_boot_alloc((size_t)section_count *
                                    sizeof(ib_payload_section_t));

    for (index = 0; index < (size_t)section_count; index++) {
        size_t header = table + index * IB_PAYLOAD_SECTION_HEADER_LEN;
        ib_u32 raw_size;
        ib_u32 raw_offset;
        ib_u32 virtual_size;
        ib_u32 virtual_address;
        ib_payload_section_t *section;
        const ib_u8 *probe;

        code = ib_payload_u32(pe, pe_len, header + IB_PAYLOAD_RAW_SIZE_AT,
                              &raw_size);
        if (code != IB_LOADER_OK) {
            ib_boot_free(image->sections);
            return ib_payload_malformed(error);
        }
        if (raw_size == 0) {
            continue;
        }

        code = ib_payload_u32(pe, pe_len, header + IB_PAYLOAD_RAW_OFFSET_AT,
                              &raw_offset);
        if (code != IB_LOADER_OK) {
            ib_boot_free(image->sections);
            return ib_payload_malformed(error);
        }
        if (ib_payload_bytes(pe, pe_len, (size_t)raw_offset, (size_t)raw_size,
                             &probe) != IB_LOADER_OK) {
            ib_boot_free(image->sections);
            return ib_payload_malformed(error);
        }

        code = ib_payload_u32(pe, pe_len, header + IB_PAYLOAD_VIRTUAL_ADDRESS_AT,
                              &virtual_address);
        if (code != IB_LOADER_OK) {
            ib_boot_free(image->sections);
            return ib_payload_malformed(error);
        }
        code = ib_payload_u32(pe, pe_len, header + IB_PAYLOAD_VIRTUAL_SIZE_AT,
                              &virtual_size);
        if (code != IB_LOADER_OK) {
            ib_boot_free(image->sections);
            return ib_payload_malformed(error);
        }

        section = &image->sections[image->section_count++];
        section->raw = (size_t)raw_offset;
        section->raw_len = (size_t)raw_size;
        section->mapped = (size_t)virtual_address;
        section->mapped_len =
            virtual_size == 0 ? (size_t)raw_size : (size_t)virtual_size;

        if (section->mapped + section->mapped_len > image->size) {
            ib_boot_free(image->sections);
            return ib_payload_malformed(error);
        }
    }

    image->pages = (image->size + IB_EFI_PAGE_SIZE - 1) / IB_EFI_PAGE_SIZE;
    return IB_LOADER_OK;
}

/* Allocates the pages the image maps into. */
static ib_loader_code ib_payload_allocate(const ib_payload_image_t *image,
                                          ib_u8 **out,
                                          ib_loader_error_t *error) {
    uint64_t address = 0;
    ib_efi_status status = ib_boot_allocate_pages(IbEfiLoaderCode, image->pages,
                                                  &address);

    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }
    *out = (ib_u8 *)(uintptr_t)address;
    return IB_LOADER_OK;
}

/* Copies the image into its allocation, zeroing what the file does not cover. */
static ib_loader_code ib_payload_copy(const ib_u8 *pe, size_t pe_len,
                                      const ib_payload_image_t *image,
                                      ib_u8 *base, ib_loader_error_t *error) {
    const ib_u8 *source;
    size_t index;

    ib_memset(base, 0, image->size);

    if (ib_payload_bytes(pe, pe_len, 0, image->headers, &source) !=
        IB_LOADER_OK) {
        return ib_payload_malformed(error);
    }
    ib_memcpy(base, source, image->headers);

    for (index = 0; index < image->section_count; index++) {
        const ib_payload_section_t *section = &image->sections[index];
        size_t len = section->raw_len < section->mapped_len
                         ? section->raw_len
                         : section->mapped_len;

        if (section->mapped > image->size ||
            len > image->size - section->mapped) {
            return ib_payload_malformed(error);
        }
        if (ib_payload_bytes(pe, pe_len, section->raw, len, &source) !=
            IB_LOADER_OK) {
            return ib_payload_malformed(error);
        }
        ib_memcpy(base + section->mapped, source, len);
    }

    return IB_LOADER_OK;
}

/* Applies the image's base relocations for the address it landed on. */
static ib_loader_code ib_payload_relocate(const ib_payload_image_t *image,
                                          ib_u8 *base,
                                          ib_loader_error_t *error) {
    ib_u64 delta = (ib_u64)(uintptr_t)base - image->base;
    size_t directory_len;
    ib_u8 *directory;
    size_t at;

    if (delta == 0 || !image->has_relocations ||
        image->reloc_start == image->reloc_end) {
        return IB_LOADER_OK;
    }

    directory_len = image->reloc_end - image->reloc_start;
    directory = ib_boot_alloc(directory_len);
    ib_memcpy(directory, base + image->reloc_start, directory_len);

    at = 0;
    while (at < directory_len) {
        ib_u32 block;
        ib_u32 page;
        size_t entries_len;
        const ib_u8 *encoded;
        size_t word;

        if (ib_payload_u32(directory, directory_len, at + 4, &block) !=
            IB_LOADER_OK) {
            ib_boot_free(directory);
            return ib_payload_malformed(error);
        }
        if ((size_t)block < IB_PAYLOAD_RELOCATION_HEADER_LEN ||
            at + (size_t)block > directory_len) {
            ib_boot_free(directory);
            return ib_payload_malformed(error);
        }

        if (ib_payload_u32(directory, directory_len, at, &page) !=
            IB_LOADER_OK) {
            ib_boot_free(directory);
            return ib_payload_malformed(error);
        }

        entries_len = (size_t)block - IB_PAYLOAD_RELOCATION_HEADER_LEN;
        if (ib_payload_bytes(directory, directory_len,
                             at + IB_PAYLOAD_RELOCATION_HEADER_LEN, entries_len,
                             &encoded) != IB_LOADER_OK) {
            ib_boot_free(directory);
            return ib_payload_malformed(error);
        }

        for (word = 0; word + 1 < entries_len; word += 2) {
            ib_u16 value = (ib_u16)((ib_u16)encoded[word] |
                                    ((ib_u16)encoded[word + 1] << 8));
            ib_u16 kind = (ib_u16)(value >> 12);
            size_t patched;
            ib_u64 held;

            if (kind == IB_PAYLOAD_RELOCATION_ABSOLUTE) {
                continue;
            }
            if (kind != IB_PAYLOAD_RELOCATION_DIR64) {
                ib_boot_free(directory);
                ib_loader_error_unsupported_relocation(error, kind);
                return IB_LOADER_ERR_UNSUPPORTED_RELOCATION;
            }

            patched = (size_t)page + (size_t)(value & 0x0fffu);
            if (patched > image->size || 8 > image->size - patched) {
                ib_boot_free(directory);
                return ib_payload_malformed(error);
            }

            ib_memcpy(&held, base + patched, 8);
            held += delta;
            ib_memcpy(base + patched, &held, 8);
        }

        at += (size_t)block;
    }

    ib_boot_free(directory);
    return IB_LOADER_OK;
}

/* Calls the payload's entry point, and hands back whatever it returns. */
static ib_efi_status ib_payload_call(ib_u8 *base,
                                     const ib_payload_image_t *image) {
    ib_efi_system_table *system_table = ib_efi_get_system_table();
    void *table = system_table != NULL ? (void *)system_table : NULL;
    ib_payload_entry_fn entry =
        (ib_payload_entry_fn)(void *)(base + image->entry);

    return entry(ib_boot_image_handle(), table);
}

ib_loader_code ib_payload_run(const ib_u8 *pe, size_t pe_len,
                              ib_loader_error_t *error) {
    ib_payload_image_t image;
    ib_u8 *base = NULL;
    ib_loader_code code;
    ib_efi_status returned;
    ib_efi_status freed;

    code = ib_payload_parse(pe, pe_len, &image, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    code = ib_payload_allocate(&image, &base, error);
    if (code != IB_LOADER_OK) {
        ib_boot_free(image.sections);
        return code;
    }

    code = ib_payload_copy(pe, pe_len, &image, base, error);
    if (code != IB_LOADER_OK) {
        ib_boot_free(image.sections);
        return code;
    }

    code = ib_payload_relocate(&image, base, error);
    if (code != IB_LOADER_OK) {
        ib_boot_free(image.sections);
        return code;
    }

    ib_console_printf("experimental boot: entering the payload at %#018llx\n",
                      (unsigned long long)(uintptr_t)(base + image.entry));
    returned = ib_payload_call(base, &image);

    freed = ib_boot_free_pages((ib_u64)(uintptr_t)base, image.pages);
    {
        char status_text[48];
        ib_status_debug(returned, status_text, sizeof(status_text));
        ib_console_printf("experimental boot: the payload returned %s\n",
                          status_text);
    }

    ib_boot_free(image.sections);
    if (IB_EFI_IS_ERROR(freed)) {
        ib_loader_error_uefi(error, freed);
        return IB_LOADER_ERR_UEFI;
    }
    return IB_LOADER_OK;
}
