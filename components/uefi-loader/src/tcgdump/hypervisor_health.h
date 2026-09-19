/*
 * hypervisor_health.h - Windows per-CPU health probe for ib-tcg-dump.
 *
 * The CPUID values below intentionally mirror the freestanding contract in
 * components/hypervisor/src/efi/efi_hypervisor.h.  Keep the two definitions
 * synchronized; this host tool must not introduce a cross-component include
 * dependency into the independent component builds.
 */
#ifndef TCGDUMP_HYPERVISOR_HEALTH_H
#define TCGDUMP_HYPERVISOR_HEALTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCGDUMP_HYPERVISOR_HEALTH_LEAF UINT32_C(0x48565031) /* "HVP1" */
#define TCGDUMP_HYPERVISOR_HEALTH_SIGNATURE UINT32_C(0x55454649) /* "UEFI" */
#define TCGDUMP_HYPERVISOR_HEALTH_VERSION UINT32_C(1)
#define TCGDUMP_HYPERVISOR_HEALTH_READY UINT32_C(1)
#define TCGDUMP_HYPERVISOR_HEALTH_PERSISTENT UINT32_C(2)

typedef struct tcgdump_hypervisor_health_regs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} tcgdump_hypervisor_health_regs_t;

typedef enum tcgdump_hypervisor_health_result {
    TCGDUMP_HYPERVISOR_HEALTH_ABSENT = 0,
    TCGDUMP_HYPERVISOR_HEALTH_PASS = 1,
    TCGDUMP_HYPERVISOR_HEALTH_FAIL = 2
} tcgdump_hypervisor_health_result_t;

/* Pure classification of one CPUID response; safe to use from host tests. */
tcgdump_hypervisor_health_result_t
tcgdump_hypervisor_health_classify(
    const tcgdump_hypervisor_health_regs_t *registers);

/*
 * Query one explicit Windows processor.  The current thread affinity is
 * saved, changed only for the single CPUID instruction, and restored before
 * return.  Returns zero when the bounded affinity operation cannot complete.
 */
int tcgdump_hypervisor_health_query_processor(
    uint16_t group,
    uint32_t processor,
    tcgdump_hypervisor_health_regs_t *registers);

/* Compatibility wrapper for processor group 0, processor 0. */
int tcgdump_hypervisor_health_query(
    tcgdump_hypervisor_health_regs_t *registers);

/* Print one result per active CPU followed by one aggregate result line. */
void tcgdump_hypervisor_health_report(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TCGDUMP_HYPERVISOR_HEALTH_H */
