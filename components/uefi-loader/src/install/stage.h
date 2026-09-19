#ifndef IB_INSTALL_STAGE_H
#define IB_INSTALL_STAGE_H

#include <stddef.h>
#include <wchar.h>

typedef int (*ib_stage_verify_signature_fn)(const wchar_t *path,
                                            void *context,
                                            wchar_t *error,
                                            size_t error_count);

typedef struct {
    const wchar_t *shim_path;
    const wchar_t *loader_path;
    const wchar_t *log_path;
    const wchar_t *payload_path;
    const wchar_t *volume_path;
    const wchar_t *test_root;
    ib_stage_verify_signature_fn verify_signature;
    void *verify_context;
} ib_stage_request_t;

int ib_stage_files(const ib_stage_request_t *request,
                   wchar_t *error,
                   size_t error_count);

int ib_stage_validate_test_root(const wchar_t *root,
                                wchar_t *resolved,
                                size_t resolved_count,
                                wchar_t *error,
                                size_t error_count);

int ib_stage_find_esp(wchar_t *volume_path,
                      size_t volume_path_count,
                      wchar_t *error,
                      size_t error_count);

#endif
