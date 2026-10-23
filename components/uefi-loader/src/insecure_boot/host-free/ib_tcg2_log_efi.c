#include "ib_tcg2.h"

#include "ib_util.h"

static ib_tcg2_code ib_tcg2_log_uefi(ib_efi_status status,
                                     ib_tcg2_error_t *error) {
    if (error != 0) {
        error->code = IB_TCG2_ERR_UEFI;
        error->status = status;
    }
    return IB_TCG2_ERR_UEFI;
}

ib_tcg2_code ib_tcg2_log_init(ib_tcg2_log_t *log, const ib_tcglog_t *dump,
                              const ib_tcglog_algorithm_t *banks,
                              size_t bank_count, size_t headroom,
                              ib_tcg2_error_t *error) {
    ib_efi_system_table *table = ib_efi_get_system_table();
    size_t needed;
    size_t total;
    size_t length;
    size_t last;
    int has_last;
    uint8_t *bytes;
    ib_efi_status status;
    ib_tcg2_code code;

    ib_memset(log, 0, sizeof(*log));

    code = ib_tcg2_log_measure(dump, banks, bank_count, &needed, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (needed > SIZE_MAX - headroom) {
        return ib_tcg2_log_uefi(IB_EFI_OUT_OF_RESOURCES, error);
    }
    total = needed + headroom;

    bytes = 0;
    status = table->boot_services->allocate_pool(IbEfiLoaderData, total,
                                                 (void **)&bytes);
    if (IB_EFI_IS_ERROR(status)) {
        return ib_tcg2_log_uefi(status, error);
    }

    code = ib_tcg2_log_encode(dump, banks, bank_count, bytes, total, &length,
                              &last, &has_last, error);
    if (code != IB_TCG2_OK) {
        table->boot_services->free_pool(bytes);
        return code;
    }

    log->bytes = bytes;
    log->capacity = total;
    log->length = length;
    log->last = last;
    log->has_last = has_last;
    log->truncated = 0;
    return IB_TCG2_OK;
}

void ib_tcg2_log_release(ib_tcg2_log_t *log) {
    ib_efi_system_table *table = ib_efi_get_system_table();

    if (log->bytes != 0) {
        table->boot_services->free_pool(log->bytes);
    }
    ib_memset(log, 0, sizeof(*log));
}
