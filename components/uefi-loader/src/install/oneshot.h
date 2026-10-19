#ifndef IB_INSTALL_ONESHOT_H
#define IB_INSTALL_ONESHOT_H

#include <stddef.h>
#include <wchar.h>

typedef enum {
    IB_ONESHOT_PROFILE_LEGACY = 0,
    IB_ONESHOT_PROFILE_INVENTORY = 1,
    IB_ONESHOT_PROFILE_SHIM_INVENTORY = 2
} ib_oneshot_profile_t;

typedef int (*ib_oneshot_verify_signature_fn)(const wchar_t *image_path,
                                             const wchar_t *certificate_path,
                                             void *context,
                                             wchar_t *error,
                                             size_t error_count);

typedef int (*ib_oneshot_measure_file_fn)(const wchar_t *path,
                                          unsigned long long *size,
                                          unsigned char digest[32],
                                          void *context);

typedef struct {
    const wchar_t *volume_path;
    const wchar_t *test_root;
    unsigned int inject_failure_step;
    /* Required by shim-inventory preflight; production verifies against staged MOK.cer. */
    ib_oneshot_verify_signature_fn verify_signature;
    void *verify_context;
    /* Optional host-test seam; the production CLI leaves this unset. */
    ib_oneshot_measure_file_fn measure_file_for_test;
    void *measure_context;
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

int ib_oneshot_profile_supports_mode(ib_oneshot_profile_t profile,
                                     const wchar_t *mode);

int ib_oneshot_validate_staged(const ib_oneshot_request_t *request,
                               wchar_t *error, size_t error_count);
int ib_oneshot_validate_staged_profile(const ib_oneshot_request_t *request,
                                      ib_oneshot_profile_t profile,
                                      wchar_t *error, size_t error_count);

#endif
