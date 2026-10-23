/*
 * Minimal, self-contained UEFI surface for the insecure-boot port.
 *
 * Only the types, table members and protocols this directory actually names
 * are declared.  Table members keep their specification order because their
 * offsets are part of the ABI: omitting a member above one that is used would
 * shift every member after it.
 *
 * Every identifier this directory introduces is prefixed with ib_/IB_.  Struct
 * members keep their specification spelling because they are scoped to their
 * struct and are part of the wire/ABI layout, not link-visible symbols.
 */
#ifndef IB_UEFI_H
#define IB_UEFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Base types                                                                */
/* ------------------------------------------------------------------------- */
typedef uint8_t  ib_u8;
typedef int8_t   ib_i8;
typedef uint16_t ib_u16;
typedef int16_t  ib_i16;
typedef uint32_t ib_u32;
typedef int32_t  ib_i32;
typedef uint64_t ib_u64;
typedef int64_t  ib_i64;
typedef char     ib_char8;
typedef uint16_t ib_char16;
typedef uint8_t  ib_boolean;

#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
typedef uint64_t ib_uintn;
typedef int64_t  ib_intn;
#else
typedef uint32_t ib_uintn;
typedef int32_t  ib_intn;
#endif

#define IB_FALSE 0
#define IB_TRUE  1

typedef ib_uintn ib_efi_status;
typedef void    *ib_efi_handle;
typedef void    *ib_efi_event;
typedef ib_u64   ib_efi_physical_address;
typedef ib_u64   ib_efi_virtual_address;

/* ------------------------------------------------------------------------- */
/* GUID                                                                      */
/* ------------------------------------------------------------------------- */
typedef struct {
    ib_u32 data1;
    ib_u16 data2;
    ib_u16 data3;
    ib_u8  data4[8];
} ib_efi_guid_t;

/* ------------------------------------------------------------------------- */
/* Calling convention                                                        */
/* ------------------------------------------------------------------------- */
#if defined(_MSC_VER)
#  define IB_EFIAPI __cdecl
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#  define IB_EFIAPI __attribute__((ms_abi))
#else
#  define IB_EFIAPI
#endif

/* ------------------------------------------------------------------------- */
/* Status codes                                                              */
/* ------------------------------------------------------------------------- */
#define IB_EFI_SUCCESS 0
#define IB_EFI_ENCODE_ERROR(code) ((ib_efi_status)(0x8000000000000000ULL | (ib_u64)(code)))

#define IB_EFI_LOAD_ERROR          IB_EFI_ENCODE_ERROR(1)
#define IB_EFI_INVALID_PARAMETER   IB_EFI_ENCODE_ERROR(2)
#define IB_EFI_UNSUPPORTED         IB_EFI_ENCODE_ERROR(3)
#define IB_EFI_BAD_BUFFER_SIZE     IB_EFI_ENCODE_ERROR(4)
#define IB_EFI_BUFFER_TOO_SMALL    IB_EFI_ENCODE_ERROR(5)
#define IB_EFI_NOT_READY           IB_EFI_ENCODE_ERROR(6)
#define IB_EFI_DEVICE_ERROR        IB_EFI_ENCODE_ERROR(7)
#define IB_EFI_WRITE_PROTECTED     IB_EFI_ENCODE_ERROR(8)
#define IB_EFI_OUT_OF_RESOURCES    IB_EFI_ENCODE_ERROR(9)
#define IB_EFI_VOLUME_CORRUPTED    IB_EFI_ENCODE_ERROR(10)
#define IB_EFI_VOLUME_FULL         IB_EFI_ENCODE_ERROR(11)
#define IB_EFI_NO_MEDIA            IB_EFI_ENCODE_ERROR(12)
#define IB_EFI_MEDIA_CHANGED       IB_EFI_ENCODE_ERROR(13)
#define IB_EFI_NOT_FOUND           IB_EFI_ENCODE_ERROR(14)
#define IB_EFI_ACCESS_DENIED       IB_EFI_ENCODE_ERROR(15)
#define IB_EFI_NO_RESPONSE         IB_EFI_ENCODE_ERROR(16)
#define IB_EFI_NO_MAPPING          IB_EFI_ENCODE_ERROR(17)
#define IB_EFI_TIMEOUT             IB_EFI_ENCODE_ERROR(18)
#define IB_EFI_NOT_STARTED         IB_EFI_ENCODE_ERROR(19)
#define IB_EFI_ALREADY_STARTED     IB_EFI_ENCODE_ERROR(20)
#define IB_EFI_ABORTED             IB_EFI_ENCODE_ERROR(21)
#define IB_EFI_SECURITY_VIOLATION  IB_EFI_ENCODE_ERROR(26)

