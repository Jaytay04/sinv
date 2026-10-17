#ifndef DEVICE_FILTER_STORAGE_SERIAL_REWRITE_H
#define DEVICE_FILTER_STORAGE_SERIAL_REWRITE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum DeviceFilterRewriteResult {
    DeviceFilterRewriteInvalidArgument = 0,
    DeviceFilterRewriteDeviceMismatch,
    DeviceFilterRewriteMalformedDescriptor,
    DeviceFilterRewriteSerialMismatch,
    DeviceFilterRewriteSerialAbsent,
    DeviceFilterRewriteApplied
} DeviceFilterRewriteResult;

/*
 * Rewrites only a bounded STORAGE_DEVICE_DESCRIPTOR-compatible byte buffer.
 * The caller must have already bound the request to the intended disk device.
 * Descriptor fields are decoded from their documented little-endian layout;
 * no WDK types or kernel-only routines are required by this module.
 */
DeviceFilterRewriteResult DeviceFilterRewriteStorageSerial(
    void *descriptorBuffer,
    size_t returnedBytes,
    uint32_t deviceNumber,
    const char *expectedSerial,
    size_t expectedSerialLength,
    const char *replacementSerial,
    size_t replacementSerialLength);

#ifdef __cplusplus
}
#endif

#endif
