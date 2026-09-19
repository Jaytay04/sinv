#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "platform_win.h"

int main(int argc, char **argv)
{
    wchar_t path[1024];
    uint8_t digest[SHA256_DIGEST_SIZE];
    size_t index;

    if (argc != 2 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                          argv[1], -1, path, 1024) == 0) {
        fprintf(stderr, "usage: audit-bootmgr-platform-test <bootmgfw.efi>\n");
        return 2;
    }
    if (!ib_audit_windows_hash_file(path, digest, NULL)) {
        fprintf(stderr, "boot manager hash failed\n");
        return 1;
    }
    if (!ib_audit_windows_verify_microsoft(path, NULL)) {
        fprintf(stderr, "trusted Microsoft Authenticode verification failed\n");
        return 1;
    }
    printf("PASS: trusted Microsoft boot manager SHA256=");
    for (index = 0u; index < SHA256_DIGEST_SIZE; ++index) {
        printf("%02X", digest[index]);
    }
    printf("\n");
    return 0;
}
