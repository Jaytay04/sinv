#include "ib_tpm_crb.h"
#include "ib_format.h"
#include "ib_util.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define IB_TPM_CRB_FENCE_ACQUIRE() _ReadWriteBarrier()
#define IB_TPM_CRB_FENCE_RELEASE() _ReadWriteBarrier()
#else
#define IB_TPM_CRB_FENCE_ACQUIRE() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define IB_TPM_CRB_FENCE_RELEASE() __atomic_thread_fence(__ATOMIC_RELEASE)
#endif

#define IB_TPM_CRB_ACPI_START_GUID_INIT \
    { 0xAB, 0x6C, 0xBF, 0x6B, 0x63, 0x54, 0x14, 0x47, \
      0xB7, 0xCD, 0xF0, 0x20, 0x3C, 0x03, 0x68, 0xD4 }

#define IB_TPM_CRB_ACPI_START_REVISION 1u
#define IB_TPM_CRB_ACPI_START_INDEX    1u

#define IB_TPM_CRB_TIMEOUT_C_NS         200000000u
#define IB_TPM_CRB_PLUTON_START_NS      200000000u
#define IB_TPM_CRB_POLL_NS                  50000u
#define IB_TPM_CRB_COMMAND_DURATION_NS 2000000000u
#define IB_TPM_CRB_COMMAND_POLL_NS        1000000u

#define IB_TPM_CRB_DOORBELL_LEN 4u
#define IB_TPM_CRB_PLUTON_READY 1u
#define IB_TPM_CRB_PLUTON_RING  1u

#define IB_TPM_CRB_TPM_HEADER_LEN     10u
#define IB_TPM_CRB_RESPONSE_LENGTH_AT  2u
#define IB_TPM_CRB_RESPONSE_PREFIX     8u

#define IB_TPM_CRB_LOC_ASSIGNED      0x00000002u
#define IB_TPM_CRB_TPM_REG_VALID_STS 0x00000080u

#define IB_TPM_CRB_LOC_REQUEST_ACCESS 0x00000001u
#define IB_TPM_CRB_LOC_RELINQUISH     0x00000002u

#define IB_TPM_CRB_CTRL_REQ_CMD_READY 0x00000001u
#define IB_TPM_CRB_CTRL_REQ_GO_IDLE   0x00000002u

#define IB_TPM_CRB_CTRL_STS_ERROR    0x00000001u
#define IB_TPM_CRB_CTRL_STS_TPM_IDLE 0x00000002u

#define IB_TPM_CRB_CTRL_START_INVOKE  0x00000001u
#define IB_TPM_CRB_CTRL_CANCEL_INVOKE 0x00000001u

typedef struct {
    ib_tpm_crb_region_t region;
} ib_tpm_crb_head_t;

typedef struct {
    ib_tpm_crb_region_t region;
} ib_tpm_crb_tail_t;

typedef struct {
    ib_uacpi_memory_range_t slot[IB_TPM_CRB_MAX_RESOURCES];
    int present[IB_TPM_CRB_MAX_RESOURCES];
} ib_tpm_crb_ranges_t;

static uint32_t ib_tpm_crb_read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t ib_tpm_crb_read_le64(const uint8_t *bytes) {
    uint64_t low = (uint64_t)ib_tpm_crb_read_le32(bytes);
    uint64_t high = (uint64_t)ib_tpm_crb_read_le32(bytes + 4);
    return high << 32 | low;
}

static uint32_t ib_tpm_crb_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static int ib_tpm_crb_read_u32_at(const uint8_t *bytes, size_t length,
                                  size_t offset, uint32_t *out) {
    if (offset > length || length - offset < 4) {
        return 0;
    }
    *out = ib_tpm_crb_read_le32(bytes + offset);
    return 1;
}

static int ib_tpm_crb_read_u64_at(const uint8_t *bytes, size_t length,
                                  size_t offset, uint64_t *out) {
    if (offset > length || length - offset < 8) {
        return 0;
    }
    *out = ib_tpm_crb_read_le64(bytes + offset);
    return 1;
}

ib_tpm_crb_start_method_t ib_tpm_crb_start_method_from_raw(uint32_t raw) {
    ib_tpm_crb_start_method_t method;
    method.raw = raw;
    return method;
}

uint32_t ib_tpm_crb_start_method_raw(ib_tpm_crb_start_method_t method) {
    return method.raw;
}

const char *ib_tpm_crb_start_method_name(ib_tpm_crb_start_method_t method) {
    switch (method.raw) {
    case IB_TPM_CRB_START_ACPI_START:                  return "ACPI start";
    case IB_TPM_CRB_START_MEMORY_MAPPED:               return "memory mapped";
    case IB_TPM_CRB_START_COMMAND_BUFFER:              return "command buffer";
    case IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ACPI:    return "command buffer with ACPI start";
    case IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ARM_SMC: return "command buffer with ARM SMC";
    case IB_TPM_CRB_START_COMMAND_BUFFER_WITH_PLUTON:  return "command buffer with Pluton";
    case IB_TPM_CRB_START_CRB_WITH_ARM_FFA:            return "CRB with Arm FF-A";
    default:                                            return "unknown";
    }
}

void ib_tpm_crb_start_method_display(ib_tpm_crb_start_method_t method,
                                     char *buffer, size_t capacity) {
    ib_snprintf(buffer, capacity, "%s (%u)",
                ib_tpm_crb_start_method_name(method), (unsigned)method.raw);
}

int ib_tpm_crb_start_method_has_idle(ib_tpm_crb_start_method_t method) {
    return !(method.raw == IB_TPM_CRB_START_ACPI_START ||
             method.raw == IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ACPI ||
             method.raw == IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ARM_SMC);
}

int ib_tpm_crb_start_method_uses_start_register(ib_tpm_crb_start_method_t method) {
    return method.raw == IB_TPM_CRB_START_COMMAND_BUFFER ||
           method.raw == IB_TPM_CRB_START_MEMORY_MAPPED;
}

