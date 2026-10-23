/*
 * The slice of the Rust ib-uacpi wrapper that the CRB driver consumes.
 *
 * The workspace ships no uACPI C runtime, so ib_uacpi_stub.c implements this
 * contract with benign defaults: no TPM2 table, no MSFT0101 device, therefore no
 * resources and no AML evaluation.  A future host can replace the stub with a
 * real uACPI wiring without changing a single caller in this directory.
 *
 * The Rust side wraps these uACPI entry points:
 *   find_table     -> uacpi_table_find_by_signature / uacpi_table_unref
 *   find_device    -> uacpi_find_devices
 *   hardware_id    -> uacpi_eval_hid / uacpi_free_id_string
 *   resources      -> uacpi_get_current_resources / uacpi_free_resources
 *   eval_dsm       -> uacpi_eval + uacpi_object_create_* / uacpi_object_unref
 *   init           -> uacpi_initialize / uacpi_namespace_load /
 *                     uacpi_namespace_initialize
 *   monotonic      -> uacpi_kernel_get_nanoseconds_since_boot
 *   stall          -> uacpi_kernel_stall / uacpi_kernel_sleep
 */
#ifndef IB_UACPI_H
#define IB_UACPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t ib_uacpi_status_t;

#define IB_UACPI_STATUS_OK             0u
#define IB_UACPI_STATUS_NOT_FOUND      1u
#define IB_UACPI_STATUS_OUT_OF_MEMORY  2u
#define IB_UACPI_STATUS_INTERNAL_ERROR 3u

typedef struct {
    ib_uacpi_status_t status;
} ib_uacpi_error_t;

const char *ib_uacpi_status_message(ib_uacpi_status_t status);
int ib_uacpi_error_is_not_found(const ib_uacpi_error_t *error);

/* Initialization mirrors uacpi_initialize + namespace load + initialize. */
ib_uacpi_status_t ib_uacpi_init(ib_uacpi_error_t *error);

/* ACPI tables.  Release every found table exactly once. */
typedef struct {
    const uint8_t *bytes;
    size_t length;
} ib_uacpi_table_t;

ib_uacpi_status_t ib_uacpi_find_table(const char *signature, ib_uacpi_table_t *out,
                                      int *found, ib_uacpi_error_t *error);
void ib_uacpi_table_release(ib_uacpi_table_t *table);

/* Namespace devices and their _HID. */
typedef struct {
    void *node;
} ib_uacpi_device_t;

#define IB_UACPI_HID_CAPACITY 64

typedef struct {
    char text[IB_UACPI_HID_CAPACITY];
} ib_uacpi_hardware_id_t;

ib_uacpi_status_t ib_uacpi_find_device(const char *hardware_id, ib_uacpi_device_t *out,
                                       int *found, ib_uacpi_error_t *error);
ib_uacpi_status_t ib_uacpi_device_hardware_id(ib_uacpi_device_t device,
                                              ib_uacpi_hardware_id_t *out,
                                              ib_uacpi_error_t *error);

/* _CRS memory ranges.  Rust keeps uACPI's descriptor list alive; this C surface
 * decodes the memory-producing descriptors into a fixed table instead.  The CRB
 * driver only trusts the first three, so the cap cannot change its behaviour. */
#define IB_UACPI_MAX_MEMORY_RANGES 16

typedef struct {
    uint64_t start;
    uint64_t end;
} ib_uacpi_memory_range_t;

typedef struct {
    ib_uacpi_memory_range_t ranges[IB_UACPI_MAX_MEMORY_RANGES];
    size_t count;
} ib_uacpi_resources_t;

uint64_t ib_uacpi_memory_range_start(const ib_uacpi_memory_range_t *range);
uint64_t ib_uacpi_memory_range_end(const ib_uacpi_memory_range_t *range);
uint64_t ib_uacpi_memory_range_length(const ib_uacpi_memory_range_t *range);
int ib_uacpi_memory_range_contains(const ib_uacpi_memory_range_t *range, uint64_t address);
int ib_uacpi_memory_range_covers(const ib_uacpi_memory_range_t *range,
                                 uint64_t start, uint64_t length);

ib_uacpi_status_t ib_uacpi_device_resources(ib_uacpi_device_t device,
                                            ib_uacpi_resources_t *out,
                                            ib_uacpi_error_t *error);
size_t ib_uacpi_resources_memory_count(const ib_uacpi_resources_t *resources);
const ib_uacpi_memory_range_t *ib_uacpi_resources_memory_at(
    const ib_uacpi_resources_t *resources, size_t index);

/* _DSM evaluation, limited to the integer-returning form this workspace uses. */
ib_uacpi_status_t ib_uacpi_device_eval_dsm_integer(ib_uacpi_device_t device,
                                                   const uint8_t guid[16],
                                                   uint64_t revision, uint64_t index,
                                                   uint64_t *out,
                                                   ib_uacpi_error_t *error);

/* Time, in nanoseconds to keep the C surface exact. */
uint64_t ib_uacpi_monotonic_ns(void);
void ib_uacpi_stall_ns(uint64_t nanoseconds);

#ifdef __cplusplus
}
#endif

#endif /* IB_UACPI_H */
