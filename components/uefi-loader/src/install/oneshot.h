#ifndef IB_INSTALL_ONESHOT_H
#define IB_INSTALL_ONESHOT_H

#include <stddef.h>
#include <wchar.h>

typedef struct {
    const wchar_t *volume_path;
    const wchar_t *test_root;
    unsigned int inject_failure_step;
} ib_oneshot_request_t;

typedef struct {
    int rollback_attempted;
    int rollback_succeeded;
    unsigned char original_sha256[32];
} ib_oneshot_report_t;

int ib_oneshot_replace(const ib_oneshot_request_t *request,
                       ib_oneshot_report_t *report,
                       wchar_t *error, size_t error_count);

int ib_oneshot_verify(const ib_oneshot_request_t *request,
                      wchar_t *error, size_t error_count);

int ib_oneshot_cleanup_sibling(const ib_oneshot_request_t *request,
                               wchar_t *error, size_t error_count);

int ib_oneshot_validate_staged(const ib_oneshot_request_t *request,
                               wchar_t *error, size_t error_count);

#endif
