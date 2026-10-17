#include "storage_serial_rewrite.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(DEVICE_FILTER_STORAGE_PROFILE_HEADER)
#include DEVICE_FILTER_STORAGE_PROFILE_HEADER
#define OriginalSerial DEVICE_FILTER_STORAGE_ORIGINAL_SERIAL
#define ReplacementSerial DEVICE_FILTER_STORAGE_REPLACEMENT_SERIAL
#else
static const char OriginalSerial[] = "0000_0000_0100_0000_E4D2_5CDF_3227_5501.";
static const char ReplacementSerial[] = "1111_1111_1111_1111_A1B2_C3D4_5566_7788.";
#endif

enum {
    DescriptorMinimumSize = 36,
    DescriptorSizeOffset = 4,
    SerialOffsetField = 24,
    DescriptorSerialOffset = 64,
    DescriptorReturnedBytes = 256,
    DescriptorTotalSize = 256
};

static void WriteU32LittleEndian(unsigned char *buffer, size_t offset, uint32_t value)
{
    buffer[offset] = (unsigned char)(value & 0xffu);
    buffer[offset + 1] = (unsigned char)((value >> 8) & 0xffu);
    buffer[offset + 2] = (unsigned char)((value >> 16) & 0xffu);
    buffer[offset + 3] = (unsigned char)((value >> 24) & 0xffu);
}

static void MakeDescriptor(unsigned char *buffer)
{
    memset(buffer, 0, DescriptorReturnedBytes);
    WriteU32LittleEndian(buffer, 0, DescriptorMinimumSize);
    WriteU32LittleEndian(buffer, DescriptorSizeOffset, DescriptorTotalSize);
    WriteU32LittleEndian(buffer, SerialOffsetField, DescriptorSerialOffset);
    memcpy(buffer + DescriptorSerialOffset, OriginalSerial, sizeof(OriginalSerial));
}

static void AssertOnlyExpectedSerialChanged(
    const unsigned char *buffer,
    const unsigned char *before,
    size_t serialOffset,
    const char *expectedSerial,
    size_t expectedSerialLength)
{
    size_t index;

    for (index = 0; index < DescriptorReturnedBytes; ++index) {
        if (index >= serialOffset && index - serialOffset < expectedSerialLength) {
            assert(buffer[index] == (unsigned char)expectedSerial[index - serialOffset]);
        } else {
            assert(buffer[index] == before[index]);
        }
    }
}

static void TestApplyAndRemoveRoundTrip(void)
{
    unsigned char buffer[DescriptorReturnedBytes];
    unsigned char before[DescriptorReturnedBytes];
    DeviceFilterRewriteResult result;

    MakeDescriptor(buffer);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteApplied);
    assert(memcmp(buffer + DescriptorSerialOffset, ReplacementSerial, sizeof(ReplacementSerial)) == 0);
    AssertOnlyExpectedSerialChanged(
        buffer,
        before,
        DescriptorSerialOffset,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);

    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1,
        OriginalSerial,
        sizeof(OriginalSerial) - 1);
    assert(result == DeviceFilterRewriteApplied);
    assert(memcmp(buffer + DescriptorSerialOffset, OriginalSerial, sizeof(OriginalSerial)) == 0);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);
}

static void TestSerialOffsetAtHeaderBoundary(void)
{
    unsigned char buffer[DescriptorReturnedBytes];
    unsigned char before[DescriptorReturnedBytes];
    const uint32_t descriptorSize =
        DescriptorMinimumSize + (uint32_t)sizeof(OriginalSerial);
    DeviceFilterRewriteResult result;

    memset(buffer, 0, sizeof(buffer));
    WriteU32LittleEndian(buffer, 0, DescriptorMinimumSize);
    WriteU32LittleEndian(buffer, DescriptorSizeOffset, descriptorSize);
    WriteU32LittleEndian(buffer, SerialOffsetField, DescriptorMinimumSize);
    memcpy(buffer + DescriptorMinimumSize, OriginalSerial, sizeof(OriginalSerial));
    memcpy(before, buffer, sizeof(buffer));

    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteApplied);
    AssertOnlyExpectedSerialChanged(
        buffer,
        before,
        DescriptorMinimumSize,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);

    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1,
        OriginalSerial,
        sizeof(OriginalSerial) - 1);
    assert(result == DeviceFilterRewriteApplied);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);
}

