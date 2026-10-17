#include "nic_policy.h"

#include <stdio.h>
#include <string.h>

static const uint8_t g_expected_originals[NIC_POLICY_BINDING_COUNT][NIC_POLICY_ADDRESS_LENGTH] = {
    {0x00, 0x50, 0x56, 0xC0, 0x00, 0x08},
    {0x00, 0x50, 0x56, 0xC0, 0x00, 0x01},
    {0xC4, 0xBD, 0xE5, 0x32, 0x06, 0xD1},
    {0xD8, 0xBB, 0xC1, 0xD8, 0x57, 0x20}
};

static const uint8_t g_expected_replacements[NIC_POLICY_BINDING_COUNT][NIC_POLICY_ADDRESS_LENGTH] = {
    {0x02, 0x90, 0x10, 0x20, 0x30, 0x40},
    {0x02, 0x90, 0x10, 0x20, 0x30, 0x41},
    {0x02, 0x90, 0x10, 0x20, 0x30, 0x42},
    {0x02, 0x90, 0x10, 0x20, 0x30, 0x43}
};

static int Fail(const char *message, size_t index)
{
    (void)fprintf(stderr, "FAIL adapter[%zu]: %s\n", index, message);
    return 0;
}

static int TestBindingRoundTrip(size_t index)
{
    const NIC_POLICY_BINDING *binding = NicPolicyBindingAt(index);
    uint8_t response[NIC_POLICY_ADDRESS_LENGTH + 1u];
    uint8_t original_with_sentinel[NIC_POLICY_ADDRESS_LENGTH + 1u];
    size_t resolved_index = NIC_POLICY_BINDING_COUNT;

    if (binding == NULL || binding->instance_id == NULL) {
        return Fail("binding missing", index);
    }
    if (memcmp(binding->original, g_expected_originals[index], NIC_POLICY_ADDRESS_LENGTH) != 0 ||
        memcmp(binding->replacement, g_expected_replacements[index], NIC_POLICY_ADDRESS_LENGTH) != 0) {
        return Fail("source values differ from the captured TSV", index);
    }
    if (!NicPolicyFindBinding(binding->instance_id, &resolved_index) || resolved_index != index) {
        return Fail("exact PNP instance identity did not resolve", index);
    }

    memcpy(response, binding->original, NIC_POLICY_ADDRESS_LENGTH);
    response[NIC_POLICY_ADDRESS_LENGTH] = 0xA5u;
    memcpy(original_with_sentinel, response, sizeof(response));

    if (NicPolicyTransformCurrentAddress(binding->instance_id,
                                          NIC_POLICY_DISABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_NO_CHANGE ||
        memcmp(response, original_with_sentinel, sizeof(response)) != 0) {
        return Fail("disabled mode changed a captured original", index);
    }

    if (NicPolicyTransformCurrentAddress(binding->instance_id,
                                          NIC_POLICY_ENABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_REPLACED ||
        memcmp(response, binding->replacement, NIC_POLICY_ADDRESS_LENGTH) != 0 ||
        response[NIC_POLICY_ADDRESS_LENGTH] != 0xA5u) {
        return Fail("enabled mode did not replace only the six-byte current address", index);
    }

    if (NicPolicyTransformCurrentAddress(binding->instance_id,
                                          NIC_POLICY_DISABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_RESTORED ||
        memcmp(response, original_with_sentinel, sizeof(response)) != 0) {
        return Fail("disabled mode did not restore the exact captured original", index);
    }

    return 1;
}

static int TestRejectedInputs(void)
{
    uint8_t multicast[NIC_POLICY_ADDRESS_LENGTH] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    uint8_t zero[NIC_POLICY_ADDRESS_LENGTH] = {0, 0, 0, 0, 0, 0};
    uint8_t broadcast[NIC_POLICY_ADDRESS_LENGTH] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t response[NIC_POLICY_ADDRESS_LENGTH] = {0x00, 0x50, 0x56, 0xC0, 0x00, 0x08};
    uint8_t saved[NIC_POLICY_ADDRESS_LENGTH];

    memcpy(saved, response, sizeof(response));
    if (NicPolicyMacIsValidUnicast(multicast) ||
        NicPolicyMacIsValidUnicast(zero) ||
        NicPolicyMacIsValidUnicast(broadcast)) {
        (void)fprintf(stderr, "FAIL malformed/group MAC validation\n");
        return 0;
    }
    if (NicPolicyTransformCurrentAddress("ROOT\\VMWARE\\0002",
                                          NIC_POLICY_ENABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_UNKNOWN_ADAPTER ||
        memcmp(response, saved, sizeof(response)) != 0) {
        (void)fprintf(stderr, "FAIL unbound adapter modified a response\n");
        return 0;
    }
    if (NicPolicyTransformCurrentAddress("ROOT\\VMWARE\\0001",
                                          NIC_POLICY_ENABLED,
                                          response,
                                          sizeof(response) - 1u) != NIC_POLICY_SHORT_BUFFER ||
        memcmp(response, saved, sizeof(response)) != 0) {
        (void)fprintf(stderr, "FAIL short response buffer was modified\n");
        return 0;
    }
    if (NicPolicyTransformCurrentAddress("ROOT\\VMWARE\\0001",
                                          (NIC_POLICY_MODE)9,
                                          response,
                                          sizeof(response)) != NIC_POLICY_INVALID_MODE ||
        memcmp(response, saved, sizeof(response)) != 0) {
        (void)fprintf(stderr, "FAIL invalid mode was accepted\n");
        return 0;
    }
    response[5] ^= 0x80u;
    memcpy(saved, response, sizeof(response));
    if (NicPolicyTransformCurrentAddress("ROOT\\VMWARE\\0001",
                                          NIC_POLICY_ENABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_NO_CHANGE ||
        memcmp(response, saved, sizeof(response)) != 0) {
        (void)fprintf(stderr, "FAIL unexpected native value bypassed the original-value guard\n");
        return 0;
    }
    return 1;
}

int main(void)
{
    size_t index;

    if (NicPolicyBindingCount() != NIC_POLICY_BINDING_COUNT ||
        !NicPolicyBindingsAreValid()) {
        (void)fprintf(stderr, "FAIL binding table format/uniqueness validation\n");
        return 1;
    }

    for (index = 0; index < NIC_POLICY_BINDING_COUNT; ++index) {
        if (!TestBindingRoundTrip(index)) {
            return 1;
        }
        (void)printf("PASS adapter[%zu] original -> spoofed -> restored\n", index);
    }

    if (!TestRejectedInputs()) {
        return 1;
    }
    (void)puts("PASS malformed MAC, wrong adapter, short buffer, invalid mode, and mismatched original guards");
    return 0;
}