int ib_tpm_crb_start_method_uses_acpi_start(ib_tpm_crb_start_method_t method) {
    return method.raw == IB_TPM_CRB_START_ACPI_START ||
           method.raw == IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ACPI;
}

size_t ib_tpm_crb_table_fixed_len(void) {
    return IB_TPM_CRB_TABLE_FIXED_LEN;
}

static void ib_tpm_crb_table_too_short(ib_tpm_crb_error_t *error, size_t length,
                                       ib_tpm_crb_start_method_t method) {
    error->code = IB_TPM_CRB_ERR_TABLE_TOO_SHORT;
    error->length = length;
    error->start_method = method;
}

int ib_tpm_crb_table_parse(const uint8_t *bytes, size_t length,
                           ib_tpm_crb_tpm2_table_t *out, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_start_method_t start_method;
    uint32_t raw;
    uint64_t control_address;
    const uint8_t *parameters;
    size_t parameters_length;

    ib_memset(error, 0, sizeof(*error));
    ib_memset(out, 0, sizeof(*out));

    if (!ib_tpm_crb_read_u32_at(bytes, length, IB_TPM_CRB_START_METHOD_AT, &raw)) {
        ib_tpm_crb_table_too_short(error, length,
                                   ib_tpm_crb_start_method_from_raw(0));
        return 0;
    }
    start_method = ib_tpm_crb_start_method_from_raw(raw);

    if (!ib_tpm_crb_read_u64_at(bytes, length, IB_TPM_CRB_CONTROL_ADDRESS,
                                &control_address)) {
        ib_tpm_crb_table_too_short(error, length, start_method);
        return 0;
    }

    parameters = bytes + IB_TPM_CRB_TABLE_FIXED_LEN;
    parameters_length = length - IB_TPM_CRB_TABLE_FIXED_LEN;

    switch (start_method.raw) {
    case IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ARM_SMC:
        if (parameters_length < IB_TPM_CRB_SMC_LEN) {
            ib_tpm_crb_table_too_short(error, length, start_method);
            return 0;
        }
        break;
    case IB_TPM_CRB_START_CRB_WITH_ARM_FFA:
        if (parameters_length < IB_TPM_CRB_FFA_LEN) {
            ib_tpm_crb_table_too_short(error, length, start_method);
            return 0;
        }
        break;
    case IB_TPM_CRB_START_COMMAND_BUFFER_WITH_PLUTON:
        if (parameters_length < IB_TPM_CRB_PLUTON_LEN) {
            ib_tpm_crb_table_too_short(error, length, start_method);
            return 0;
        }
        if (!ib_tpm_crb_read_u64_at(parameters, parameters_length, 0,
                                    &out->pluton_start) ||
            !ib_tpm_crb_read_u64_at(parameters, parameters_length, 8,
                                    &out->pluton_reply)) {
            ib_tpm_crb_table_too_short(error, length, start_method);
            return 0;
        }
        out->has_pluton = 1;
        break;
    default:
        break;
    }

    out->control_address = control_address;
    out->start_method = start_method;
    error->code = IB_TPM_CRB_OK;
    return 1;
}

static void ib_tpm_crb_set_unmappable(ib_tpm_crb_error_t *error,
                                      uint64_t start, uint64_t length) {
    error->code = IB_TPM_CRB_ERR_UNMAPPABLE_REGION;
    error->start = start;
    error->len = length;
}

ib_tpm_crb_code ib_tpm_crb_region_new(uint64_t start, uint64_t length,
                                      ib_tpm_crb_region_t *out,
                                      ib_tpm_crb_error_t *error) {
    uint64_t last;

    ib_memset(error, 0, sizeof(*error));

    if (length == 0) {
        ib_tpm_crb_set_unmappable(error, start, length);
        return error->code;
    }
    last = start + (length - 1u);
    if (last < start || last > (uint64_t)(uintptr_t)~(uintptr_t)0 ||
        start > (uint64_t)(uintptr_t)~(uintptr_t)0 ||
        length > (uint64_t)(size_t)~(size_t)0) {
        ib_tpm_crb_set_unmappable(error, start, length);
        return error->code;
    }

    out->base = (uintptr_t)start;
    out->length = (size_t)length;
    error->code = IB_TPM_CRB_OK;
    return IB_TPM_CRB_OK;
}

ib_tpm_crb_code ib_tpm_crb_region_registers(uint64_t start, uint64_t length,
                                            ib_tpm_crb_region_t *out,
                                            ib_tpm_crb_error_t *error) {
    if ((start & 3u) == 0) {
        return ib_tpm_crb_region_new(start, length, out, error);
    }
    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_ERR_MISALIGNED_REGISTERS;
    error->address = start;
    return error->code;
}

ib_tpm_crb_code ib_tpm_crb_region_subregion(const ib_tpm_crb_region_t *region,
                                            size_t offset, size_t length,
                                            ib_tpm_crb_region_t *out,
                                            ib_tpm_crb_error_t *error) {
    uint64_t start;
    if (length > (size_t)~(size_t)0 - offset ||
        offset + length > region->length) {
        start = (uint64_t)(uintptr_t)(region->base + offset);
        ib_memset(error, 0, sizeof(*error));
        ib_tpm_crb_set_unmappable(error, start, (uint64_t)length);
        return error->code;
    }
    return ib_tpm_crb_region_new((uint64_t)(uintptr_t)(region->base + offset),
                                 (uint64_t)length, out, error);
}

uint32_t ib_tpm_crb_region_read32(const ib_tpm_crb_region_t *region, size_t offset) {
    return *(volatile const uint32_t *)(region->base + offset);
}

void ib_tpm_crb_region_write32(const ib_tpm_crb_region_t *region, size_t offset,
                               uint32_t value) {
    *(volatile uint32_t *)(region->base + offset) = value;
}