static void TestDoesNotChangeWrongDeviceOrSerial(void)
{
    unsigned char buffer[DescriptorReturnedBytes];
    unsigned char before[DescriptorReturnedBytes];
    char wrongExpected[sizeof(OriginalSerial)];
    DeviceFilterRewriteResult result;

    MakeDescriptor(buffer);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        1,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteDeviceMismatch);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    memcpy(wrongExpected, OriginalSerial, sizeof(OriginalSerial));
    wrongExpected[0] = '9';
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        wrongExpected,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteSerialMismatch);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);
}

static void TestRejectsMalformedDescriptorsWithoutMutation(void)
{
    unsigned char buffer[DescriptorReturnedBytes];
    unsigned char before[DescriptorReturnedBytes];
    DeviceFilterRewriteResult result;

    MakeDescriptor(buffer);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorMinimumSize - 1,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteMalformedDescriptor);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    MakeDescriptor(buffer);
    WriteU32LittleEndian(buffer, DescriptorSizeOffset, DescriptorMinimumSize - 1);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteMalformedDescriptor);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    MakeDescriptor(buffer);
    WriteU32LittleEndian(buffer, SerialOffsetField, DescriptorMinimumSize - 1);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteMalformedDescriptor);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    MakeDescriptor(buffer);
    WriteU32LittleEndian(buffer, SerialOffsetField, DescriptorTotalSize);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteMalformedDescriptor);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    MakeDescriptor(buffer);
    WriteU32LittleEndian(buffer, SerialOffsetField, 0);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteSerialAbsent);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    MakeDescriptor(buffer);
    WriteU32LittleEndian(buffer, SerialOffsetField, DescriptorTotalSize + 1);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteMalformedDescriptor);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    MakeDescriptor(buffer);
    memset(buffer + DescriptorSerialOffset, 'X', DescriptorTotalSize - DescriptorSerialOffset);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteMalformedDescriptor);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);

    MakeDescriptor(buffer);
    WriteU32LittleEndian(buffer, DescriptorSizeOffset, (uint32_t)sizeof(buffer) + 1);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        sizeof(buffer),
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        ReplacementSerial,
        sizeof(ReplacementSerial) - 1);
    assert(result == DeviceFilterRewriteMalformedDescriptor);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);
}

static void TestRejectsUnsafeReplacementShape(void)
{
    unsigned char buffer[DescriptorReturnedBytes];
    unsigned char before[DescriptorReturnedBytes];
    DeviceFilterRewriteResult result;

    MakeDescriptor(buffer);
    memcpy(before, buffer, sizeof(buffer));
    result = DeviceFilterRewriteStorageSerial(
        buffer,
        DescriptorReturnedBytes,
        0,
        OriginalSerial,
        sizeof(OriginalSerial) - 1,
        "longer replacement serial",
        sizeof("longer replacement serial") - 1);
    assert(result == DeviceFilterRewriteInvalidArgument);
    assert(memcmp(buffer, before, sizeof(buffer)) == 0);
}

int main(void)
{
    TestApplyAndRemoveRoundTrip();
    TestSerialOffsetAtHeaderBoundary();
    TestDoesNotChangeWrongDeviceOrSerial();
    TestRejectsMalformedDescriptorsWithoutMutation();
    TestRejectsUnsafeReplacementShape();
    puts("PASS: storage serial descriptor apply/remove and negative cases");
    return 0;
}
