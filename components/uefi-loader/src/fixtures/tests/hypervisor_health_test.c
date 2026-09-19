/* Pure contract tests for one Windows processor health response. */
#include <stdio.h>

#include "hypervisor_health.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        return 1; \
    } \
} while (0)

static tcgdump_hypervisor_health_regs_t valid_registers(void)
{
    tcgdump_hypervisor_health_regs_t registers;

    registers.eax = TCGDUMP_HYPERVISOR_HEALTH_VERSION;
    registers.ebx = TCGDUMP_HYPERVISOR_HEALTH_SIGNATURE;
    registers.ecx = TCGDUMP_HYPERVISOR_HEALTH_READY |
                    TCGDUMP_HYPERVISOR_HEALTH_PERSISTENT;
    registers.edx = 0u;
    return registers;
}

static int test_pass(void)
{
    tcgdump_hypervisor_health_regs_t registers = valid_registers();

    CHECK(tcgdump_hypervisor_health_classify(&registers) ==
          TCGDUMP_HYPERVISOR_HEALTH_PASS);
    registers.ecx |= UINT32_C(0x80000000);
    CHECK(tcgdump_hypervisor_health_classify(&registers) ==
          TCGDUMP_HYPERVISOR_HEALTH_PASS);
    return 0;
}

static int test_absent(void)
{
    tcgdump_hypervisor_health_regs_t registers = { 0u, 0u, 0u, 0u };

    CHECK(tcgdump_hypervisor_health_classify(&registers) ==
          TCGDUMP_HYPERVISOR_HEALTH_ABSENT);
    registers.ebx = UINT32_C(0x12345678);
    CHECK(tcgdump_hypervisor_health_classify(&registers) ==
          TCGDUMP_HYPERVISOR_HEALTH_ABSENT);
    CHECK(tcgdump_hypervisor_health_classify(NULL) ==
          TCGDUMP_HYPERVISOR_HEALTH_ABSENT);
    return 0;
}

static int test_fail(void)
{
    tcgdump_hypervisor_health_regs_t registers = valid_registers();

    registers.eax++;
    CHECK(tcgdump_hypervisor_health_classify(&registers) ==
          TCGDUMP_HYPERVISOR_HEALTH_FAIL);
    registers = valid_registers();
    registers.ecx &= ~TCGDUMP_HYPERVISOR_HEALTH_PERSISTENT;
    CHECK(tcgdump_hypervisor_health_classify(&registers) ==
          TCGDUMP_HYPERVISOR_HEALTH_FAIL);
    registers = valid_registers();
    registers.ecx &= ~TCGDUMP_HYPERVISOR_HEALTH_READY;
    CHECK(tcgdump_hypervisor_health_classify(&registers) ==
          TCGDUMP_HYPERVISOR_HEALTH_FAIL);
    return 0;
}

int main(void)
{
    if (test_pass() != 0 || test_absent() != 0 || test_fail() != 0) {
        return 1;
    }
    puts("PASS: Windows hypervisor health classification");
    return 0;
}
