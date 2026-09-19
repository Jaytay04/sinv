/*
 * hypervisor_health.c - Windows per-CPU CPUID health probe for ib-tcg-dump.
 */
#include <stdio.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>

#include "hypervisor_health.h"

static uint32_t required_status(void)
{
    return TCGDUMP_HYPERVISOR_HEALTH_READY |
           TCGDUMP_HYPERVISOR_HEALTH_PERSISTENT;
}

tcgdump_hypervisor_health_result_t
tcgdump_hypervisor_health_classify(
    const tcgdump_hypervisor_health_regs_t *registers)
{
    if (registers == NULL ||
        registers->ebx != TCGDUMP_HYPERVISOR_HEALTH_SIGNATURE) {
        /* A missing contract signature means no loaded EFI hypervisor. */
        return TCGDUMP_HYPERVISOR_HEALTH_ABSENT;
    }

    if (registers->eax != TCGDUMP_HYPERVISOR_HEALTH_VERSION ||
        (registers->ecx & required_status()) != required_status()) {
        return TCGDUMP_HYPERVISOR_HEALTH_FAIL;
    }

    return TCGDUMP_HYPERVISOR_HEALTH_PASS;
}

int tcgdump_hypervisor_health_query_processor(
    uint16_t group,
    uint32_t processor,
    tcgdump_hypervisor_health_regs_t *registers)
{
    GROUP_AFFINITY previous;
    GROUP_AFFINITY target;
    int restored;
    int values[4];

    if (registers == NULL) {
        return 0;
    }
    registers->eax = 0;
    registers->ebx = 0;
    registers->ecx = 0;
    registers->edx = 0;

    if (processor >= GetActiveProcessorCount((WORD)group) ||
        processor >= sizeof(KAFFINITY) * 8u) {
        return 0;
    }

    /* Keep each affinity window bounded to one CPUID instruction. */
    ZeroMemory(&previous, sizeof(previous));
    ZeroMemory(&target, sizeof(target));
    target.Mask = ((KAFFINITY)1) << processor;
    target.Group = (WORD)group;

    if (!GetThreadGroupAffinity(GetCurrentThread(), &previous)) {
        return 0;
    }
    if (!SetThreadGroupAffinity(GetCurrentThread(), &target, NULL)) {
        return 0;
    }

    __cpuidex(values, (int)TCGDUMP_HYPERVISOR_HEALTH_LEAF, 0);
    registers->eax = (uint32_t)values[0];
    registers->ebx = (uint32_t)values[1];
    registers->ecx = (uint32_t)values[2];
    registers->edx = (uint32_t)values[3];

    restored = SetThreadGroupAffinity(GetCurrentThread(), &previous, NULL)
               ? 1 : 0;
    return restored;
}

int tcgdump_hypervisor_health_query(
    tcgdump_hypervisor_health_regs_t *registers)
{
    return tcgdump_hypervisor_health_query_processor(0, 0, registers);
}

static const char *result_name(tcgdump_hypervisor_health_result_t result)
{
    switch (result) {
    case TCGDUMP_HYPERVISOR_HEALTH_PASS:
        return "PASS";
    case TCGDUMP_HYPERVISOR_HEALTH_FAIL:
        return "FAIL";
    case TCGDUMP_HYPERVISOR_HEALTH_ABSENT:
    default:
        return "ABSENT";
    }
}

void tcgdump_hypervisor_health_report(void)
{
    tcgdump_hypervisor_health_regs_t registers;
    tcgdump_hypervisor_health_result_t aggregate =
        TCGDUMP_HYPERVISOR_HEALTH_ABSENT;
    uint32_t total = 0;
    uint32_t passes = 0;
    uint32_t failures = 0;
    WORD group;
    WORD groupCount = GetActiveProcessorGroupCount();

    for (group = 0; group < groupCount; ++group) {
        DWORD processor;
        DWORD processorCount = GetActiveProcessorCount(group);
        for (processor = 0; processor < processorCount; ++processor) {
            tcgdump_hypervisor_health_result_t result;
            if (!tcgdump_hypervisor_health_query_processor(
                    group, processor, &registers)) {
                result = TCGDUMP_HYPERVISOR_HEALTH_FAIL;
            } else {
                result = tcgdump_hypervisor_health_classify(&registers);
            }
            printf("UEFI_HYPERVISOR_WINDOWS_CPU:%u:%lu:%s\n",
                   (unsigned)group, (unsigned long)processor,
                   result_name(result));
            fflush(stdout);
            ++total;
            if (result == TCGDUMP_HYPERVISOR_HEALTH_PASS) {
                ++passes;
            } else if (result == TCGDUMP_HYPERVISOR_HEALTH_FAIL) {
                ++failures;
            }
        }
    }

    if (total == 0 || failures != 0 || (passes != 0 && passes != total)) {
        aggregate = TCGDUMP_HYPERVISOR_HEALTH_FAIL;
    } else if (passes == total) {
        aggregate = TCGDUMP_HYPERVISOR_HEALTH_PASS;
    }
    printf("UEFI_HYPERVISOR_WINDOWS_CPUID:%s\n", result_name(aggregate));
    fflush(stdout);
}
