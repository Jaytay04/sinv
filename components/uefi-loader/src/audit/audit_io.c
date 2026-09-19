#include <stdio.h>
#include <stdlib.h>

#include "audit_io.h"

int ib_audit_read_file(const char *path, uint8_t **bytes, size_t *size)
{
    FILE *file = NULL;
    long length;
    uint8_t *buffer;

    if (path == NULL || bytes == NULL || size == NULL) {
        return 0;
    }
    *bytes = NULL;
    *size = 0;

#if defined(_MSC_VER)
    if (fopen_s(&file, path, "rb") != 0 || file == NULL) {
        return 0;
    }
#else
    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
#endif

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length <= 0 || (unsigned long)length > IB_AUDIT_MAX_FILE_SIZE) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    buffer = (uint8_t *)malloc((size_t)length);
    if (buffer == NULL) {
        fclose(file);
        return 0;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        free(buffer);
        return 0;
    }

    *bytes = buffer;
    *size = (size_t)length;
    return 1;
}
