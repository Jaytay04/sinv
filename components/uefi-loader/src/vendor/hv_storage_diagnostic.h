#ifndef HV_STORAGE_DIAGNOSTIC_H
#define HV_STORAGE_DIAGNOSTIC_H

#include <stdint.h>

/* Native diagnostic build only. Subleaf zero triggers one read-only snapshot.
 * Later subleaves return that cached snapshot without touching hardware. */
#define HV_STORAGE_DIAGNOSTIC_LEAF UINT32_C(0x48564431) /* HVD1 */
#define HV_STORAGE_DIAGNOSTIC_SIGNATURE UINT32_C(0x53544731) /* STG1 */
#define HV_STORAGE_DIAGNOSTIC_VERSION UINT32_C(1)
#define HV_STORAGE_DIAGNOSTIC_UNIT_BASE UINT32_C(0x100)
#define HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE UINT32_C(4)
#define HV_STORAGE_DIAGNOSTIC_MAX_UNITS UINT32_C(8)

/* Header EDX and target subleaf 2 ECX flags. */
#define HV_STORAGE_DIAGNOSTIC_BASELINE_CAPTURED (UINT32_C(1) << 0)
#define HV_STORAGE_DIAGNOSTIC_FAILURE_CAPTURED (UINT32_C(1) << 1)
#define HV_STORAGE_DIAGNOSTIC_TARGET_VALID (UINT32_C(1) << 2)
#define HV_STORAGE_DIAGNOSTIC_TARGET_ECAM_VALID (UINT32_C(1) << 3)
#define HV_STORAGE_DIAGNOSTIC_TARGET_LIVE_VALID (UINT32_C(1) << 4)
#define HV_STORAGE_DIAGNOSTIC_TARGET_RESTORE_MATCH (UINT32_C(1) << 5)
#define HV_STORAGE_DIAGNOSTIC_TARGET_RESTORE_ABSENT (UINT32_C(1) << 6)
#define HV_STORAGE_DIAGNOSTIC_OUTER_UNAVAILABLE (UINT32_C(1) << 7)

/* Per-unit subleaf 2 ECX flags; missing data is never encoded as zero data. */
#define HV_STORAGE_DIAGNOSTIC_UNIT_BASELINE_FSTS (UINT32_C(1) << 0)
#define HV_STORAGE_DIAGNOSTIC_UNIT_BASELINE_GSTS (UINT32_C(1) << 1)
#define HV_STORAGE_DIAGNOSTIC_UNIT_BASELINE_FRCD (UINT32_C(1) << 2)
#define HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FSTS (UINT32_C(1) << 3)
#define HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_GSTS (UINT32_C(1) << 4)
#define HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD (UINT32_C(1) << 5)
#define HV_STORAGE_DIAGNOSTIC_UNIT_FRO_VALID (UINT32_C(1) << 6)
#define HV_STORAGE_DIAGNOSTIC_UNIT_FRI_VALID (UINT32_C(1) << 7)
#define HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED (UINT32_C(1) << 8)
#define HV_STORAGE_DIAGNOSTIC_UNIT_WINDOW_BUSY (UINT32_C(1) << 9)
#define HV_STORAGE_DIAGNOSTIC_UNIT_CAP_CHANGED (UINT32_C(1) << 10)

/* 0: STG1, version, unit count, global flags.
 * 1: BDF (segment:bus:device:function = 16:8:5:3), vendor|device,
 *    class|subclass|progif|revision, pre-command|restored-command.
 * 2: live command, live vendor|device, target flags, reserved.
 * 0x100+4*i+0: baseline FSTS, failure FSTS, baseline GSTS, failure GSTS.
 * +1: failure FRCD low/high as four little-endian 32-bit words.
 * +2: cached CAP low/high, validity flags, FRO(byte offset)|FRI<<16.
 * +3: baseline FRCD low/high as four little-endian 32-bit words. */

#endif