#define IB_EFI_IS_ERROR(status) (((ib_intn)(status)) < 0)

/* ------------------------------------------------------------------------- */
/* Table header and enums                                                    */
/* ------------------------------------------------------------------------- */
typedef struct {
    ib_u64 signature;
    ib_u32 revision;
    ib_u32 header_size;
    ib_u32 crc32;
    ib_u32 reserved;
} ib_efi_table_header;

typedef enum {
    IbAllocateAnyPages,
    IbAllocateMaxAddress,
    IbAllocateAddress,
    IbMaxAllocateType
} ib_efi_allocate_type;

typedef enum {
    IbEfiNativeInterface
} ib_efi_interface_type;

typedef enum {
    IbAllHandles,
    IbByRegisterNotify,
    IbByProtocol
} ib_efi_locate_search_type;

typedef enum {
    IbTimerCancel,
    IbTimerPeriodic,
    IbTimerRelative
} ib_efi_timer_delay;

typedef enum {
    IbEfiReservedMemoryType,
    IbEfiLoaderCode,
    IbEfiLoaderData,
    IbEfiBootServicesCode,
    IbEfiBootServicesData,
    IbEfiRuntimeServicesCode,
    IbEfiRuntimeServicesData,
    IbEfiConventionalMemory,
    IbEfiUnusableMemory,
    IbEfiACPIReclaimMemory,
    IbEfiACPIMemoryNVS,
    IbEfiMemoryMappedIO,
    IbEfiMemoryMappedIOPortSpace,
    IbEfiPalCode,
    IbEfiPersistentMemory,
    IbEfiMaxMemoryType
} ib_efi_memory_type;

typedef struct {
    ib_u32 type;
    ib_efi_physical_address physical_start;
    ib_efi_virtual_address virtual_start;
    ib_u64 number_of_pages;
    ib_u64 attribute;
} ib_efi_memory_descriptor;

/* ------------------------------------------------------------------------- */
/* Variables                                                                 */
/* ------------------------------------------------------------------------- */
#define IB_EFI_VARIABLE_NON_VOLATILE       0x00000001u
#define IB_EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002u
#define IB_EFI_VARIABLE_RUNTIME_ACCESS     0x00000004u

typedef enum {
    IbEfiResetCold,
    IbEfiResetWarm,
    IbEfiResetShutdown,
    IbEfiResetPlatformSpecific
} ib_efi_reset_type;

/* ------------------------------------------------------------------------- */
/* Device path                                                               */
/* ------------------------------------------------------------------------- */
typedef struct ib_efi_device_path_protocol {
    ib_u8 type;
    ib_u8 sub_type;
    ib_u8 length[2];
} ib_efi_device_path_protocol;

#define IB_EFI_MEDIA_DEVICE_PATH             0x04u
#define IB_EFI_MEDIA_FILEPATH_DP             0x04u
#define IB_EFI_END_DEVICE_PATH_TYPE          0x7fu
#define IB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE 0xffu

/* ------------------------------------------------------------------------- */
/* Simple text output                                                        */
/* ------------------------------------------------------------------------- */
typedef struct ib_efi_simple_text_output_protocol ib_efi_simple_text_output_protocol;

