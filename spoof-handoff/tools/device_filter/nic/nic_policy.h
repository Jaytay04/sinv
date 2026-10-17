#ifndef HYPERSINV_NIC_POLICY_H
#define HYPERSINV_NIC_POLICY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NIC_POLICY_ADDRESS_LENGTH 6u
#define NIC_POLICY_BINDING_COUNT 4u

typedef enum NIC_POLICY_MODE {
    NIC_POLICY_DISABLED = 0,
    NIC_POLICY_ENABLED = 1
} NIC_POLICY_MODE;

typedef enum NIC_POLICY_RESULT {
    NIC_POLICY_NO_CHANGE = 0,
    NIC_POLICY_REPLACED = 1,
    NIC_POLICY_RESTORED = 2,
    NIC_POLICY_UNKNOWN_ADAPTER = 3,
    NIC_POLICY_SHORT_BUFFER = 4,
    NIC_POLICY_MISMATCH = 5,
    NIC_POLICY_INVALID_MODE = 6
} NIC_POLICY_RESULT;

typedef struct NIC_POLICY_BINDING {
    const char *instance_id;
    uint8_t original[NIC_POLICY_ADDRESS_LENGTH];
    uint8_t replacement[NIC_POLICY_ADDRESS_LENGTH];
} NIC_POLICY_BINDING;

size_t NicPolicyBindingCount(void);
const NIC_POLICY_BINDING *NicPolicyBindingAt(size_t index);
int NicPolicyFindBinding(const char *instance_id, size_t *index_out);
int NicPolicyMacIsValidUnicast(const uint8_t address[NIC_POLICY_ADDRESS_LENGTH]);
int NicPolicyBindingsAreValid(void);

/*
 * Transforms only the first six bytes after an exact adapter identity and
 * original/replacement guard. Bytes beyond the address are never changed.
 * DISABLED restores only an exact replacement; ENABLED replaces only the
 * captured original. Unknown or unexpected values pass through unchanged.
 */
NIC_POLICY_RESULT NicPolicyTransformCurrentAddress(
    const char *instance_id,
    NIC_POLICY_MODE mode,
    uint8_t *buffer,
    size_t buffer_length);

#ifdef __cplusplus
}
#endif

#endif
