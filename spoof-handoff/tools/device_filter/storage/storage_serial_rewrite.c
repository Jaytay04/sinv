#include "storage_serial_rewrite.h"

#define STORAGE_DESCRIPTOR_MINIMUM_SIZE 36u
#define STORAGE_DESCRIPTOR_SIZE_OFFSET 4u
#define STORAGE_DESCRIPTOR_SERIAL_OFFSET 24u
#define STORAGE_FILTER_TARGET_DEVICE_NUMBER 0u
#define STORAGE_FILTER_MAXIMUM_SERIAL_LENGTH 128u

static uint32_t ReadLittleEndianU32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) |
        ((uint32_t)bytes[3] << 24);
}

static int IsBoundedPrintableAscii(const char *text, size_t length)
{
    size_t index;

    if (text == NULL || length == 0 || length > STORAGE_FILTER_MAXIMUM_SERIAL_LENGTH) {
        return 0;
    }

    for (index = 0; index < length; ++index) {
        const unsigned char value = (unsigned char)text[index];
        if (value < 0x20u || value > 0x7eu) {
            return 0;
        }
    }

    return 1;
}

static int BytesEqual(const uint8_t *left, const char *right, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (left[index] != (uint8_t)(unsigned char)right[index]) {
            return 0;
        }
    }

    return 1;
}

DeviceFilterRewriteResult DeviceFilterRewriteStorageSerial(
    void *descriptorBuffer,
    size_t returnedBytes,
    uint32_t deviceNumber,
    const char *expectedSerial,
    size_t expectedSerialLength,
    const char *replacementSerial,
    size_t replacementSerialLength)
{
    uint8_t *bytes = (uint8_t *)descriptorBuffer;
    uint32_t descriptorSize;
    uint32_t serialOffset;
    size_t serialCapacity;
    size_t actualLength;

    if (descriptorBuffer == NULL ||
        expectedSerial == NULL || replacementSerial == NULL ||
        !IsBoundedPrintableAscii(expectedSerial, expectedSerialLength) ||
        !IsBoundedPrintableAscii(replacementSerial, replacementSerialLength) ||
        expectedSerialLength != replacementSerialLength ||
        BytesEqual((const uint8_t *)expectedSerial, replacementSerial, expectedSerialLength)) {
        return DeviceFilterRewriteInvalidArgument;
    }

    if (deviceNumber != STORAGE_FILTER_TARGET_DEVICE_NUMBER) {
        return DeviceFilterRewriteDeviceMismatch;
    }

    if (returnedBytes < STORAGE_DESCRIPTOR_MINIMUM_SIZE) {
        return DeviceFilterRewriteMalformedDescriptor;
    }

    descriptorSize = ReadLittleEndianU32(bytes + STORAGE_DESCRIPTOR_SIZE_OFFSET);
    serialOffset = ReadLittleEndianU32(bytes + STORAGE_DESCRIPTOR_SERIAL_OFFSET);

    if (descriptorSize < STORAGE_DESCRIPTOR_MINIMUM_SIZE ||
        (size_t)descriptorSize > returnedBytes) {
        return DeviceFilterRewriteMalformedDescriptor;
    }

    if (serialOffset == 0) {
        return DeviceFilterRewriteSerialAbsent;
    }

    if (serialOffset < STORAGE_DESCRIPTOR_MINIMUM_SIZE || serialOffset >= descriptorSize) {
        return DeviceFilterRewriteMalformedDescriptor;
    }

    serialCapacity = (size_t)descriptorSize - (size_t)serialOffset;
    for (actualLength = 0; actualLength < serialCapacity; ++actualLength) {
        if (bytes[serialOffset + actualLength] == 0) {
            break;
        }
    }

    if (actualLength == serialCapacity || actualLength != expectedSerialLength) {
        return DeviceFilterRewriteMalformedDescriptor;
    }

    if (!BytesEqual(bytes + serialOffset, expectedSerial, expectedSerialLength)) {
        return DeviceFilterRewriteSerialMismatch;
    }

    /* Equal lengths preserve all descriptor offsets and adjacent properties. */
    {
        size_t index;
        for (index = 0; index < replacementSerialLength; ++index) {
            bytes[serialOffset + index] = (uint8_t)(unsigned char)replacementSerial[index];
        }
    }

    return DeviceFilterRewriteApplied;
}
