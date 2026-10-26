/* Host fixture for the real boot-manager transaction and image wrappers.
 * The fake filesystem and BootServices model lifecycle edges only; this test
 * does not call the TPM, replay, mapper, or payload paths. */
#include "ib_boot.h"
#include "ib_bootmgfw.h"
#include "ib_console.h"
#include "ib_fs.h"
#include "ib_loader_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                              \
            return 0;                                                          \
        }                                                                      \
    } while (0)

#define FIXTURE_FILE_COUNT 8u
#define FIXTURE_FILE_CAPACITY 256u

typedef struct {
    char path[96];
    ib_u8 bytes[FIXTURE_FILE_CAPACITY];
    size_t length;
    ib_fs_saved_t saved;
    int present;
} fixture_file_t;

static fixture_file_t fixture_files[FIXTURE_FILE_COUNT];
static size_t pool_allocations;
static size_t pool_frees;
static const char *fail_write_path;
static unsigned int fail_write_count;
static int corrupt_target_read_once;
static unsigned int console_messages;

static void fixture_boot_reset(void);

static const ib_u8 fixture_backup[] = "original boot manager";
static const ib_u8 fixture_displaced[] = "staged shim";

static fixture_file_t *fixture_find_file(const char *path, int create) {
    size_t index;
    fixture_file_t *free_slot = NULL;

    for (index = 0; index < FIXTURE_FILE_COUNT; index++) {
        if (fixture_files[index].path[0] == '\0') {
            if (free_slot == NULL) {
                free_slot = &fixture_files[index];
            }
            continue;
        }
        if (strcmp(fixture_files[index].path, path) == 0) {
            return &fixture_files[index];
        }
    }

    if (!create || free_slot == NULL) {
        return NULL;
    }
    (void)snprintf(free_slot->path, sizeof(free_slot->path), "%s", path);
    return free_slot;
}

static void fixture_set_file(const char *path, const ib_u8 *bytes,
                             size_t length) {
    fixture_file_t *file = fixture_find_file(path, 1);
    if (file == NULL || length > sizeof(file->bytes)) {
        abort();
    }
    memcpy(file->bytes, bytes, length);
    file->length = length;
    memset(&file->saved, 0, sizeof(file->saved));
    file->saved.attribute = 0x1234u;
    file->present = 1;
}

static int fixture_file_equals(const char *path, const ib_u8 *bytes,
                               size_t length) {
    fixture_file_t *file = fixture_find_file(path, 0);
    return file != NULL && file->present && file->length == length &&
           memcmp(file->bytes, bytes, length) == 0;
}

static int fixture_file_present(const char *path) {
    fixture_file_t *file = fixture_find_file(path, 0);
    return file != NULL && file->present;
}

static void fixture_fs_reset(int target_present) {
    memset(fixture_files, 0, sizeof(fixture_files));
    fail_write_path = NULL;
    fail_write_count = 0;
    corrupt_target_read_once = 0;
    fixture_set_file(IB_BOOTMGFW_BACKUP, fixture_backup,
                     sizeof(fixture_backup) - 1u);
    if (target_present) {
        fixture_set_file(IB_BOOTMGFW_PATH, fixture_displaced,
                         sizeof(fixture_displaced) - 1u);
    }
}

static int fixture_copy_file(const char *path, ib_bytes_t *out,
                             ib_loader_error_t *error) {
    fixture_file_t *file = fixture_find_file(path, 0);

    out->data = NULL;
    out->length = 0;
    if (file == NULL || !file->present) {
        ib_loader_error_str(error, IB_LOADER_ERR_MISSING_ARTIFACT, path);
        return 0;
    }
    out->data = (ib_u8 *)ib_boot_alloc(file->length);
    out->length = file->length;
    memcpy(out->data, file->bytes, file->length);
    return 1;
}