typedef struct {
    ib_i32 max_mode;
    ib_i32 mode;
    ib_i32 attribute;
    ib_i32 cursor_column;
    ib_i32 cursor_row;
    ib_boolean cursor_visible;
} ib_efi_simple_text_output_mode;

typedef ib_efi_status (IB_EFIAPI *ib_efi_text_reset)(
    ib_efi_simple_text_output_protocol *self, ib_boolean extended_verification);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_string)(
    ib_efi_simple_text_output_protocol *self, ib_char16 *string);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_test_string)(
    ib_efi_simple_text_output_protocol *self, ib_char16 *string);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_query_mode)(
    ib_efi_simple_text_output_protocol *self, ib_uintn mode_number,
    ib_uintn *columns, ib_uintn *rows);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_set_mode)(
    ib_efi_simple_text_output_protocol *self, ib_uintn mode_number);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_set_attribute)(
    ib_efi_simple_text_output_protocol *self, ib_uintn attribute);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_clear_screen)(
    ib_efi_simple_text_output_protocol *self);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_set_cursor_position)(
    ib_efi_simple_text_output_protocol *self, ib_uintn column, ib_uintn row);
typedef ib_efi_status (IB_EFIAPI *ib_efi_text_enable_cursor)(
    ib_efi_simple_text_output_protocol *self, ib_boolean visible);

struct ib_efi_simple_text_output_protocol {
    ib_efi_text_reset                reset;
    ib_efi_text_string               output_string;
    ib_efi_text_test_string          test_string;
    ib_efi_text_query_mode           query_mode;
    ib_efi_text_set_mode             set_mode;
    ib_efi_text_set_attribute        set_attribute;
    ib_efi_text_clear_screen         clear_screen;
    ib_efi_text_set_cursor_position  set_cursor_position;
    ib_efi_text_enable_cursor        enable_cursor;
    ib_efi_simple_text_output_mode  *mode;
};

/* ------------------------------------------------------------------------- */
/* Filesystem                                                                */
/* ------------------------------------------------------------------------- */
#define IB_EFI_FILE_MODE_READ   0x0000000000000001ULL
#define IB_EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define IB_EFI_FILE_MODE_CREATE 0x8000000000000000ULL

#define IB_EFI_FILE_READ_ONLY  0x0000000000000001ULL
#define IB_EFI_FILE_HIDDEN     0x0000000000000002ULL
#define IB_EFI_FILE_SYSTEM     0x0000000000000004ULL
#define IB_EFI_FILE_RESERVED   0x0000000000000008ULL
#define IB_EFI_FILE_DIRECTORY  0x0000000000000010ULL
#define IB_EFI_FILE_ARCHIVE    0x0000000000000020ULL
#define IB_EFI_FILE_VALID_ATTR 0x0000000000000037ULL

typedef struct {
    ib_u16 year;
    ib_u8  month;
    ib_u8  day;
    ib_u8  hour;
    ib_u8  minute;
    ib_u8  second;
    ib_u8  pad1;
    ib_u32 nanosecond;
    ib_i16 time_zone;
    ib_u8  daylight;
    ib_u8  pad2;
} ib_efi_time;

typedef struct {
    ib_u64 size;
    ib_u64 file_size;
    ib_u64 physical_size;
    ib_efi_time create_time;
    ib_efi_time last_access_time;
    ib_efi_time modification_time;
    ib_u64 attribute;
    ib_char16 file_name[1];
} ib_efi_file_info;

typedef struct ib_efi_file_protocol ib_efi_file_protocol;

