#ifndef HYPERSINV_VMX_PERSISTENT_PREENTRY_H
#define HYPERSINV_VMX_PERSISTENT_PREENTRY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure per-target gate: CET must be clear and FEATURE_CONTROL must permit VMX. */
bool VmxPersistentPreentryValidate(uint64_t cr4, uint64_t feature_control);

#ifdef __cplusplus
}
#endif
#endif