ib_tpm_crb_code ib_tpm_crb_region_read_bytes(const ib_tpm_crb_region_t *region,
                                             size_t offset, uint8_t *destination,
                                             size_t length, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_region_t source;
    ib_tpm_crb_code code = ib_tpm_crb_region_subregion(region, offset, length,
                                                        &source, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }
    ib_memcpy(destination, (const void *)source.base, length);
    IB_TPM_CRB_FENCE_ACQUIRE();
    error->code = IB_TPM_CRB_OK;
    return IB_TPM_CRB_OK;
}

ib_tpm_crb_code ib_tpm_crb_region_write_bytes(const ib_tpm_crb_region_t *region,
                                              size_t offset, const uint8_t *source,
                                              size_t length, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_region_t destination;
    ib_tpm_crb_code code = ib_tpm_crb_region_subregion(region, offset, length,
                                                        &destination, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }
    ib_memcpy((void *)destination.base, source, length);
    IB_TPM_CRB_FENCE_RELEASE();
    error->code = IB_TPM_CRB_OK;
    return IB_TPM_CRB_OK;
}

static ib_tpm_crb_head_t ib_tpm_crb_head(ib_tpm_crb_region_t region) {
    ib_tpm_crb_head_t head;
    head.region = region;
    return head;
}

static ib_tpm_crb_tail_t ib_tpm_crb_tail(ib_tpm_crb_region_t region) {
    ib_tpm_crb_tail_t tail;
    tail.region = region;
    return tail;
}

static uint32_t ib_tpm_crb_head_loc_state(ib_tpm_crb_head_t head) {
    return ib_tpm_crb_region_read32(&head.region, 0x00) &
           (IB_TPM_CRB_LOC_ASSIGNED | IB_TPM_CRB_TPM_REG_VALID_STS);
}

static int ib_tpm_crb_head_loc_state_matches(ib_tpm_crb_head_t head,
                                             uint32_t mask, uint32_t expected) {
    return (ib_tpm_crb_head_loc_state(head) & mask) == expected;
}

static void ib_tpm_crb_head_set_loc_ctrl(ib_tpm_crb_head_t head, uint32_t value) {
    ib_tpm_crb_region_write32(&head.region, 0x08, value);
}

static uint64_t ib_tpm_crb_head_interface_id(ib_tpm_crb_head_t head) {
    uint32_t low = ib_tpm_crb_region_read32(&head.region, 0x30);
    uint32_t high = ib_tpm_crb_region_read32(&head.region, 0x34);
    return (uint64_t)high << 32 | (uint64_t)low;
}

static uint32_t ib_tpm_crb_tail_ctrl_req(ib_tpm_crb_tail_t tail) {
    return ib_tpm_crb_region_read32(&tail.region, 0x00) & 0x3u;
}

static void ib_tpm_crb_tail_set_ctrl_req(ib_tpm_crb_tail_t tail, uint32_t value) {
    ib_tpm_crb_region_write32(&tail.region, 0x00, value);
}

static uint32_t ib_tpm_crb_tail_ctrl_sts(ib_tpm_crb_tail_t tail) {
    return ib_tpm_crb_region_read32(&tail.region, 0x04) & 0x3u;
}

static uint32_t ib_tpm_crb_tail_ctrl_cancel(ib_tpm_crb_tail_t tail) {
    return ib_tpm_crb_region_read32(&tail.region, 0x08) & 0x1u;
}

static void ib_tpm_crb_tail_set_ctrl_cancel(ib_tpm_crb_tail_t tail, uint32_t value) {
    ib_tpm_crb_region_write32(&tail.region, 0x08, value);
}

static uint32_t ib_tpm_crb_tail_ctrl_start(ib_tpm_crb_tail_t tail) {
    return ib_tpm_crb_region_read32(&tail.region, 0x0C) & 0x1u;
}

static void ib_tpm_crb_tail_set_ctrl_start(ib_tpm_crb_tail_t tail, uint32_t value) {
    ib_tpm_crb_region_write32(&tail.region, 0x0C, value);
}

static uint32_t ib_tpm_crb_tail_command_size(ib_tpm_crb_tail_t tail) {
    return ib_tpm_crb_region_read32(&tail.region, 0x18);
}

static uint64_t ib_tpm_crb_tail_command_address(ib_tpm_crb_tail_t tail) {
    uint32_t high = ib_tpm_crb_region_read32(&tail.region, 0x20);
    uint32_t low = ib_tpm_crb_region_read32(&tail.region, 0x1C);
    return (uint64_t)high << 32 | (uint64_t)low;
}

static uint32_t ib_tpm_crb_tail_response_size(ib_tpm_crb_tail_t tail) {
    return ib_tpm_crb_region_read32(&tail.region, 0x24);
}

static ib_tpm_crb_code ib_tpm_crb_tail_response_address(ib_tpm_crb_tail_t tail,
                                                        uint64_t *out,
                                                        ib_tpm_crb_error_t *error) {
    uint8_t bytes[8];
    ib_tpm_crb_code code = ib_tpm_crb_region_read_bytes(&tail.region, 0x28,
                                                        bytes, sizeof(bytes), error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }
    *out = ib_tpm_crb_read_le64(bytes);
    return IB_TPM_CRB_OK;
}

typedef int (*ib_tpm_crb_ready_fn)(const void *context);

static int ib_tpm_crb_wait_until(uint64_t timeout_ns,
                                 ib_tpm_crb_ready_fn ready,
                                 const void *context) {
    uint64_t deadline = ib_uacpi_monotonic_ns() + timeout_ns;
    for (;;) {
        if (ready(context)) {
            return 1;
        }
        ib_uacpi_stall_ns(IB_TPM_CRB_POLL_NS);
        if (ib_uacpi_monotonic_ns() >= deadline) {
            return ready(context);
        }
    }
}

typedef struct {
    ib_tpm_crb_tail_t tail;
    uint32_t request;
} ib_tpm_crb_request_wait_t;