ib_loader_code ib_fs_read(ib_fs_volume_t *volume, const char *path,
                          ib_bytes_t *out, ib_loader_error_t *error) {
    size_t index;
    (void)volume;
    if (!fixture_copy_file(path, out, error)) {
        return error->code;
    }
    if (strcmp(path, IB_BOOTMGFW_PATH) == 0 && corrupt_target_read_once) {
        corrupt_target_read_once = 0;
        for (index = 0; index < out->length; index++) {
            out->data[index] ^= 0x01u;
        }
    }
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_read_saved(ib_fs_volume_t *volume, const char *path,
                                ib_bytes_t *out, ib_fs_saved_t *saved,
                                ib_loader_error_t *error) {
    fixture_file_t *file;
    (void)volume;
    if (!fixture_copy_file(path, out, error)) {
        return error->code;
    }
    file = fixture_find_file(path, 0);
    *saved = file->saved;
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_write_saved(ib_fs_volume_t *volume, const char *path,
                                 const ib_u8 *bytes, size_t length,
                                 const ib_fs_saved_t *saved,
                                 ib_loader_error_t *error) {
    fixture_file_t *file;
    (void)volume;
    if (length > FIXTURE_FILE_CAPACITY) {
        ib_loader_error_str(error, IB_LOADER_ERR_OVERSIZED, path);
        return IB_LOADER_ERR_OVERSIZED;
    }

    file = fixture_find_file(path, 1);
    if (file == NULL) {
        ib_loader_error_uefi(error, IB_EFI_OUT_OF_RESOURCES);
        return IB_LOADER_ERR_UEFI;
    }
    file->present = 1;
    file->length = length;
    file->saved = *saved;
    if (fail_write_count > 0 && fail_write_path != NULL &&
        strcmp(path, fail_write_path) == 0) {
        size_t partial = length == 0 ? 0 : length / 2u;
        fail_write_count--;
        memcpy(file->bytes, bytes, partial);
        ib_loader_error_uefi(error, IB_EFI_DEVICE_ERROR);
        return IB_LOADER_ERR_UEFI;
    }
    memcpy(file->bytes, bytes, length);
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_wipe(ib_fs_volume_t *volume, const char *path,
                          ib_loader_error_t *error) {
    fixture_file_t *file;
    (void)volume;
    (void)error;
    file = fixture_find_file(path, 0);
    if (file != NULL) {
        file->present = 0;
        file->length = 0;
    }
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_delete(ib_fs_volume_t *volume, const char *path,
                            ib_loader_error_t *error) {
    fixture_file_t *file;
    (void)volume;
    file = fixture_find_file(path, 0);
    if (file == NULL || !file->present) {
        ib_loader_error_str(error, IB_LOADER_ERR_MISSING_ARTIFACT, path);
        return IB_LOADER_ERR_MISSING_ARTIFACT;
    }
    file->present = 0;
    file->length = 0;
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_file_device_path(const char *path,
                                      ib_efi_device_path_protocol **out_path,
                                      ib_loader_error_t *error) {
    ib_efi_device_path_protocol *device_path;
    (void)error;
    if (strcmp(path, IB_BOOTMGFW_PATH) != 0) {
        return IB_LOADER_ERR_DEVICE_PATH;
    }
    device_path = (ib_efi_device_path_protocol *)
        ib_boot_alloc(sizeof(*device_path));
    memset(device_path, 0, sizeof(*device_path));
    *out_path = device_path;
    return IB_LOADER_OK;
}

void ib_fs_device_path_free(ib_efi_device_path_protocol *path) {
    ib_boot_free(path);
}

void ib_loader_error_str(ib_loader_error_t *error, ib_loader_code code,
                         const char *text) {
    memset(error, 0, sizeof(*error));
    error->code = code;
    error->text = text;
}

void ib_loader_error_uefi(ib_loader_error_t *error, ib_efi_status status) {
    memset(error, 0, sizeof(*error));
    error->code = IB_LOADER_ERR_UEFI;
    error->status = status;
}

void ib_console_println(const char *format, ...) {
    (void)format;
    console_messages++;
}

void ib_console_printf(const char *format, ...) {
    (void)format;
    console_messages++;
}

void ib_status_debug(ib_efi_status status, char *buffer, size_t capacity) {
    (void)status;
    if (capacity > 0) {
        buffer[0] = '\0';
    }
}

static int test_restore_missing_target(void) {
    ib_fs_volume_t volume = { 0 };
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_fs_reset(0);
    code = ib_bootmgfw_restore(&volume, &error);
    CHECK(code == IB_LOADER_OK);
    CHECK(fixture_file_equals(IB_BOOTMGFW_PATH, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_BACKUP, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(!fixture_file_present(IB_BOOTMGFW_STAGING));
    return 1;
}

static int test_restore_existing_target(void) {
    ib_fs_volume_t volume = { 0 };
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_fs_reset(1);
    code = ib_bootmgfw_restore(&volume, &error);
    CHECK(code == IB_LOADER_OK);
    CHECK(fixture_file_equals(IB_BOOTMGFW_PATH, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_BACKUP, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(!fixture_file_present(IB_BOOTMGFW_STAGING));
    return 1;
}

static int test_restore_write_failure_rolls_back_and_keeps_recovery_files(void) {
    ib_fs_volume_t volume = { 0 };
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_fs_reset(1);
    fail_write_path = IB_BOOTMGFW_PATH;
    fail_write_count = 1;
    code = ib_bootmgfw_restore(&volume, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_DEVICE_ERROR);
    CHECK(fixture_file_equals(IB_BOOTMGFW_PATH, fixture_displaced,
                              sizeof(fixture_displaced) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_BACKUP, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_STAGING, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    return 1;
}

static int test_restore_verify_failure_rolls_back_and_keeps_recovery_files(void) {
    ib_fs_volume_t volume = { 0 };
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_fs_reset(1);
    corrupt_target_read_once = 1;
    code = ib_bootmgfw_restore(&volume, &error);
    CHECK(code == IB_LOADER_ERR_METADATA);
    CHECK(fixture_file_equals(IB_BOOTMGFW_PATH, fixture_displaced,
                              sizeof(fixture_displaced) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_BACKUP, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_STAGING, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    return 1;
}

static int test_restore_write_failure_without_displaced_target_keeps_stage(void) {
    ib_fs_volume_t volume = { 0 };
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_fs_reset(0);
    fail_write_path = IB_BOOTMGFW_PATH;
    fail_write_count = 1;
    code = ib_bootmgfw_restore(&volume, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_DEVICE_ERROR);
    CHECK(fixture_file_present(IB_BOOTMGFW_PATH));
    CHECK(fixture_file_equals(IB_BOOTMGFW_BACKUP, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_STAGING, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    return 1;
}

static int test_stage_write_failure_preserves_backup_and_target(void) {
    ib_fs_volume_t volume = { 0 };
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_fs_reset(1);
    fail_write_path = IB_BOOTMGFW_STAGING;
    fail_write_count = 1;
    code = ib_bootmgfw_restore(&volume, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_DEVICE_ERROR);
    CHECK(fixture_file_equals(IB_BOOTMGFW_PATH, fixture_displaced,
                              sizeof(fixture_displaced) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_BACKUP, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_present(IB_BOOTMGFW_STAGING));
    CHECK(!fixture_file_equals(IB_BOOTMGFW_STAGING, fixture_backup,
                               sizeof(fixture_backup) - 1u));
    return 1;
}

static int test_rollback_failure_preserves_original_error_and_recovery_files(void) {
    ib_fs_volume_t volume = { 0 };
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_fs_reset(1);
    fail_write_path = IB_BOOTMGFW_PATH;
    fail_write_count = 2;
    code = ib_bootmgfw_restore(&volume, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_DEVICE_ERROR);
    CHECK(fixture_file_equals(IB_BOOTMGFW_BACKUP, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_equals(IB_BOOTMGFW_STAGING, fixture_backup,
                              sizeof(fixture_backup) - 1u));
    CHECK(fixture_file_present(IB_BOOTMGFW_PATH));
    CHECK(console_messages > 0);
    return 1;
}

static ib_efi_system_table fixture_system_table;
static ib_efi_boot_services fixture_boot_services;
static ib_efi_device_path_protocol fixture_device_path_storage;
#define fixture_device_path (&fixture_device_path_storage)

static ib_efi_loaded_image_protocol fixture_loaded_image;
static ib_efi_handle fixture_child_handle = (ib_efi_handle)(uintptr_t)0x1234u;
static ib_efi_handle fixture_parent_handle = (ib_efi_handle)(uintptr_t)0x4321u;
static int fixture_loaded_registered;
static int fixture_start_behavior;
static ib_efi_status fixture_start_status;
static ib_efi_status fixture_load_status;
static ib_efi_status fixture_query_status;
static unsigned int fixture_start_calls;
static unsigned int fixture_unload_calls;
static unsigned int fixture_handle_queries;
static unsigned int fixture_service_errors;
static int fixture_foreign_image;

enum {
    FixtureStartKeepsImage = 0,
    FixtureStartRemovesImage = 1,
    FixtureStartReusesHandle = 2
};

static ib_efi_status IB_EFIAPI fixture_allocate_pool(
    ib_efi_memory_type pool_type, ib_uintn size, void **buffer) {
    (void)pool_type;
    *buffer = malloc((size_t)size);
    if (*buffer == NULL) {
        return IB_EFI_OUT_OF_RESOURCES;
    }
    pool_allocations++;
    return IB_EFI_SUCCESS;
}

static ib_efi_status IB_EFIAPI fixture_free_pool(void *buffer) {
    if (buffer == NULL) {
        fixture_service_errors++;
        return IB_EFI_INVALID_PARAMETER;
    }
    pool_frees++;
    free(buffer);
    return IB_EFI_SUCCESS;
}

static ib_efi_status IB_EFIAPI fixture_handle_protocol(
    ib_efi_handle handle, ib_efi_guid_t *protocol, void **interface) {
    fixture_handle_queries++;
    if (handle != fixture_child_handle ||
        protocol->data1 != ib_guid_loaded_image.data1) {
        fixture_service_errors++;
        return IB_EFI_INVALID_PARAMETER;
    }
    if (!fixture_loaded_registered) {
        return IB_EFI_NOT_FOUND;
    }
    if (IB_EFI_IS_ERROR(fixture_query_status)) return fixture_query_status;
    *interface = &fixture_loaded_image;
    return IB_EFI_SUCCESS;
}

static ib_efi_status IB_EFIAPI fixture_load_image(
    ib_boolean boot_policy, ib_efi_handle parent,
    ib_efi_device_path_protocol *device_path, void *source_buffer,
    ib_uintn source_size, ib_efi_handle *image_handle) {
    /* A cached load still has to carry the path: shim copies it, and the
     * Windows boot manager needs the device handle it resolves to. */
    if (boot_policy != IB_FALSE || parent != fixture_parent_handle ||
        device_path == NULL || source_buffer == NULL || source_size == 0) {
        fixture_service_errors++;
        return IB_EFI_INVALID_PARAMETER;
    }
    *image_handle = fixture_child_handle;
    fixture_loaded_registered = 1;
    return fixture_load_status;
}

static ib_efi_status IB_EFIAPI fixture_start_image(
    ib_efi_handle image_handle, ib_uintn *exit_data_size,
    ib_char16 **exit_data) {
    (void)exit_data_size;
    (void)exit_data;
    fixture_start_calls++;
    if (image_handle != fixture_child_handle) {
        fixture_service_errors++;
        return IB_EFI_INVALID_PARAMETER;
    }
    if (fixture_start_behavior == FixtureStartRemovesImage) {
        fixture_loaded_registered = 0;
    } else if (fixture_start_behavior == FixtureStartReusesHandle) {
        fixture_loaded_image.image_base = &fixture_foreign_image;
    }
    return fixture_start_status;
}

static ib_efi_status IB_EFIAPI fixture_unload_image(
    ib_efi_handle image_handle) {
    fixture_unload_calls++;
    if (image_handle != fixture_child_handle) {
        fixture_service_errors++;
        return IB_EFI_INVALID_PARAMETER;
    }
    fixture_loaded_registered = 0;
    return IB_EFI_SUCCESS;
}

static void fixture_boot_reset(void) {
    memset(&fixture_system_table, 0, sizeof(fixture_system_table));
    memset(&fixture_boot_services, 0, sizeof(fixture_boot_services));
    memset(&fixture_loaded_image, 0, sizeof(fixture_loaded_image));
    fixture_boot_services.allocate_pool = fixture_allocate_pool;
    fixture_boot_services.free_pool = fixture_free_pool;
    fixture_boot_services.handle_protocol = fixture_handle_protocol;
    fixture_boot_services.load_image = fixture_load_image;
    fixture_boot_services.start_image = fixture_start_image;
    fixture_boot_services.unload_image = fixture_unload_image;
    fixture_system_table.boot_services = &fixture_boot_services;
    ib_efi_set_system_table(&fixture_system_table);
    ib_efi_set_image_handle(fixture_parent_handle);

    fixture_loaded_image.revision = 1;
    fixture_loaded_image.parent_handle = fixture_parent_handle;
    fixture_loaded_image.system_table = &fixture_system_table;
    fixture_loaded_image.device_handle = (ib_efi_handle)(uintptr_t)0x5678u;
    fixture_loaded_image.file_path = (ib_efi_device_path_protocol *)
        (uintptr_t)0x8765u;
    fixture_loaded_image.load_options_size = 4;
    fixture_loaded_image.load_options = (void *)(uintptr_t)0x9876u;
    fixture_loaded_image.image_base = (void *)(uintptr_t)0xa000u;
    fixture_loaded_image.image_size = 4096;
    fixture_loaded_image.image_code_type = IbEfiLoaderCode;
    fixture_loaded_image.image_data_type = IbEfiLoaderData;

    fixture_loaded_registered = 0;
    fixture_start_behavior = FixtureStartKeepsImage;
    fixture_start_status = IB_EFI_SUCCESS;
    fixture_load_status = IB_EFI_SUCCESS;
    fixture_query_status = IB_EFI_SUCCESS;
    fixture_start_calls = 0;
    fixture_unload_calls = 0;
    fixture_handle_queries = 0;
    fixture_service_errors = 0;
    fixture_foreign_image = 0;
    console_messages = 0;
    pool_allocations = 0;
    pool_frees = 0;
}

static int fixture_pool_balanced(void) {
    CHECK(pool_allocations == pool_frees);
    CHECK(fixture_service_errors == 0);
    return 1;
}

static int test_child_returned_security_violation_was_already_unloaded(void) {
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_start_behavior = FixtureStartRemovesImage;
    fixture_start_status = IB_EFI_SECURITY_VIOLATION;
    code = ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), fixture_device_path, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_SECURITY_VIOLATION);
    CHECK(fixture_unload_calls == 0);
    CHECK(fixture_handle_queries == 3);   /* identity + start + metadata */
    CHECK(fixture_pool_balanced());
    return 1;
}

static int test_preentry_rejection_unloads_same_registered_image(void) {
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_start_status = IB_EFI_SECURITY_VIOLATION;
    code = ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), fixture_device_path, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_SECURITY_VIOLATION);
    CHECK(fixture_unload_calls == 1);
    CHECK(fixture_handle_queries == 3);   /* identity + start + metadata */
    CHECK(fixture_pool_balanced());
    return 1;
}

static int test_snapshot_failure_stops_before_entry(void) {
    ib_loader_error_t error;
    fixture_boot_reset();
    fixture_query_status = IB_EFI_DEVICE_ERROR;
    CHECK(ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), fixture_device_path, &error) == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_DEVICE_ERROR);
    CHECK(fixture_start_calls == 0);
    CHECK(fixture_unload_calls == 1);
    CHECK(!fixture_loaded_registered);
    CHECK(fixture_pool_balanced());
    return 1;
}

static int test_reused_handle_is_not_unloaded(void) {
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_start_behavior = FixtureStartReusesHandle;
    fixture_start_status = IB_EFI_SECURITY_VIOLATION;
    code = ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), fixture_device_path, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_SECURITY_VIOLATION);
    CHECK(fixture_unload_calls == 0);
    CHECK(fixture_pool_balanced());
    return 1;
}

static int test_unexpected_success_return_cleans_owned_image(void) {
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    code = ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), fixture_device_path, &error);
    CHECK(code == IB_LOADER_ERR_BOOT_MANAGER_RETURNED);
    CHECK(error.code == IB_LOADER_ERR_BOOT_MANAGER_RETURNED);
    CHECK(fixture_unload_calls == 1);
    CHECK(fixture_pool_balanced());
    return 1;
}

static int test_unexpected_success_after_firmware_unload_is_not_unloaded_again(void) {
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_start_behavior = FixtureStartRemovesImage;
    code = ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), fixture_device_path, &error);
    CHECK(code == IB_LOADER_ERR_BOOT_MANAGER_RETURNED);
    CHECK(fixture_unload_calls == 0);
    CHECK(fixture_pool_balanced());
    return 1;
}

static int test_zero_memory_types_are_normalized_before_the_snapshot(void) {
    ib_loader_error_t error;

    fixture_boot_reset();
    fixture_loaded_image.image_code_type = (ib_efi_memory_type)0;
    fixture_loaded_image.image_data_type = (ib_efi_memory_type)0;
    CHECK(ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup),
                            fixture_device_path, &error) ==
          IB_LOADER_ERR_BOOT_MANAGER_RETURNED);
    CHECK(fixture_loaded_image.image_code_type == IbEfiLoaderCode);
    CHECK(fixture_loaded_image.image_data_type == IbEfiLoaderData);
    /* The snapshot must be taken after the fill, or cleanup decides this is
     * some other image and leaves it loaded. */
    CHECK(fixture_unload_calls == 1);
    CHECK(fixture_pool_balanced());
    return 1;
}

static int test_missing_bytes_are_refused(void) {
    ib_loader_error_t error;

    fixture_boot_reset();
    CHECK(ib_bootmgfw_start(NULL, 0, fixture_device_path, &error) ==
          IB_LOADER_ERR_MISSING_ARTIFACT);
    CHECK(!fixture_loaded_registered);
    CHECK(ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), NULL, &error) ==
          IB_LOADER_ERR_MISSING_ARTIFACT);
    CHECK(!fixture_loaded_registered);
    CHECK(fixture_service_errors == 0);
    return 1;
}

static int test_load_failure_releases_returned_handle_and_device_path(void) {
    ib_loader_error_t error;
    ib_loader_code code;

    fixture_boot_reset();
    fixture_load_status = IB_EFI_SECURITY_VIOLATION;
    code = ib_bootmgfw_start(fixture_backup, sizeof(fixture_backup), fixture_device_path, &error);
    CHECK(code == IB_LOADER_ERR_UEFI);
    CHECK(error.status == IB_EFI_SECURITY_VIOLATION);
    CHECK(fixture_unload_calls == 1);
    CHECK(fixture_handle_queries == 0);
    CHECK(fixture_pool_balanced());
    return 1;
}

typedef int (*fixture_test_fn)(void);

static int fixture_run_test(const char *name, fixture_test_fn test) {
    int passed = test();
    if (!passed) {
        fprintf(stderr, "FAIL: %s\n", name);
        return 0;
    }
    if (pool_allocations != pool_frees) {
        fprintf(stderr, "FAIL: %s left pool allocations unbalanced\n", name);
        return 0;
    }
    printf("PASS: %s\n", name);
    return 1;
}

int main(void) {
    static const struct {
        const char *name;
        fixture_test_fn test;
    } tests[] = {
        { "restore missing canonical target", test_restore_missing_target },
        { "restore existing canonical target", test_restore_existing_target },
        { "write failure rolls back and retains backup/stage",
          test_restore_write_failure_rolls_back_and_keeps_recovery_files },
        { "verify failure rolls back and retains backup/stage",
          test_restore_verify_failure_rolls_back_and_keeps_recovery_files },
        { "write failure with absent target retains recovery files",
          test_restore_write_failure_without_displaced_target_keeps_stage },
        { "stage write failure retains backup and displaced target",
          test_stage_write_failure_preserves_backup_and_target },
        { "rollback failure retains recovery files and original error",
          test_rollback_failure_preserves_original_error_and_recovery_files },
        { "returned child error is not unloaded twice",
          test_child_returned_security_violation_was_already_unloaded },
        { "pre-entry rejection unloads still-owned image",
          test_preentry_rejection_unloads_same_registered_image },
        { "reused handle is not unloaded",
          test_reused_handle_is_not_unloaded },
        { "identity snapshot failure releases image before entry",
          test_snapshot_failure_stops_before_entry },
        { "unexpected success return cleans owned image",
          test_unexpected_success_return_cleans_owned_image },
        { "unexpected success after firmware unload avoids double unload",
          test_unexpected_success_after_firmware_unload_is_not_unloaded_again },
        { "load failure releases the returned handle",
          test_load_failure_releases_returned_handle_and_device_path },
        { "zero memory types are filled before the identity snapshot",
          test_zero_memory_types_are_normalized_before_the_snapshot },
        { "missing boot manager bytes are refused before any load",
          test_missing_bytes_are_refused }
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); index++) {
        if (!fixture_run_test(tests[index].name, tests[index].test)) {
            return EXIT_FAILURE;
        }
    }
    printf("%u fixture tests passed\n",
           (unsigned int)(sizeof(tests) / sizeof(tests[0])));
    return EXIT_SUCCESS;
}