typedef ib_efi_status (IB_EFIAPI *ib_efi_file_open)(
    ib_efi_file_protocol *self, ib_efi_file_protocol **new_handle,
    ib_char16 *file_name, ib_u64 open_mode, ib_u64 attributes);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_close)(ib_efi_file_protocol *self);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_delete)(ib_efi_file_protocol *self);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_read)(
    ib_efi_file_protocol *self, ib_uintn *buffer_size, void *buffer);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_write)(
    ib_efi_file_protocol *self, ib_uintn *buffer_size, void *buffer);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_get_position)(
    ib_efi_file_protocol *self, ib_u64 *position);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_set_position)(
    ib_efi_file_protocol *self, ib_u64 position);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_get_info)(
    ib_efi_file_protocol *self, ib_efi_guid_t *information_type,
    ib_uintn *buffer_size, void *buffer);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_set_info)(
    ib_efi_file_protocol *self, ib_efi_guid_t *information_type,
    ib_uintn buffer_size, void *buffer);
typedef ib_efi_status (IB_EFIAPI *ib_efi_file_flush)(ib_efi_file_protocol *self);

struct ib_efi_file_protocol {
    ib_u64 revision;
    ib_efi_file_open         open;
    ib_efi_file_close        close;
    ib_efi_file_delete       delete_file;
    ib_efi_file_read         read;
    ib_efi_file_write        write;
    ib_efi_file_get_position get_position;
    ib_efi_file_set_position set_position;
    ib_efi_file_get_info     get_info;
    ib_efi_file_set_info     set_info;
    ib_efi_file_flush        flush;
};

typedef struct ib_efi_simple_file_system_protocol ib_efi_simple_file_system_protocol;

typedef ib_efi_status (IB_EFIAPI *ib_efi_simple_file_system_open_volume)(
    ib_efi_simple_file_system_protocol *self, ib_efi_file_protocol **root);

struct ib_efi_simple_file_system_protocol {
    ib_u64 revision;
    ib_efi_simple_file_system_open_volume open_volume;
};

typedef struct {
    ib_u32 revision;
    ib_efi_handle parent_handle;
    void *system_table;
    ib_efi_handle device_handle;
    ib_efi_device_path_protocol *file_path;
    void *reserved;
    ib_u32 load_options_size;
    void *load_options;
    void *image_base;
    ib_u64 image_size;
    ib_efi_memory_type image_code_type;
    ib_efi_memory_type image_data_type;
    void *unload;
} ib_efi_loaded_image_protocol;

/* ------------------------------------------------------------------------- */
/* Runtime services                                                          */
/* ------------------------------------------------------------------------- */
typedef ib_efi_status (IB_EFIAPI *ib_efi_get_variable)(
    ib_char16 *name, ib_efi_guid_t *vendor, ib_u32 *attributes,
    ib_uintn *data_size, void *data);
typedef ib_efi_status (IB_EFIAPI *ib_efi_get_next_variable_name)(
    ib_uintn *name_size, ib_char16 *name, ib_efi_guid_t *vendor);
typedef ib_efi_status (IB_EFIAPI *ib_efi_set_variable)(
    ib_char16 *name, ib_efi_guid_t *vendor, ib_u32 attributes,
    ib_uintn data_size, void *data);
typedef void (IB_EFIAPI *ib_efi_reset_system)(
    ib_efi_reset_type reset_type, ib_efi_status reset_status,
    ib_uintn data_size, void *reset_data);

typedef struct ib_efi_runtime_services {
    ib_efi_table_header hdr;
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    ib_efi_get_variable get_variable;
    ib_efi_get_next_variable_name get_next_variable_name;
    ib_efi_set_variable set_variable;
    void *get_next_high_monotonic_count;
    ib_efi_reset_system reset_system;
    void *update_capsule;
    void *query_capsule_capabilities;
    void *query_variable_info;
} ib_efi_runtime_services;

/* ------------------------------------------------------------------------- */
/* Boot services                                                             */
/* ------------------------------------------------------------------------- */
typedef ib_uintn (IB_EFIAPI *ib_efi_raise_tpl)(ib_uintn new_tpl);
typedef void (IB_EFIAPI *ib_efi_restore_tpl)(ib_uintn old_tpl);
typedef ib_efi_status (IB_EFIAPI *ib_efi_allocate_pages)(
    ib_efi_allocate_type type, ib_efi_memory_type memory_type,
    ib_uintn pages, ib_efi_physical_address *memory);
