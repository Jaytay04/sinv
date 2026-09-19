#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "authenticode.h"

static void set_error(wchar_t *error, size_t count, const wchar_t *format, ...)
{
    va_list args;
    if (error == NULL || count == 0u) return;
    va_start(args, format);
    _vsnwprintf_s(error, count, _TRUNCATE, format, args);
    va_end(args);
}

static int verify_pe_integrity(const wchar_t *path,
                               wchar_t *error,
                               size_t error_count)
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO file;
    WINTRUST_DATA trust;
    LONG status;
    ZeroMemory(&file, sizeof(file));
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path;
    ZeroMemory(&trust, sizeof(trust));
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    status = WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &trust);
    if (status == ERROR_SUCCESS || status == CERT_E_UNTRUSTEDROOT ||
        status == CERT_E_CHAINING) return 1;
    set_error(error, error_count, L"Authenticode integrity verification failed (0x%08lX)",
              (unsigned long)status);
    return 0;
}

int ib_authenticode_verify_file(const wchar_t *path,
                                const wchar_t *signer_cert_path,
                                wchar_t *error,
                                size_t error_count)
{
    HCERTSTORE message_store = NULL;
    HCRYPTMSG message = NULL;
    PCCERT_CONTEXT expected = NULL;
    PCCERT_CONTEXT embedded = NULL;
    DWORD encoding = 0, content_type = 0, format = 0;
    DWORD signer_count = 0, size = sizeof(signer_count);
    DWORD signer_index;
    int success = 0;

    if (path == NULL || signer_cert_path == NULL || path[0] == L'\0' ||
        signer_cert_path[0] == L'\0') {
        set_error(error, error_count, L"signature verification input is incomplete");
        return 0;
    }
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, signer_cert_path,
                          CERT_QUERY_CONTENT_FLAG_CERT,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0u, &encoding,
                          &content_type, &format, NULL, NULL,
                          (const void **)&expected) || expected == NULL) {
        set_error(error, error_count, L"signer certificate is not a DER certificate");
        goto cleanup;
    }
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path,
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY, 0u, &encoding,
                          &content_type, &format, &message_store, &message, NULL) ||
        message == NULL || message_store == NULL) {
        set_error(error, error_count, L"file has no embedded Authenticode signature");
        goto cleanup;
    }
    if (!CryptMsgGetParam(message, CMSG_SIGNER_COUNT_PARAM, 0u,
                          &signer_count, &size) || signer_count == 0u) {
        set_error(error, error_count, L"embedded Authenticode message is malformed");
        goto cleanup;
    }
    if (!verify_pe_integrity(path, error, error_count)) goto cleanup;

    for (signer_index = 0u; signer_index < signer_count; ++signer_index) {
        CMSG_SIGNER_INFO *signer = NULL;
        DWORD signer_size = 0;
        CERT_INFO search;
        CMSG_CTRL_VERIFY_SIGNATURE_EX_PARA verify;
        if (!CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, signer_index,
                              NULL, &signer_size)) continue;
        signer = (CMSG_SIGNER_INFO *)malloc(signer_size);
        if (signer == NULL ||
            !CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, signer_index,
                              signer, &signer_size)) {
            free(signer);
            continue;
        }
        ZeroMemory(&search, sizeof(search));
        search.Issuer = signer->Issuer;
        search.SerialNumber = signer->SerialNumber;
        embedded = CertFindCertificateInStore(message_store,
                                               X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                               0u, CERT_FIND_SUBJECT_CERT,
                                               &search, NULL);
        if (embedded != NULL && embedded->cbCertEncoded == expected->cbCertEncoded &&
            memcmp(embedded->pbCertEncoded, expected->pbCertEncoded,
                   expected->cbCertEncoded) == 0) {
            ZeroMemory(&verify, sizeof(verify));
            verify.cbSize = sizeof(verify);
            verify.dwSignerIndex = signer_index;
            verify.dwSignerType = CMSG_VERIFY_SIGNER_CERT;
            verify.pvSigner = (void *)embedded;
            if (CryptMsgControl(message, 0u, CMSG_CTRL_VERIFY_SIGNATURE_EX, &verify)) {
                success = 1;
            } else {
                set_error(error, error_count, L"embedded Authenticode signature is invalid");
            }
        }
        CertFreeCertificateContext(embedded);
        embedded = NULL;
        free(signer);
        if (success) break;
    }
    if (!success && (error == NULL || error[0] == L'\0')) {
        set_error(error, error_count, L"embedded signer does not match the pinned certificate");
    }
cleanup:
    CertFreeCertificateContext(embedded);
    if (message != NULL) CryptMsgClose(message);
    if (message_store != NULL) CertCloseStore(message_store, 0u);
    CertFreeCertificateContext(expected);
    return success;
}
