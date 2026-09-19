#ifndef IB_INSTALL_AUTHENTICODE_H
#define IB_INSTALL_AUTHENTICODE_H

#include <stddef.h>
#include <wchar.h>

int ib_authenticode_verify_file(const wchar_t *path,
                                const wchar_t *signer_cert_path,
                                wchar_t *error,
                                size_t error_count);

#endif