static int ib_tpm_crb_ready_request(const void *context) {
    const ib_tpm_crb_request_wait_t *wait = (const ib_tpm_crb_request_wait_t *)context;
    return (ib_tpm_crb_tail_ctrl_req(wait->tail) & wait->request) == 0;
}

typedef struct {
    ib_tpm_crb_head_t head;
    uint32_t mask;
    uint32_t expected;
} ib_tpm_crb_locality_wait_t;

static int ib_tpm_crb_ready_locality(const void *context) {
    const ib_tpm_crb_locality_wait_t *wait =
        (const ib_tpm_crb_locality_wait_t *)context;
    return ib_tpm_crb_head_loc_state_matches(wait->head, wait->mask, wait->expected);
}

typedef struct {
    ib_tpm_crb_region_t region;
} ib_tpm_crb_register_wait_t;

static int ib_tpm_crb_ready_reply(const void *context) {
    const ib_tpm_crb_register_wait_t *wait =
        (const ib_tpm_crb_register_wait_t *)context;
    return ib_tpm_crb_region_read32(&wait->region, 0) == IB_TPM_CRB_PLUTON_READY;
}

static int ib_tpm_crb_ready_pending(const void *context) {
    const ib_tpm_crb_register_wait_t *wait =
        (const ib_tpm_crb_register_wait_t *)context;
    return ib_tpm_crb_region_read32(&wait->region, 0) == 0;
}

static ib_tpm_crb_code ib_tpm_crb_interface_request(
    const ib_tpm_crb_interface_t *interface, uint32_t request,
    const char *what, ib_tpm_crb_error_t *error);

static ib_tpm_crb_code ib_tpm_crb_interface_pluton_doorbell(
    const ib_tpm_crb_interface_t *interface, int wait_for_acceptance,
    ib_tpm_crb_error_t *error);

static ib_tpm_crb_code ib_tpm_crb_interface_acpi_start(
    const ib_tpm_crb_interface_t *interface, ib_tpm_crb_error_t *error) {
    static const uint8_t guid[16] = IB_TPM_CRB_ACPI_START_GUID_INIT;
    uint64_t returned = 0;
    ib_uacpi_error_t acpi;

    ib_uacpi_status_t status = ib_uacpi_device_eval_dsm_integer(
        interface->device, guid, IB_TPM_CRB_ACPI_START_REVISION,
        IB_TPM_CRB_ACPI_START_INDEX, &returned, &acpi);
    if (status != IB_UACPI_STATUS_OK) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_ACPI;
        error->acpi = acpi;
        return error->code;
    }

    if (returned == 0) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_ERR_START_METHOD_FAILED;
    error->returned = returned;
    return error->code;
}

static ib_tpm_crb_code ib_tpm_crb_interface_request(
    const ib_tpm_crb_interface_t *interface, uint32_t request,
    const char *what, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_request_wait_t wait;
    ib_tpm_crb_tail_t tail = ib_tpm_crb_tail(interface->tail);

    if (!ib_tpm_crb_start_method_has_idle(interface->start_method)) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    ib_tpm_crb_tail_set_ctrl_req(tail, request);
    {
        ib_tpm_crb_code code =
            ib_tpm_crb_interface_pluton_doorbell(interface, 1, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
    }

    wait.tail = tail;
    wait.request = request;
    if (ib_tpm_crb_wait_until(IB_TPM_CRB_TIMEOUT_C_NS,
                              ib_tpm_crb_ready_request, &wait)) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_ERR_TIMEOUT;
    error->what = what;
    return error->code;
}

static ib_tpm_crb_code ib_tpm_crb_interface_request_locality(
    const ib_tpm_crb_interface_t *interface, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_head_t head;
    ib_tpm_crb_locality_wait_t wait;

    if (!interface->has_head) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    head = ib_tpm_crb_head(interface->head);
    ib_tpm_crb_head_set_loc_ctrl(head, IB_TPM_CRB_LOC_REQUEST_ACCESS);

    wait.head = head;
    wait.mask = IB_TPM_CRB_LOC_ASSIGNED | IB_TPM_CRB_TPM_REG_VALID_STS;
    wait.expected = wait.mask;
    if (ib_tpm_crb_wait_until(IB_TPM_CRB_TIMEOUT_C_NS,
                              ib_tpm_crb_ready_locality, &wait)) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_ERR_TIMEOUT;
    error->what = "the locality to be assigned";
    return error->code;
}

static ib_tpm_crb_code ib_tpm_crb_interface_relinquish_locality(
    const ib_tpm_crb_interface_t *interface, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_head_t head;
    ib_tpm_crb_locality_wait_t wait;

    if (!interface->has_head) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    head = ib_tpm_crb_head(interface->head);
    ib_tpm_crb_head_set_loc_ctrl(head, IB_TPM_CRB_LOC_RELINQUISH);

    wait.head = head;
    wait.mask = IB_TPM_CRB_LOC_ASSIGNED | IB_TPM_CRB_TPM_REG_VALID_STS;
    wait.expected = IB_TPM_CRB_TPM_REG_VALID_STS;
    if (ib_tpm_crb_wait_until(IB_TPM_CRB_TIMEOUT_C_NS,
                              ib_tpm_crb_ready_locality, &wait)) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_ERR_TIMEOUT;
    error->what = "the locality to be released";
    return error->code;
}

static ib_tpm_crb_code ib_tpm_crb_interface_pluton_doorbell(
    const ib_tpm_crb_interface_t *interface, int wait_for_acceptance,
    ib_tpm_crb_error_t *error) {
    ib_tpm_crb_register_wait_t wait;

    if (!interface->has_pluton) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    wait.region = interface->pluton_reply;
    if (!ib_tpm_crb_wait_until(IB_TPM_CRB_TIMEOUT_C_NS,
                               ib_tpm_crb_ready_reply, &wait)) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_TIMEOUT;
        error->what = "the Pluton reply register";
        return error->code;
    }

    ib_tpm_crb_region_write32(&interface->pluton_start, 0, IB_TPM_CRB_PLUTON_RING);

    if (!wait_for_acceptance) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    wait.region = interface->pluton_start;
    if (ib_tpm_crb_wait_until(IB_TPM_CRB_PLUTON_START_NS,
                              ib_tpm_crb_ready_pending, &wait)) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_ERR_TIMEOUT;
    error->what = "Pluton to accept the start request";
    return error->code;
}

