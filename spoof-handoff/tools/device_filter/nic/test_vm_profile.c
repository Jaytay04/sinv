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

static const char *const g_host_instance_ids[NIC_POLICY_BINDING_COUNT] = {
    "ROOT\\VMWARE\\0001",
    "ROOT\\VMWARE\\0000",
    "PCI\\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\\3&11583659&1&A3",
    "PCI\\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\\4&3FA6681&0&00E2"
};

static int Fail(const char *message, size_t index)
{
    (void)fprintf(stderr, "FAIL VM adapter[%zu]: %s\n", index, message);
    return 0;
}

static int TestVmBindingRoundTrip(size_t index)
{
    const NIC_POLICY_BINDING *binding = NicPolicyBindingAt(index);
    uint8_t response[NIC_POLICY_ADDRESS_LENGTH + 1u];
    uint8_t original_with_sentinel[NIC_POLICY_ADDRESS_LENGTH + 1u];
    size_t resolved_index = NIC_POLICY_BINDING_COUNT;
    size_t other;

    if (binding == NULL || binding->instance_id == NULL) {
        return Fail("profile binding missing", index);
    }
    if (memcmp(binding->original, g_expected_originals[index], NIC_POLICY_ADDRESS_LENGTH) != 0 ||
        memcmp(binding->replacement, g_expected_replacements[index], NIC_POLICY_ADDRESS_LENGTH) != 0) {
        return Fail("profile values differ from the approved client-originals.tsv mapping", index);
    }
    if (!NicPolicyMacIsValidUnicast(binding->original) ||
        !NicPolicyMacIsValidUnicast(binding->replacement) ||
        memcmp(binding->original, binding->replacement, NIC_POLICY_ADDRESS_LENGTH) == 0) {
        return Fail("profile contains an invalid or unchanged unicast address", index);
    }
    for (other = 0; other < NIC_POLICY_BINDING_COUNT; ++other) {
        if (NicPolicyFindBinding(g_host_instance_ids[other], &resolved_index)) {
            return Fail("generated guest profile contains a production host PnP pin", index);
        }
    }
    if (!NicPolicyFindBinding(binding->instance_id, &resolved_index) || resolved_index != index) {
        return Fail("guest PnP identity did not resolve to its profile binding", index);
    }

    memcpy(response, binding->original, NIC_POLICY_ADDRESS_LENGTH);
    response[NIC_POLICY_ADDRESS_LENGTH] = 0xA5u;
    memcpy(original_with_sentinel, response, sizeof(response));
    if (NicPolicyTransformCurrentAddress(binding->instance_id,
                                          NIC_POLICY_DISABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_NO_CHANGE ||
        memcmp(response, original_with_sentinel, sizeof(response)) != 0) {
        return Fail("disabled profile changed the original", index);
    }
    if (NicPolicyTransformCurrentAddress(binding->instance_id,
                                          NIC_POLICY_ENABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_REPLACED ||
        memcmp(response, binding->replacement, NIC_POLICY_ADDRESS_LENGTH) != 0 ||
        response[NIC_POLICY_ADDRESS_LENGTH] != 0xA5u) {
        return Fail("enabled profile did not replace only the six-byte address", index);
    }
    if (NicPolicyTransformCurrentAddress(binding->instance_id,
                                          NIC_POLICY_DISABLED,
                                          response,
                                          sizeof(response)) != NIC_POLICY_RESTORED ||
        memcmp(response, original_with_sentinel, sizeof(response)) != 0) {
        return Fail("disabled profile did not restore the original", index);
    }
    return 1;
}

static int TestProfileIdentityUniqueness(void)
{
    size_t index;
    size_t other;
    const NIC_POLICY_BINDING *left;
    const NIC_POLICY_BINDING *right;

    for (index = 0; index < NIC_POLICY_BINDING_COUNT; ++index) {
        left = NicPolicyBindingAt(index);
        if (left == NULL || left->instance_id == NULL || left->instance_id[0] == '\0') {
            return Fail("profile PnP identity is empty", index);
        }
        for (other = index + 1; other < NIC_POLICY_BINDING_COUNT; ++other) {
            right = NicPolicyBindingAt(other);
            if (right == NULL || right->instance_id == NULL ||
                _stricmp(left->instance_id, right->instance_id) == 0) {
                return Fail("profile PnP identities are not unique", index);
            }
        }
    }
    return 1;
}

int main(void)
{
    size_t index;

    if (NicPolicyBindingCount() != NIC_POLICY_BINDING_COUNT ||
        !NicPolicyBindingsAreValid() ||
        !TestProfileIdentityUniqueness()) {
        (void)fprintf(stderr, "FAIL VM profile count, validity, or identity uniqueness\n");
        return 1;
    }
    for (index = 0; index < NIC_POLICY_BINDING_COUNT; ++index) {
        if (!TestVmBindingRoundTrip(index)) {
            return 1;
        }
        (void)printf("PASS VM adapter[%zu] original -> spoofed -> restored\n", index);
    }
    return 0;
}