typedef ib_efi_status (IB_EFIAPI *ib_efi_free_pages)(
    ib_efi_physical_address memory, ib_uintn pages);
typedef ib_efi_status (IB_EFIAPI *ib_efi_get_memory_map)(
    ib_uintn *memory_map_size, ib_efi_memory_descriptor *memory_map,
    ib_uintn *map_key, ib_uintn *descriptor_size, ib_u32 *descriptor_version);
typedef ib_efi_status (IB_EFIAPI *ib_efi_allocate_pool)(
    ib_efi_memory_type pool_type, ib_uintn size, void **buffer);
typedef ib_efi_status (IB_EFIAPI *ib_efi_free_pool)(void *buffer);
typedef void (IB_EFIAPI *ib_efi_event_notify)(ib_efi_event event, void *context);
typedef ib_efi_status (IB_EFIAPI *ib_efi_create_event)(
    ib_u32 type, ib_uintn notify_tpl, ib_efi_event_notify notify_function,
    void *notify_context, ib_efi_event *event);
typedef ib_efi_status (IB_EFIAPI *ib_efi_set_timer)(
    ib_efi_event event, ib_efi_timer_delay type, ib_u64 trigger_time);
typedef ib_efi_status (IB_EFIAPI *ib_efi_wait_for_event)(
    ib_uintn number_of_events, ib_efi_event *event, ib_uintn *index);
typedef ib_efi_status (IB_EFIAPI *ib_efi_signal_event)(ib_efi_event event);
typedef ib_efi_status (IB_EFIAPI *ib_efi_close_event)(ib_efi_event event);
typedef ib_efi_status (IB_EFIAPI *ib_efi_check_event)(ib_efi_event event);
typedef ib_efi_status (IB_EFIAPI *ib_efi_install_protocol_interface)(
    ib_efi_handle *handle, ib_efi_guid_t *protocol,
    ib_efi_interface_type interface_type, void *interface);
typedef ib_efi_status (IB_EFIAPI *ib_efi_reinstall_protocol_interface)(
    ib_efi_handle handle, ib_efi_guid_t *protocol,
    void *old_interface, void *new_interface);
typedef ib_efi_status (IB_EFIAPI *ib_efi_uninstall_protocol_interface)(
    ib_efi_handle handle, ib_efi_guid_t *protocol, void *interface);
typedef ib_efi_status (IB_EFIAPI *ib_efi_handle_protocol)(
    ib_efi_handle handle, ib_efi_guid_t *protocol, void **interface);
typedef ib_efi_status (IB_EFIAPI *ib_efi_register_protocol_notify)(
    ib_efi_guid_t *protocol, ib_efi_event event, void **registration);
typedef ib_efi_status (IB_EFIAPI *ib_efi_locate_handle)(
    ib_efi_locate_search_type search_type, ib_efi_guid_t *protocol,
    void *search_key, ib_uintn *buffer_size, ib_efi_handle *buffer);
typedef ib_efi_status (IB_EFIAPI *ib_efi_locate_device_path)(
    ib_efi_guid_t *protocol, ib_efi_device_path_protocol **device_path,
    ib_efi_handle *device);
typedef ib_efi_status (IB_EFIAPI *ib_efi_install_configuration_table)(
    ib_efi_guid_t *guid, void *table);
typedef ib_efi_status (IB_EFIAPI *ib_efi_image_load)(
    ib_boolean boot_policy, ib_efi_handle parent_image_handle,
    ib_efi_device_path_protocol *device_path, void *source_buffer,
    ib_uintn source_size, ib_efi_handle *image_handle);
typedef ib_efi_status (IB_EFIAPI *ib_efi_image_start)(
    ib_efi_handle image_handle, ib_uintn *exit_data_size, ib_char16 **exit_data);