static ib_tpm_crb_code ib_tpm_crb_interface_cmd_ready(
    const ib_tpm_crb_interface_t *interface, ib_tpm_crb_error_t *error) {
    return ib_tpm_crb_interface_request(interface, IB_TPM_CRB_CTRL_REQ_CMD_READY,
                                        "the command buffer to become ready",
                                        error);
}

static ib_tpm_crb_code ib_tpm_crb_interface_go_idle(
    const ib_tpm_crb_interface_t *interface, ib_tpm_crb_error_t *error) {
    return ib_tpm_crb_interface_request(interface, IB_TPM_CRB_CTRL_REQ_GO_IDLE,
                                        "the TPM to go idle", error);
}

static int ib_tpm_crb_interface_is_complete(const ib_tpm_crb_interface_t *interface) {
    ib_tpm_crb_tail_t tail = ib_tpm_crb_tail(interface->tail);
    return (ib_tpm_crb_tail_ctrl_start(tail) & IB_TPM_CRB_CTRL_START_INVOKE) == 0;
}

static int ib_tpm_crb_interface_is_cancelled(const ib_tpm_crb_interface_t *interface) {
    ib_tpm_crb_tail_t tail = ib_tpm_crb_tail(interface->tail);
    return (ib_tpm_crb_tail_ctrl_cancel(tail) & IB_TPM_CRB_CTRL_CANCEL_INVOKE) != 0;
}

static ib_tpm_crb_code ib_tpm_crb_interface_cancel(
    const ib_tpm_crb_interface_t *interface, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_tail_t tail = ib_tpm_crb_tail(interface->tail);
    ib_tpm_crb_tail_set_ctrl_cancel(tail, IB_TPM_CRB_CTRL_CANCEL_INVOKE);
    if (ib_tpm_crb_start_method_uses_acpi_start(interface->start_method)) {
        return ib_tpm_crb_interface_acpi_start(interface, error);
    }
    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_OK;
    return IB_TPM_CRB_OK;
}

static int ib_tpm_crb_collect_ranges(const ib_uacpi_resources_t *resources,
                                     ib_tpm_crb_ranges_t *ranges,
                                     ib_tpm_crb_error_t *error) {
    size_t count = ib_uacpi_resources_memory_count(resources);
    size_t index;

    ib_memset(ranges, 0, sizeof(*ranges));
    for (index = 0; index < count; index++) {
        const ib_uacpi_memory_range_t *range =
            ib_uacpi_resources_memory_at(resources, index);
        size_t slot;
        if (range == 0) {
            continue;
        }
        for (slot = 0; slot < IB_TPM_CRB_MAX_RESOURCES; slot++) {
            if (!ranges->present[slot]) {
                ranges->slot[slot] = *range;
                ranges->present[slot] = 1;
                break;
            }
        }
    }

    if (!ranges->present[0]) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_NO_MEMORY_RESOURCE;
        return 0;
    }
    return 1;
}

static int ib_tpm_crb_enclosing(const ib_tpm_crb_ranges_t *ranges,
                                uint64_t start, uint64_t length,
                                ib_uacpi_memory_range_t *out) {
    size_t index;
    for (index = 0; index < IB_TPM_CRB_MAX_RESOURCES; index++) {
        if (ranges->present[index] &&
            ib_uacpi_memory_range_covers(&ranges->slot[index], start, length)) {
            *out = ranges->slot[index];
            return 1;
        }
    }
    return 0;
}

static int ib_tpm_crb_containing(const ib_tpm_crb_ranges_t *ranges,
                                 uint64_t address,
                                 ib_uacpi_memory_range_t *out) {
    size_t index;
    for (index = 0; index < IB_TPM_CRB_MAX_RESOURCES; index++) {
        if (ranges->present[index] &&
            ib_uacpi_memory_range_contains(&ranges->slot[index], address)) {
            *out = ranges->slot[index];
            return 1;
        }
    }
    return 0;
}

static uint32_t ib_tpm_crb_clamp_to_declared(const ib_tpm_crb_ranges_t *ranges,
                                             uint64_t start, uint32_t size) {
    ib_uacpi_memory_range_t range;
    uint64_t available;

    if (size == 0) {
        return 0;
    }
    if (!ib_tpm_crb_containing(ranges, start, &range)) {
        return size;
    }
    if (ib_uacpi_memory_range_covers(&range, start, (uint64_t)size)) {
        return size;
    }

    available = ib_uacpi_memory_range_end(&range) - start + 1u;
    if (available > 0xffffffffULL) {
        return size;
    }
    return (uint32_t)available;
}

static ib_tpm_crb_code ib_tpm_crb_locate_head(
    ib_tpm_crb_start_method_t start_method, const ib_tpm_crb_ranges_t *ranges,
    uint64_t control_address, int *has_head, ib_tpm_crb_region_t *out,
    ib_tpm_crb_error_t *error) {
    ib_uacpi_memory_range_t range;
    uint64_t range_start;

    *has_head = 0;
    if (start_method.raw != IB_TPM_CRB_START_COMMAND_BUFFER) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    if (!ib_tpm_crb_enclosing(ranges, control_address, IB_TPM_CRB_TAIL_LEN,
                              &range)) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }
    range_start = ib_uacpi_memory_range_start(&range);
    if (range_start + IB_TPM_CRB_HEAD_LEN != control_address ||
        range_start + IB_TPM_CRB_HEAD_LEN < range_start) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_OK;
        return IB_TPM_CRB_OK;
    }

    {
        ib_tpm_crb_code code = ib_tpm_crb_region_registers(
            range_start, IB_TPM_CRB_HEAD_LEN, out, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
    }
    *has_head = 1;
    return IB_TPM_CRB_OK;
}

