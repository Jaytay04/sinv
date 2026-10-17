#include "nic_policy.h"

#ifdef NIC_POLICY_PROFILE_HEADER
#include NIC_POLICY_PROFILE_HEADER

static const NIC_POLICY_BINDING g_bindings[NIC_POLICY_BINDING_COUNT] =
    NIC_POLICY_PROFILE_BINDINGS;
#else
static const NIC_POLICY_BINDING g_bindings[NIC_POLICY_BINDING_COUNT] = {
    {
        "ROOT\\VMWARE\\0001",
        {0x00, 0x50, 0x56, 0xC0, 0x00, 0x08},
        {0x02, 0x90, 0x10, 0x20, 0x30, 0x40}
    },
    {
        "ROOT\\VMWARE\\0000",
        {0x00, 0x50, 0x56, 0xC0, 0x00, 0x01},
        {0x02, 0x90, 0x10, 0x20, 0x30, 0x41}
    },
    {
        "PCI\\VEN_8086&DEV_7AF0&SUBSYS_00748086&REV_11\\3&11583659&1&A3",
        {0xC4, 0xBD, 0xE5, 0x32, 0x06, 0xD1},
        {0x02, 0x90, 0x10, 0x20, 0x30, 0x42}
    },
    {
        "PCI\\VEN_10EC&DEV_8125&SUBSYS_7D371462&REV_05\\4&3FA6681&0&00E2",
        {0xD8, 0xBB, 0xC1, 0xD8, 0x57, 0x20},
        {0x02, 0x90, 0x10, 0x20, 0x30, 0x43}
    }
};
#endif

static unsigned char NicAsciiLower(unsigned char value)
{
    if (value >= (unsigned char)'A' && value <= (unsigned char)'Z') {
        return (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'));
    }
    return value;
}

static int NicAsciiEqualInsensitive(const char *left, const char *right)
{
    size_t index = 0;

    if (left == NULL || right == NULL) {
        return 0;
    }

    while (left[index] != '\0' && right[index] != '\0') {
        if (NicAsciiLower((unsigned char)left[index]) !=
            NicAsciiLower((unsigned char)right[index])) {
            return 0;
        }
        ++index;
    }

    return left[index] == '\0' && right[index] == '\0';
}

static int NicBytesEqual(
    const uint8_t left[NIC_POLICY_ADDRESS_LENGTH],
    const uint8_t right[NIC_POLICY_ADDRESS_LENGTH])
{
    size_t index;

    for (index = 0; index < NIC_POLICY_ADDRESS_LENGTH; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

size_t NicPolicyBindingCount(void)
{
    return NIC_POLICY_BINDING_COUNT;
}

const NIC_POLICY_BINDING *NicPolicyBindingAt(size_t index)
{
    return index < NIC_POLICY_BINDING_COUNT ? &g_bindings[index] : NULL;
}

int NicPolicyFindBinding(const char *instance_id, size_t *index_out)
{
    size_t index;

    if (instance_id == NULL || index_out == NULL) {
        return 0;
    }

    for (index = 0; index < NIC_POLICY_BINDING_COUNT; ++index) {
        if (NicAsciiEqualInsensitive(instance_id, g_bindings[index].instance_id)) {
            *index_out = index;
            return 1;
        }
    }
    return 0;
}

int NicPolicyMacIsValidUnicast(const uint8_t address[NIC_POLICY_ADDRESS_LENGTH])
{
    size_t index;
    int any_nonzero = 0;
    int all_ones = 1;

    if (address == NULL || (address[0] & 0x01u) != 0u) {
        return 0;
    }

    for (index = 0; index < NIC_POLICY_ADDRESS_LENGTH; ++index) {
        if (address[index] != 0u) {
            any_nonzero = 1;
        }
        if (address[index] != 0xFFu) {
            all_ones = 0;
        }
    }

    return any_nonzero && !all_ones;
}

int NicPolicyBindingsAreValid(void)
{
    size_t index;
    size_t other;

    for (index = 0; index < NIC_POLICY_BINDING_COUNT; ++index) {
        if (!NicPolicyMacIsValidUnicast(g_bindings[index].original) ||
            !NicPolicyMacIsValidUnicast(g_bindings[index].replacement) ||
            NicBytesEqual(g_bindings[index].original, g_bindings[index].replacement)) {
            return 0;
        }

        for (other = index + 1; other < NIC_POLICY_BINDING_COUNT; ++other) {
            if (NicAsciiEqualInsensitive(g_bindings[index].instance_id,
                                         g_bindings[other].instance_id)) {
                return 0;
            }
        }
    }

    return 1;
}

NIC_POLICY_RESULT NicPolicyTransformCurrentAddress(
    const char *instance_id,
    NIC_POLICY_MODE mode,
    uint8_t *buffer,
    size_t buffer_length)
{
    size_t index;
    const NIC_POLICY_BINDING *binding;

    if (mode != NIC_POLICY_DISABLED && mode != NIC_POLICY_ENABLED) {
        return NIC_POLICY_INVALID_MODE;
    }
    if (buffer == NULL || buffer_length < NIC_POLICY_ADDRESS_LENGTH) {
        return NIC_POLICY_SHORT_BUFFER;
    }
    if (!NicPolicyFindBinding(instance_id, &index)) {
        return NIC_POLICY_UNKNOWN_ADAPTER;
    }

    binding = &g_bindings[index];
    if (mode == NIC_POLICY_ENABLED) {
        if (NicBytesEqual(buffer, binding->original)) {
            for (index = 0; index < NIC_POLICY_ADDRESS_LENGTH; ++index) {
                buffer[index] = binding->replacement[index];
            }
            return NIC_POLICY_REPLACED;
        }
        return NIC_POLICY_NO_CHANGE;
    }

    if (NicBytesEqual(buffer, binding->replacement)) {
        for (index = 0; index < NIC_POLICY_ADDRESS_LENGTH; ++index) {
            buffer[index] = binding->original[index];
        }
        return NIC_POLICY_RESTORED;
    }
    return NIC_POLICY_NO_CHANGE;
}