typedef ib_efi_status (IB_EFIAPI *ib_efi_exit)(
    ib_efi_handle image_handle, ib_efi_status exit_status,
    ib_uintn exit_data_size, ib_char16 *exit_data);
typedef ib_efi_status (IB_EFIAPI *ib_efi_image_unload)(ib_efi_handle image_handle);
typedef ib_efi_status (IB_EFIAPI *ib_efi_exit_boot_services)(
    ib_efi_handle image_handle, ib_uintn map_key);
typedef ib_efi_status (IB_EFIAPI *ib_efi_get_next_monotonic_count)(ib_u64 *count);
typedef ib_efi_status (IB_EFIAPI *ib_efi_stall)(ib_uintn microseconds);
typedef ib_efi_status (IB_EFIAPI *ib_efi_set_watchdog_timer)(
    ib_uintn timeout, ib_u64 watchdog_code, ib_uintn data_size,
    ib_char16 *watchdog_data);
typedef ib_efi_status (IB_EFIAPI *ib_efi_connect_controller)(
    ib_efi_handle controller_handle, ib_efi_handle *driver_image_handle,
    ib_efi_device_path_protocol *remaining_device_path, ib_boolean recursive);
typedef ib_efi_status (IB_EFIAPI *ib_efi_disconnect_controller)(
    ib_efi_handle controller_handle, ib_efi_handle driver_image_handle,
    ib_efi_handle child_handle);
typedef ib_efi_status (IB_EFIAPI *ib_efi_open_protocol)(
    ib_efi_handle handle, ib_efi_guid_t *protocol, void **interface,
    ib_efi_handle agent_handle, ib_efi_handle controller_handle,
    ib_u32 attributes);
typedef ib_efi_status (IB_EFIAPI *ib_efi_close_protocol)(
    ib_efi_handle handle, ib_efi_guid_t *protocol,
    ib_efi_handle agent_handle, ib_efi_handle controller_handle);
typedef ib_efi_status (IB_EFIAPI *ib_efi_open_protocol_information)(
    ib_efi_handle handle, ib_efi_guid_t *protocol,
    void **entry_buffer, ib_uintn *entry_count);
typedef ib_efi_status (IB_EFIAPI *ib_efi_protocols_per_handle)(
    ib_efi_handle handle, ib_efi_guid_t ***protocol_buffer,
    ib_uintn *protocol_buffer_count);
typedef ib_efi_status (IB_EFIAPI *ib_efi_locate_handle_buffer)(
    ib_efi_locate_search_type search_type, ib_efi_guid_t *protocol,
    void *search_key, ib_uintn *no_handles, ib_efi_handle **buffer);
typedef ib_efi_status (IB_EFIAPI *ib_efi_locate_protocol)(
    ib_efi_guid_t *protocol, void *registration, void **interface);
typedef ib_efi_status (IB_EFIAPI *ib_efi_install_multiple_protocol_interfaces)(
    ib_efi_handle *handle, ...);
typedef ib_efi_status (IB_EFIAPI *ib_efi_uninstall_multiple_protocol_interfaces)(
    ib_efi_handle handle, ...);
typedef ib_efi_status (IB_EFIAPI *ib_efi_calculate_crc32)(
    void *data, ib_uintn data_size, ib_u32 *crc32);
typedef void (IB_EFIAPI *ib_efi_copy_mem)(
    void *destination, const void *source, ib_uintn length);
typedef void (IB_EFIAPI *ib_efi_set_mem)(
    void *buffer, ib_uintn size, ib_u8 value);
typedef ib_efi_status (IB_EFIAPI *ib_efi_create_event_ex)(
    ib_u32 type, ib_uintn notify_tpl, ib_efi_event_notify notify_function,
    const void *notify_context, const ib_efi_guid_t *event_group,
    ib_efi_event *event);

