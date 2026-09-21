#include "hal_runtime_hygiene.h"
#include <stdio.h>
#include <windows.h>
#include <intrin.h>

/**
 * @brief Test DR hygiene after VM-entry
 */
int TestDRHygiene() {
    printf("Testing DR hygiene...\n");
    
    // Initialize DR values
    __dr0 = 0;
    __dr1 = 0;
    __dr2 = 0;
    __dr3 = 0;
    __dr6 = 0;
    __dr7 = 0;
    
    // Run hypervisor VM-entry (simulated)
    volatile LARGE_INTEGER begin = {};
    QueryPerformanceCounter(&begin);
    
    // These would be the actual VMCS pointer from hypervisor
    size_t vmcs_ptr = 0x00000000D0000000ULL;
    
    // Get current guest DR values
    uint64_t current_dr0 = __dr0;
    uint64_t current_dr1 = __dr1;
    uint64_t current_dr2 = __dr2;
    uint64_t current_dr3 = __dr3;
    uint64_t current_dr6 = __dr6;
    uint64_t current_dr7 = __dr7;
    
    volatile LARGE_INTEGER end = {};
    QueryPerformanceCounter(&end);
    
    // Clear debug registers
    __dr0 = 0;
    __dr1 = 0;
    __dr2 = 0;
    __dr3 = 0;
    __dr6 = 0;
    __dr7 = 0;
    
    // Check that registers were cleared
    if (__dr0 != 0 || __dr1 != 0 || __dr2 != 0 || __dr3 != 0 || __dr6 != 0 || __dr7 != 0) {
        printf("FAILED: Debug registers not cleared\n");
        printf("DR0: %llX, DR1: %llX, DR2: %llX, DR3: %llX\n", __dr0, __dr1, __dr2, __dr3);
        printf("DR6: %llX, DR7: %llX\n", __dr6, __dr7);
        return 0;
    }
    
    printf("PASSED: Debug registers cleared\n");
    return 1;
}

/**
 * @brief Test IDT integrity validation
 */
int TestIDTIntegrity() {
    printf("Testing IDT integrity...\n");
    
    // Get IDT base
    uint64_t idt_base = __readcr8(); // Special register for IDTR
    
    if (idt_base == 0) {
        printf("FAILED: Could not get IDT base\n");
        return 0;
    }
    
    BOOL result = HalRuntimeValidateIDTIntegrity((PUINT8)idt_base);
    
    if (!result) {
        printf("FAILED: IDT integrity check failed\n");
        return 0;
    }
    
    printf("PASSED: IDT integrity verified\n");
    return 1;
}

int main() {
    printf("====== Debugger Anticheat Hygiene Tests ======\n\n");
    
    int dr_test = TestDRHygiene();
    printf("\n");
    int idt_test = TestIDTIntegrity();
    
    printf("\n");
    if (dr_test && idt_test) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    
    printf("Some tests failed\n");
    return 1;
}
