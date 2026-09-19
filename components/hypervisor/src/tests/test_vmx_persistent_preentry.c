#include "vmx_persistent_preentry.h"
#include "vmx.h"

#include <stdint.h>

int main(void)
{
    uint64_t feature;
    unsigned int cet;
    unsigned int failures = 0;

    for (cet = 0; cet < 2; ++cet) {
        for (feature = 0; feature < 8; ++feature) {
            const uint64_t cr4 = cet ? CR4_CET : UINT64_C(0);
            const bool expected = cet == 0 &&
                (feature & UINT64_C(0x5)) == UINT64_C(0x5);
            if (VmxPersistentPreentryValidate(cr4, feature) != expected)
                ++failures;
        }
    }
    if (!VmxPersistentPreentryValidate(0, UINT64_C(0x8000000000000005)))
        ++failures;
    if (VmxPersistentPreentryValidate(CR4_CET, UINT64_MAX))
        ++failures;
    return failures == 0 ? 0 : 1;
}