static ib_tpm_crb_code ib_tpm_crb_map_buffers(
    const ib_tpm_crb_interface_t *interface, const ib_tpm_crb_ranges_t *ranges,
    ib_tpm_crb_region_t *command, ib_tpm_crb_region_t *response,
    uint32_t *command_size, ib_tpm_crb_error_t *error) {
    ib_tpm_crb_tail_t tail = ib_tpm_crb_tail(interface->tail);
    uint64_t command_address = ib_tpm_crb_tail_command_address(tail);
    uint32_t size;
    uint64_t response_address;
    uint32_t response_size;
    ib_tpm_crb_code code;

    size = ib_tpm_crb_clamp_to_declared(ranges, command_address,
                                        ib_tpm_crb_tail_command_size(tail));
    code = ib_tpm_crb_region_new(command_address, (uint64_t)size, command, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    code = ib_tpm_crb_tail_response_address(tail, &response_address, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }
    response_size = ib_tpm_crb_clamp_to_declared(ranges, response_address,
                                                 ib_tpm_crb_tail_response_size(tail));

    if (command_address != response_address) {
        code = ib_tpm_crb_region_new(response_address, (uint64_t)response_size,
                                     response, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
        *command_size = size;
        return IB_TPM_CRB_OK;
    }

    if (size != response_size) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_BUFFER_SIZE_MISMATCH;
        error->command = size;
        error->response = response_size;
        return error->code;
    }

    *response = *command;
    *command_size = size;
    return IB_TPM_CRB_OK;
}

static ib_tpm_crb_code ib_tpm_crb_attach(const ib_tpm_crb_tpm2_table_t *info,
                                         ib_uacpi_device_t device,
                                         ib_tpm_crb_t *out,
                                         ib_tpm_crb_error_t *error) {
    ib_uacpi_resources_t resources;
    ib_tpm_crb_ranges_t ranges;
    ib_tpm_crb_interface_t interface;
    ib_uacpi_status_t status;
    ib_uacpi_error_t acpi;
    ib_tpm_crb_code code;
    ib_tpm_crb_region_t command;
    ib_tpm_crb_region_t response;
    uint32_t command_size;
    uint32_t tail_length;

    if (info->start_method.raw == IB_TPM_CRB_START_MEMORY_MAPPED) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_NOT_COMMAND_RESPONSE_BUFFER;
        error->start_method = info->start_method;
        return error->code;
    }

    if (info->start_method.raw == IB_TPM_CRB_START_CRB_WITH_ARM_FFA ||
        info->start_method.raw == IB_TPM_CRB_START_COMMAND_BUFFER_WITH_ARM_SMC) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_UNSUPPORTED_START_METHOD;
        error->start_method = info->start_method;
        return error->code;
    }

    ib_memset(&interface, 0, sizeof(interface));

    status = ib_uacpi_device_hardware_id(device, &interface.hardware_id, &acpi);
    if (status != IB_UACPI_STATUS_OK) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_ACPI;
        error->acpi = acpi;
        return error->code;
    }

    status = ib_uacpi_device_resources(device, &resources, &acpi);
    if (status != IB_UACPI_STATUS_OK) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_ACPI;
        error->acpi = acpi;
        return error->code;
    }

    if (!ib_tpm_crb_collect_ranges(&resources, &ranges, error)) {
        return error->code;
    }

    interface.start_method = info->start_method;
    interface.device = device;

    code = ib_tpm_crb_locate_head(info->start_method, &ranges,
                                  info->control_address,
                                  &interface.has_head, &interface.head, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    tail_length = IB_TPM_CRB_TAIL_LEN;
    code = ib_tpm_crb_region_registers(info->control_address, tail_length,
                                       &interface.tail, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    if (info->has_pluton) {
        code = ib_tpm_crb_region_registers(info->pluton_start,
                                           IB_TPM_CRB_DOORBELL_LEN,
                                           &interface.pluton_start, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
        code = ib_tpm_crb_region_registers(info->pluton_reply,
                                           IB_TPM_CRB_DOORBELL_LEN,
                                           &interface.pluton_reply, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
        interface.has_pluton = 1;
    }

    code = ib_tpm_crb_interface_request_locality(&interface, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    code = ib_tpm_crb_interface_cmd_ready(&interface, error);
    if (code != IB_TPM_CRB_OK) {
        ib_tpm_crb_error_t ignored;
        (void)ib_tpm_crb_interface_relinquish_locality(&interface, &ignored);
        return code;
    }

    code = ib_tpm_crb_map_buffers(&interface, &ranges, &command, &response,
                                  &command_size, error);

    {
        ib_tpm_crb_error_t ignored;
        (void)ib_tpm_crb_interface_go_idle(&interface, &ignored);
        (void)ib_tpm_crb_interface_relinquish_locality(&interface, &ignored);
    }

    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    out->interface = interface;
    out->command = command;
    out->response = response;
    out->command_size = command_size;
    return IB_TPM_CRB_OK;
}

ib_tpm_crb_code ib_tpm_crb_probe(ib_tpm_crb_t *out, int *found,
                                 ib_tpm_crb_error_t *error) {
    ib_uacpi_table_t table;
    ib_uacpi_device_t device;
    ib_uacpi_error_t acpi;
    ib_uacpi_status_t status;
    int present;
    int device_present;
    ib_tpm_crb_code code;

    ib_memset(error, 0, sizeof(*error));
    *found = 0;

    status = ib_uacpi_find_table(IB_TPM_CRB_TPM2_SIGNATURE, &table, &present, &acpi);
    if (status != IB_UACPI_STATUS_OK) {
        error->code = IB_TPM_CRB_ERR_ACPI;
        error->acpi = acpi;
        return error->code;
    }
    if (!present) {
        return IB_TPM_CRB_OK;
    }

    if (table.length < ib_tpm_crb_table_fixed_len()) {
        ib_tpm_crb_table_too_short(error, table.length,
                                   ib_tpm_crb_start_method_from_raw(0));
        ib_uacpi_table_release(&table);
        return error->code;
    }

    {
        ib_tpm_crb_tpm2_table_t info;
        if (!ib_tpm_crb_table_parse(table.bytes, table.length, &info, error)) {
            ib_uacpi_table_release(&table);
            return error->code;
        }

        status = ib_uacpi_find_device(IB_TPM_CRB_HID, &device, &device_present,
                                      &acpi);
        if (status != IB_UACPI_STATUS_OK) {
            ib_uacpi_table_release(&table);
            error->code = IB_TPM_CRB_ERR_ACPI;
            error->acpi = acpi;
            return error->code;
        }
        if (!device_present) {
            ib_uacpi_table_release(&table);
            return IB_TPM_CRB_OK;
        }

        code = ib_tpm_crb_attach(&info, device, out, error);
    }

    ib_uacpi_table_release(&table);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }
    *found = 1;
    return IB_TPM_CRB_OK;
}

ib_tpm_crb_start_method_t ib_tpm_crb_start_method(const ib_tpm_crb_t *tpm) {
    return tpm->interface.start_method;
}

const char *ib_tpm_crb_hardware_id(const ib_tpm_crb_t *tpm) {
    return tpm->interface.hardware_id.text;
}

uint32_t ib_tpm_crb_command_size(const ib_tpm_crb_t *tpm) {
    return tpm->command_size;
}

int ib_tpm_crb_interface_id(const ib_tpm_crb_t *tpm, uint64_t *out) {
    if (!tpm->interface.has_head) {
        return 0;
    }
    *out = ib_tpm_crb_head_interface_id(ib_tpm_crb_head(tpm->interface.head));
    return 1;
}

static ib_tpm_crb_code ib_tpm_crb_send(const ib_tpm_crb_t *tpm,
                                       const uint8_t *command,
                                       size_t command_length,
                                       ib_tpm_crb_error_t *error) {
    ib_tpm_crb_tail_t tail = ib_tpm_crb_tail(tpm->interface.tail);
    ib_tpm_crb_code code;

    ib_tpm_crb_tail_set_ctrl_cancel(tail, 0);

    if (command_length > (size_t)tpm->command_size) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_COMMAND_TOO_LONG;
        error->length = command_length;
        error->command = tpm->command_size;
        return error->code;
    }

    if (tpm->interface.has_pluton) {
        code = ib_tpm_crb_interface_cmd_ready(&tpm->interface, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
    }

    code = ib_tpm_crb_region_write_bytes(&tpm->command, 0, command,
                                         command_length, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    if (ib_tpm_crb_start_method_uses_start_register(tpm->interface.start_method) ||
        ib_strcmp(tpm->interface.hardware_id.text, IB_TPM_CRB_HID) == 0) {
        ib_tpm_crb_tail_set_ctrl_start(tail, IB_TPM_CRB_CTRL_START_INVOKE);
    }

    if (ib_tpm_crb_start_method_uses_acpi_start(tpm->interface.start_method)) {
        code = ib_tpm_crb_interface_acpi_start(&tpm->interface, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
    }

    return ib_tpm_crb_interface_pluton_doorbell(&tpm->interface, 0, error);
}

static ib_tpm_crb_code ib_tpm_crb_receive(const ib_tpm_crb_t *tpm,
                                          uint8_t *response,
                                          size_t response_capacity,
                                          size_t *response_length,
                                          ib_tpm_crb_error_t *error) {
    ib_tpm_crb_tail_t tail = ib_tpm_crb_tail(tpm->interface.tail);
    uint32_t length;
    ib_tpm_crb_code code;

    if (response_capacity < IB_TPM_CRB_TPM_HEADER_LEN) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_RESPONSE_TOO_LONG;
        error->length = IB_TPM_CRB_TPM_HEADER_LEN;
        error->response = (uint32_t)response_capacity;
        return error->code;
    }

    if ((ib_tpm_crb_tail_ctrl_sts(tail) & IB_TPM_CRB_CTRL_STS_ERROR) != 0) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_DEVICE_ERROR;
        return error->code;
    }

    code = ib_tpm_crb_region_read_bytes(&tpm->response, 0, response,
                                        IB_TPM_CRB_RESPONSE_PREFIX, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    length = ib_tpm_crb_read_be32(response + IB_TPM_CRB_RESPONSE_LENGTH_AT);

    if (length < IB_TPM_CRB_TPM_HEADER_LEN) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_MALFORMED_RESPONSE;
        error->length = (size_t)length;
        return error->code;
    }
    if ((size_t)length > response_capacity) {
        ib_memset(error, 0, sizeof(*error));
        error->code = IB_TPM_CRB_ERR_RESPONSE_TOO_LONG;
        error->length = (size_t)length;
        error->response = (uint32_t)response_capacity;
        return error->code;
    }

    if ((size_t)length > IB_TPM_CRB_RESPONSE_PREFIX) {
        code = ib_tpm_crb_region_read_bytes(
            &tpm->response, IB_TPM_CRB_RESPONSE_PREFIX,
            response + IB_TPM_CRB_RESPONSE_PREFIX,
            (size_t)length - IB_TPM_CRB_RESPONSE_PREFIX, error);
        if (code != IB_TPM_CRB_OK) {
            return code;
        }
    }

    *response_length = (size_t)length;
    return IB_TPM_CRB_OK;
}

static ib_tpm_crb_code ib_tpm_crb_exchange(ib_tpm_crb_t *tpm,
                                           const uint8_t *command,
                                           size_t command_length,
                                           uint8_t *response,
                                           size_t response_capacity,
                                           size_t *response_length,
                                           ib_tpm_crb_error_t *error) {
    uint64_t deadline;
    ib_tpm_crb_code code;

    code = ib_tpm_crb_interface_cmd_ready(&tpm->interface, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    code = ib_tpm_crb_send(tpm, command, command_length, error);
    if (code != IB_TPM_CRB_OK) {
        return code;
    }

    deadline = ib_uacpi_monotonic_ns() + IB_TPM_CRB_COMMAND_DURATION_NS;
    for (;;) {
        if (ib_tpm_crb_interface_is_complete(&tpm->interface)) {
            return ib_tpm_crb_receive(tpm, response, response_capacity,
                                      response_length, error);
        }
        if (ib_tpm_crb_interface_is_cancelled(&tpm->interface)) {
            ib_memset(error, 0, sizeof(*error));
            error->code = IB_TPM_CRB_ERR_CANCELLED;
            return error->code;
        }

        ib_uacpi_stall_ns(IB_TPM_CRB_COMMAND_POLL_NS);
        IB_TPM_CRB_FENCE_ACQUIRE();

        if (ib_uacpi_monotonic_ns() >= deadline) {
            break;
        }
    }

    {
        ib_tpm_crb_error_t ignored;
        (void)ib_tpm_crb_interface_cancel(&tpm->interface, &ignored);
    }
    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TPM_CRB_ERR_TIMEOUT;
    error->what = "the TPM to complete the command";
    return error->code;
}

ib_tpm_crb_code ib_tpm_crb_transmit(ib_tpm_crb_t *tpm,
                                    const uint8_t *command, size_t command_length,
                                    uint8_t *response, size_t response_capacity,
                                    size_t *response_length,
                                    ib_tpm_crb_error_t *error) {
    ib_tpm_crb_code result;

    result = ib_tpm_crb_interface_request_locality(&tpm->interface, error);
    if (result != IB_TPM_CRB_OK) {
        return result;
    }

    result = ib_tpm_crb_exchange(tpm, command, command_length,
                                 response, response_capacity, response_length,
                                 error);

    {
        ib_tpm_crb_error_t ignored;
        (void)ib_tpm_crb_interface_go_idle(&tpm->interface, &ignored);
        (void)ib_tpm_crb_interface_relinquish_locality(&tpm->interface, &ignored);
    }

    return result;
}

void ib_tpm_crb_error_message(const ib_tpm_crb_error_t *error,
                              char *buffer, size_t capacity) {
    char method[64];

    switch (error->code) {
    case IB_TPM_CRB_ERR_TABLE_TOO_SHORT:
        ib_tpm_crb_start_method_display(error->start_method, method, sizeof(method));
        ib_snprintf(buffer, capacity,
                    "the ACPI TPM2 table is %u bytes, too short for start method %s",
                    (unsigned)error->length, method);
        break;
    case IB_TPM_CRB_ERR_NOT_COMMAND_RESPONSE_BUFFER:
        ib_tpm_crb_start_method_display(error->start_method, method, sizeof(method));
        ib_snprintf(buffer, capacity,
                    "start method %s is not a Command Response Buffer interface",
                    method);
        break;
    case IB_TPM_CRB_ERR_UNSUPPORTED_START_METHOD:
        ib_tpm_crb_start_method_display(error->start_method, method, sizeof(method));
        ib_snprintf(buffer, capacity,
                    "start method %s needs firmware support this platform does not provide",
                    method);
        break;
    case IB_TPM_CRB_ERR_NO_MEMORY_RESOURCE:
        ib_snprintf(buffer, capacity,
                    "the MSFT0101 device describes no memory resource");
        break;
    case IB_TPM_CRB_ERR_UNMAPPABLE_REGION:
        ib_snprintf(buffer, capacity,
                    "the region at 0x%llx spanning 0x%llx bytes is not addressable",
                    (unsigned long long)error->start,
                    (unsigned long long)error->len);
        break;
    case IB_TPM_CRB_ERR_MISALIGNED_REGISTERS:
        ib_snprintf(buffer, capacity,
                    "the register block at 0x%llx is not 32-bit aligned",
                    (unsigned long long)error->address);
        break;
    case IB_TPM_CRB_ERR_BUFFER_SIZE_MISMATCH:
        ib_snprintf(buffer, capacity,
                    "overlapping command and response buffers differ in size (%u vs %u)",
                    (unsigned)error->command, (unsigned)error->response);
        break;
    case IB_TPM_CRB_ERR_TIMEOUT:
        ib_snprintf(buffer, capacity, "timed out waiting for %s", error->what);
        break;
    case IB_TPM_CRB_ERR_DEVICE_ERROR:
        ib_snprintf(buffer, capacity, "the TPM reports an unrecoverable error");
        break;
    case IB_TPM_CRB_ERR_COMMAND_TOO_LONG:
        ib_snprintf(buffer, capacity,
                    "a %u-byte command does not fit the %u-byte command buffer",
                    (unsigned)error->length, (unsigned)error->command);
        break;
    case IB_TPM_CRB_ERR_RESPONSE_TOO_LONG:
        ib_snprintf(buffer, capacity,
                    "a %u-byte buffer cannot hold a %u-byte response",
                    (unsigned)error->response, (unsigned)error->length);
        break;
    case IB_TPM_CRB_ERR_MALFORMED_RESPONSE:
        ib_snprintf(buffer, capacity,
                    "the response header reports an impossible length of %u bytes",
                    (unsigned)error->length);
        break;
    case IB_TPM_CRB_ERR_CANCELLED:
        ib_snprintf(buffer, capacity, "the TPM cancelled the command");
        break;
    case IB_TPM_CRB_ERR_ACPI:
        ib_snprintf(buffer, capacity, "ACPI: %s",
                    ib_uacpi_status_message(error->acpi.status));
        break;
    case IB_TPM_CRB_ERR_START_METHOD_FAILED:
        ib_snprintf(buffer, capacity, "the ACPI start method returned %llu",
                    (unsigned long long)error->returned);
        break;
    case IB_TPM_CRB_OK:
    default:
        ib_snprintf(buffer, capacity, "no TPM CRB error");
        break;
    }
}