typedef struct ib_efi_boot_services {
    ib_efi_table_header hdr;

    ib_efi_raise_tpl   raise_tpl;
    ib_efi_restore_tpl restore_tpl;

    ib_efi_allocate_pages  allocate_pages;
    ib_efi_free_pages      free_pages;
    ib_efi_get_memory_map  get_memory_map;
    ib_efi_allocate_pool   allocate_pool;
    ib_efi_free_pool       free_pool;

    ib_efi_create_event   create_event;
    ib_efi_set_timer      set_timer;
    ib_efi_wait_for_event wait_for_event;
    ib_efi_signal_event   signal_event;
    ib_efi_close_event    close_event;
    ib_efi_check_event    check_event;

    ib_efi_install_protocol_interface   install_protocol_interface;
    ib_efi_reinstall_protocol_interface reinstall_protocol_interface;
    ib_efi_uninstall_protocol_interface uninstall_protocol_interface;
    ib_efi_handle_protocol              handle_protocol;
    void                               *reserved;
    ib_efi_register_protocol_notify     register_protocol_notify;
    ib_efi_locate_handle                locate_handle;
    ib_efi_locate_device_path           locate_device_path;
    ib_efi_install_configuration_table  install_configuration_table;

    ib_efi_image_load    load_image;
    ib_efi_image_start   start_image;
    ib_efi_exit          exit;
    ib_efi_image_unload  unload_image;
    ib_efi_exit_boot_services exit_boot_services;

    ib_efi_get_next_monotonic_count get_next_monotonic_count;
    ib_efi_stall                    stall;
    ib_efi_set_watchdog_timer       set_watchdog_timer;

    ib_efi_connect_controller    connect_controller;
    ib_efi_disconnect_controller disconnect_controller;

    ib_efi_open_protocol             open_protocol;
    ib_efi_close_protocol            close_protocol;
    ib_efi_open_protocol_information open_protocol_information;

    ib_efi_protocols_per_handle    protocols_per_handle;
    ib_efi_locate_handle_buffer    locate_handle_buffer;
    ib_efi_locate_protocol         locate_protocol;
    ib_efi_install_multiple_protocol_interfaces   install_multiple_protocol_interfaces;
    ib_efi_uninstall_multiple_protocol_interfaces uninstall_multiple_protocol_interfaces;

    ib_efi_calculate_crc32 calculate_crc32;

    ib_efi_copy_mem  copy_mem;
    ib_efi_set_mem   set_mem;
    ib_efi_create_event_ex create_event_ex;
} ib_efi_boot_services;

/* ------------------------------------------------------------------------- */
/* System table                                                              */
/* ------------------------------------------------------------------------- */
typedef struct {
    ib_efi_guid_t vendor_guid;
    void *vendor_table;
} ib_efi_configuration_table;

typedef struct {
    ib_efi_table_header hdr;
    ib_char16 *firmware_vendor;
    ib_u32 firmware_revision;
    ib_efi_handle console_in_handle;
    void *con_in;
    ib_efi_handle console_out_handle;
    ib_efi_simple_text_output_protocol *con_out;
    ib_efi_handle standard_error_handle;
    ib_efi_simple_text_output_protocol *std_err;
    ib_efi_runtime_services *runtime_services;
    ib_efi_boot_services *boot_services;
    ib_uintn number_of_table_entries;
    ib_efi_configuration_table *configuration_table;
} ib_efi_system_table;

/* ------------------------------------------------------------------------- */
/* Firmware table and image handle                                           */
/* ------------------------------------------------------------------------- */
/* The entry point installs these before calling into the port.  They are the
 * C counterpart of the globals the uefi crate keeps behind boot::*(). */
void ib_efi_set_system_table(ib_efi_system_table *table);
ib_efi_system_table *ib_efi_get_system_table(void);
void ib_efi_set_image_handle(ib_efi_handle handle);
ib_efi_handle ib_efi_image_handle(void);

