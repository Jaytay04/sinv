#ifndef IB_INSTALL_SIGN_H
#define IB_INSTALL_SIGN_H

#include <stddef.h>
#include <wchar.h>

typedef int (*ib_sign_run_process_fn)(const wchar_t *application,
                                      wchar_t *command_line,
                                      void *context);

typedef struct {
    ib_sign_run_process_fn run_process;
    void *context;
} ib_sign_process_t;

typedef int (*ib_sign_verify_signature_fn)(const wchar_t *path,
                                           const wchar_t *signer_cert_path,
                                           void *context,
                                           wchar_t *error,
                                           size_t error_count);

typedef struct {
    const wchar_t *signtool_path;
    const wchar_t *pfx_path;
    const wchar_t *signer_cert_path;
    const wchar_t *password_env;
    const wchar_t *loader_path;
    const wchar_t *payload_path;
    const wchar_t *out_dir;
    ib_sign_verify_signature_fn verify_signature;
    void *verify_context;
} ib_sign_request_t;

int ib_sign_images(const ib_sign_request_t *request,
                   const ib_sign_process_t *process,
                   wchar_t *error,
                   size_t error_count);

int ib_sign_verify_file(const wchar_t *path,
                        const wchar_t *signer_cert_path,
                        wchar_t *error,
                        size_t error_count);

int ib_sign_default_run_process(const wchar_t *application,
                                wchar_t *command_line,
                                void *context);

#endif
