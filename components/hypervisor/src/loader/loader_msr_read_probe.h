#ifndef HYPERSINV_LOADER_MSR_READ_PROBE_H
#define HYPERSINV_LOADER_MSR_READ_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum LOADER_MSR_READ_PROBE_RESULT {
    LOADER_MSR_READ_PROBE_PASS = 0,
    LOADER_MSR_READ_PROBE_FEATURE_CONTROL_VISIBLE = 1,
    LOADER_MSR_READ_PROBE_VMX_BASIC_VISIBLE = 2,
    LOADER_MSR_READ_PROBE_USER_CET_VISIBLE = 3,
    LOADER_MSR_READ_PROBE_NATIVE_EFER_INVALID = 4,
    LOADER_MSR_READ_PROBE_NATIVE_EFER_CHANGED = 5
} LOADER_MSR_READ_PROBE_RESULT;

LOADER_MSR_READ_PROBE_RESULT LoaderMsrReadProbeClassify(
    uint64_t FeatureControl,
    uint64_t VmxBasic,
    uint64_t UserCet,
    uint64_t NativeEferBefore,
    uint64_t NativeEferAfter);

#ifdef __cplusplus
}
#endif

#endif