/* ------------------------------------------------------------------------- */
/* Well-known GUIDs                                                          */
/* ------------------------------------------------------------------------- */
static const ib_efi_guid_t ib_guid_loaded_image = {
    0x5b1b31a1, 0x9562, 0x11d2,
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
static const ib_efi_guid_t ib_guid_simple_file_system = {
    0x964e5b22, 0x6459, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
static const ib_efi_guid_t ib_guid_device_path = {
    0x09576e91, 0x6d3f, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
static const ib_efi_guid_t ib_guid_file_info = {
    0x09576e92, 0x6d3f, 0x11d2,
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};

#define IB_EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL  0x00000001u
#define IB_EFI_OPEN_PROTOCOL_GET_PROTOCOL        0x00000002u
#define IB_EFI_OPEN_PROTOCOL_TEST_PROTOCOL       0x00000004u
#define IB_EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER 0x00000008u
#define IB_EFI_OPEN_PROTOCOL_BY_DRIVER           0x00000010u
#define IB_EFI_OPEN_PROTOCOL_EXCLUSIVE           0x00000020u

#define IB_EFI_PAGE_SIZE 4096u

/* ------------------------------------------------------------------------- */
/* Device path utilities                                                     */
/* ------------------------------------------------------------------------- */
/* uefi-rs builds a device path by appending a node onto the boot volume's own
 * path with this protocol, so the loader does the same rather than reimplement
 * the concatenation.  Only AppendDevicePath is called; the whole table is
 * declared so every member keeps its ABI offset. */
typedef struct ib_efi_device_path_utilities_protocol
    ib_efi_device_path_utilities_protocol;

typedef ib_uintn (IB_EFIAPI *ib_efi_dp_get_size)(
    const ib_efi_device_path_protocol *device_path);
typedef ib_efi_device_path_protocol *(IB_EFIAPI *ib_efi_dp_duplicate)(
    const ib_efi_device_path_protocol *device_path);
typedef ib_efi_device_path_protocol *(IB_EFIAPI *ib_efi_dp_append)(
    const ib_efi_device_path_protocol *source1,
    const ib_efi_device_path_protocol *source2);
typedef ib_efi_device_path_protocol *(IB_EFIAPI *ib_efi_dp_append_node)(
    const ib_efi_device_path_protocol *device_path,
    const ib_efi_device_path_protocol *device_node);
typedef ib_efi_device_path_protocol *(IB_EFIAPI *ib_efi_dp_append_instance)(
    const ib_efi_device_path_protocol *device_path,
    const ib_efi_device_path_protocol *device_path_instance);
typedef ib_efi_device_path_protocol *(IB_EFIAPI *ib_efi_dp_get_next_instance)(
    ib_efi_device_path_protocol **device_path_instance,
    ib_uintn *device_path_instance_size);
typedef ib_boolean (IB_EFIAPI *ib_efi_dp_is_multi_instance)(
    const ib_efi_device_path_protocol *device_path);
typedef ib_efi_device_path_protocol *(IB_EFIAPI *ib_efi_dp_create_node)(
    ib_u8 node_type, ib_u8 node_sub_type, ib_u16 node_length);

struct ib_efi_device_path_utilities_protocol {
    ib_efi_dp_get_size           get_device_path_size;
    ib_efi_dp_duplicate          duplicate_device_path;
    ib_efi_dp_append             append_device_path;
    ib_efi_dp_append_node        append_device_path_node;
    ib_efi_dp_append_instance    append_device_path_instance;
    ib_efi_dp_get_next_instance  get_next_device_path_instance;
    ib_efi_dp_is_multi_instance  is_device_path_multi_instance;
    ib_efi_dp_create_node        create_device_node;
};

static const ib_efi_guid_t ib_guid_device_path_utilities = {
    0x0379be4e, 0xd706, 0x437d,
    { 0xb0, 0x37, 0xed, 0xb9, 0xce, 0xb8, 0x24, 0xda }
};

#ifdef __cplusplus
}
#endif

#endif /* IB_UEFI_H */
