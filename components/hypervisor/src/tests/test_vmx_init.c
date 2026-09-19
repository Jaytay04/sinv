/*
 * test_vmx_init.c — Unit tests for VMX initialization pure logic
 *
 * Tests revision ID extraction, region alignment, and VMCS field encoding
 * validation. Uses mocked MSR/CPUID values — no hardware dependencies.
 */

#include "test_common.h"
#include "vmx.h"

/*===========================================================================
 * VMCS Revision ID Extraction
 *===========================================================================*/

static int test_revision_extract_standard(void) {
    /* Simulated IA32_VMX_BASIC value: revision=5, vmcs_size=1024, type=6 */
    uint64_t vmx_basic = 0x0000001A80000005ULL;
    /* bits 30:0 = 0x5, bits 40:32 = 1024/4-1=255=0xFF, bits 53:50=6 */
    uint32_t rev = VmxExtractRevisionId(vmx_basic);
    TEST_ASSERT_UINT_EQ(rev, 5, "Standard revision ID 5");
    return 0;
}

static int test_revision_extract_zero(void) {
    uint64_t vmx_basic = 0;
    uint32_t rev = VmxExtractRevisionId(vmx_basic);
    TEST_ASSERT_UINT_EQ(rev, 0, "Zero revision ID");
    return 0;
}

static int test_revision_extract_max(void) {
    uint64_t vmx_basic = 0x0000000000000001ULL;
    uint32_t rev = VmxExtractRevisionId(vmx_basic);
    TEST_ASSERT_UINT_EQ(rev, 1, "Revision ID minimum non-zero");
    return 0;
}

static int test_revision_extract_large(void) {
    /* Max revision = 0x7FFFFFFF (all 31 bits set) */
    uint64_t vmx_basic = 0x00000000007FFFFFFFULL;
    uint32_t rev = VmxExtractRevisionId(vmx_basic);
    TEST_ASSERT_UINT_EQ(rev, 0x7FFFFFFF, "Max revision (all 31 bits)");
    return 0;
}

static int test_revision_extract_upper_bits_masked(void) {
    /* Bit 31 should be masked out (bit 31 = 1, bits 30:0 = 1) */
    uint64_t vmx_basic = 0x0000000080000001ULL;
    uint32_t rev = VmxExtractRevisionId(vmx_basic);
    TEST_ASSERT_UINT_EQ(rev, 1, "Upper bits masked out");
    return 0;
}

/*===========================================================================
 * VMX Region Alignment Check
 *===========================================================================*/

static int test_region_aligned_4k(void) {
    /* Simulate a 4K-aligned address */
    void* region = (void*)0xFFFF800000000000ULL;
    bool aligned = VmxIsRegionAligned(region);
    TEST_ASSERT_TRUE(aligned, "4KB-aligned address");
    return 0;
}

static int test_region_aligned_8k(void) {
    void* region = (void*)0xFFFF800000002000ULL;
    bool aligned = VmxIsRegionAligned(region);
    TEST_ASSERT_TRUE(aligned, "8KB-aligned address (still page-aligned)");
    return 0;
}

static int test_region_misaligned_1byte(void) {
    void* region = (void*)0xFFFF800000000001ULL;
    bool aligned = VmxIsRegionAligned(region);
    TEST_ASSERT_FALSE(aligned, "1-byte misaligned");
    return 0;
}

static int test_region_misaligned_4k_minus_1(void) {
    void* region = (void*)0xFFFF800000000FFFULL;
    bool aligned = VmxIsRegionAligned(region);
    TEST_ASSERT_FALSE(aligned, "4KB-1 misaligned");
    return 0;
}

static int test_region_misaligned_half_page(void) {
    void* region = (void*)0xFFFF800000000800ULL;
    bool aligned = VmxIsRegionAligned(region);
    TEST_ASSERT_FALSE(aligned, "Half-page (2KB) misaligned");
    return 0;
}

static int test_region_null_pointer(void) {
    /* NULL is technically "aligned" to 4KB (0 mod 4096 = 0) but it's
     * semantically invalid — we should verify this doesn't crash.
     * The alignment check is purely mathematical. */
    bool aligned = VmxIsRegionAligned(NULL);
    TEST_ASSERT_TRUE(aligned, "NULL pointer arithmetic (0 is aligned)");
    return 0;
}

/*===========================================================================
 * VMCS Field Encoding Validation
 *===========================================================================*/

static int test_vmcs_encoding_width_16bit(void) {
    /* 16-bit fields use encoding bits 14:13 = 00.
     * Example: IA32_VMX_VPID (0x00000000) has width = 0 */
    uint32_t w = VmcsFieldEncoding_Width(VMCS_CTRL_VPID);
    TEST_ASSERT_UINT_EQ(w, 0, "16-bit control field width (VPID encoding)");
    return 0;
}

static int test_vmcs_encoding_width_64bit(void) {
    /* VMCS_CTRL_MSR_BITMAP_ADDR = 0x00002004 (bits 14:13 = 01 = 64-bit) */
    uint32_t w = VmcsFieldEncoding_Width(VMCS_CTRL_MSR_BITMAP_ADDR);
    TEST_ASSERT_UINT_EQ(w, 1, "64-bit control field width");
    return 0;
}

static int test_vmcs_encoding_width_32bit(void) {
    /* VMCS_GUEST_CS_ACCESS = 0x00004816 (bits 14:13 = 10 = 32-bit) */
    uint32_t w = VmcsFieldEncoding_Width(VMCS_GUEST_CS_ACCESS);
    TEST_ASSERT_UINT_EQ(w, 2, "32-bit guest field width");
    return 0;
}

static int test_vmcs_encoding_width_natural(void) {
    /* VMCS_GUEST_CR0 = 0x00006800 (bits 14:13 = 11 = natural-width) */
    uint32_t w = VmcsFieldEncoding_Width(VMCS_GUEST_CR0);
    TEST_ASSERT_UINT_EQ(w, 3, "Natural-width guest field width");
    return 0;
}

static int test_vmcs_encoding_type_control(void) {
    /* bits 11:10 = 00 → control */
    uint32_t t = VmcsFieldEncoding_Type(VMCS_CTRL_PIN_BASED);
    TEST_ASSERT_UINT_EQ(t, 0, "Control field type");
    return 0;
}

static int test_vmcs_encoding_type_read_only(void) {
    /* bits 11:10 = 01 → VM-exit read-only */
    uint32_t t1 = VmcsFieldEncoding_Type(0x00004400);
    TEST_ASSERT_UINT_EQ(t1, 1, "Read-only field type (VM-exit info)");
    return 0;
}

static int test_vmcs_encoding_type_guest(void) {
    /* bits 11:10 = 10 → guest-state */
    uint32_t t = VmcsFieldEncoding_Type(VMCS_GUEST_CR0);
    TEST_ASSERT_UINT_EQ(t, 2, "Guest-state field type");
    return 0;
}

static int test_vmcs_encoding_type_host(void) {
    /* bits 11:10 = 11 → host-state */
    uint32_t t = VmcsFieldEncoding_Type(VMCS_HOST_CR0);
    TEST_ASSERT_UINT_EQ(t, 3, "Host-state field type");
    return 0;
}

static int test_vmcs_encoding_index_pin_based(void) {
    /* VMCS_CTRL_PIN_BASED = 0x00004000, index = bits 10:1 = 0 */
    uint32_t idx = VmcsFieldEncoding_Index(VMCS_CTRL_PIN_BASED);
    TEST_ASSERT_UINT_EQ(idx, 0, "Pin-based controls index");
    return 0;
}

static int test_vmcs_encoding_index_guest_rip(void) {
    /* VMCS_GUEST_RIP = 0x0000681E, index = bits 10:1 = 0x00F */
    uint32_t idx = VmcsFieldEncoding_Index(VMCS_GUEST_RIP);
    TEST_ASSERT_UINT_EQ(idx, 0x000F, "Guest RIP index");
    return 0;
}

static int test_vmcs_encoding_invalid_bit15_set(void) {
    /* Bit 15 must be 0 for valid VMCS encodings */
    bool valid = VmcsFieldEncoding_IsValid(0x00008000);
    TEST_ASSERT_FALSE(valid, "Invalid encoding (bit 15 set)");
    return 0;
}

static int test_vmcs_encoding_valid(void) {
    bool valid = VmcsFieldEncoding_IsValid(VMCS_GUEST_RIP);
    TEST_ASSERT_TRUE(valid, "Valid encoding (guest RIP)");
    return 0;
}

/*===========================================================================
 * VMX Instruction Error Codes
 *===========================================================================*/

static int test_vmx_error_codes_negative(void) {
    /* Verify error codes are negative */
    TEST_ASSERT_INT_EQ(VMX_SUCCESS, 0, "VMX_SUCCESS is 0");
    TEST_ASSERT_TRUE(VMX_ERROR_INVALID < 0, "VMX_ERROR_INVALID is negative");
    TEST_ASSERT_TRUE(VMX_ERROR_VMXON < 0, "VMX_ERROR_VMXON is negative");
    TEST_ASSERT_TRUE(VMX_ERROR_ALIGNMENT < 0, "VMX_ERROR_ALIGNMENT is negative");
    return 0;
}

/*===========================================================================
 * Known VMCS Field Encoding Values (Sanity Checks)
 *
 * These verify that the #define constants match what Intel's SDM specifies.
 * This is a documentation test — catches copy-paste errors in constants.
 *===========================================================================*/

static int test_vmcs_field_value_ctrl_pin_based(void) {
    TEST_ASSERT_UINT_EQ(VMCS_CTRL_PIN_BASED, 0x00004000u, "PIN_BASED encoding");
    return 0;
}

static int test_vmcs_field_value_ctrl_cpu_based(void) {
    TEST_ASSERT_UINT_EQ(VMCS_CTRL_CPU_BASED, 0x00004002u, "CPU_BASED encoding");
    return 0;
}

static int test_vmcs_field_value_guest_rip(void) {
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_RIP, 0x0000681Eu, "GUEST_RIP encoding");
    return 0;
}

static int test_vmcs_field_value_guest_rsp(void) {
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_RSP, 0x0000681Cu, "GUEST_RSP encoding");
    return 0;
}

static int test_vmcs_field_value_guest_cr0(void) {
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_CR0, 0x00006800u, "GUEST_CR0 encoding");
    return 0;
}

static int test_vmcs_field_value_host_rip(void) {
    TEST_ASSERT_UINT_EQ(VMCS_HOST_RIP, 0x00006C16u, "HOST_RIP encoding");
    return 0;
}

static int test_vmcs_field_value_eptp(void) {
    TEST_ASSERT_UINT_EQ(VMCS_CTRL_EPTP, 0x0000201Au, "EPTP encoding");
    return 0;
}

static int test_vmcs_field_value_msr_bitmap(void) {
    TEST_ASSERT_UINT_EQ(VMCS_CTRL_MSR_BITMAP_ADDR, 0x00002004u, "MSR_BITMAP encoding");
    return 0;
}

static int test_vmcs_field_value_guest_s_cet(void) {
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_S_CET, 0x00006828u, "GUEST_S_CET encoding");
    return 0;
}

static int test_vmcs_field_value_guest_ssp(void) {
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_SSP, 0x0000682Au, "GUEST_SSP encoding");
    return 0;
}

/*===========================================================================
 * Exit Reason Code Coverage
 *===========================================================================*/

static int test_exit_reason_codes(void) {
    TEST_ASSERT_UINT_EQ(EXIT_REASON_CPUID, 10, "EXIT_REASON_CPUID");
    TEST_ASSERT_UINT_EQ(EXIT_REASON_HLT, 12, "EXIT_REASON_HLT");
    TEST_ASSERT_UINT_EQ(EXIT_REASON_VMCALL, 18, "EXIT_REASON_VMCALL");
    TEST_ASSERT_UINT_EQ(EXIT_REASON_CR_ACCESS, 28, "EXIT_REASON_CR_ACCESS");
    TEST_ASSERT_UINT_EQ(EXIT_REASON_MSR_READ, 31, "EXIT_REASON_MSR_READ");
    TEST_ASSERT_UINT_EQ(EXIT_REASON_MSR_WRITE, 32, "EXIT_REASON_MSR_WRITE");
    TEST_ASSERT_UINT_EQ(EXIT_REASON_EPT_VIOLATION, 48, "EXIT_REASON_EPT_VIOLATION");
    return 0;
}

/*===========================================================================
 * MSR Address Constants
 *===========================================================================*/

static int test_msr_address_values(void) {
    TEST_ASSERT_UINT_EQ(IA32_VMX_BASIC, 0x480, "IA32_VMX_BASIC");
    TEST_ASSERT_UINT_EQ(IA32_VMX_PROCBASED_CTLS2, 0x48B, "IA32_VMX_PROCBASED_CTLS2");
    TEST_ASSERT_UINT_EQ(IA32_FEATURE_CONTROL, 0x3A, "IA32_FEATURE_CONTROL");
    TEST_ASSERT_UINT_EQ(IA32_EFER, 0xC0000080, "IA32_EFER");
    return 0;
}

/*===========================================================================
 * Test Runner
 *===========================================================================*/

TEST_MAIN_BEGIN()

    /* Revision ID extraction */
    TEST_RUN(test_revision_extract_standard);
    TEST_RUN(test_revision_extract_zero);
    TEST_RUN(test_revision_extract_max);
    TEST_RUN(test_revision_extract_large);
    TEST_RUN(test_revision_extract_upper_bits_masked);

    /* Region alignment */
    TEST_RUN(test_region_aligned_4k);
    TEST_RUN(test_region_aligned_8k);
    TEST_RUN(test_region_misaligned_1byte);
    TEST_RUN(test_region_misaligned_4k_minus_1);
    TEST_RUN(test_region_misaligned_half_page);
    TEST_RUN(test_region_null_pointer);

    /* VMCS field encoding validation */
    TEST_RUN(test_vmcs_encoding_width_16bit);
    TEST_RUN(test_vmcs_encoding_width_64bit);
    TEST_RUN(test_vmcs_encoding_width_32bit);
    TEST_RUN(test_vmcs_encoding_width_natural);
    TEST_RUN(test_vmcs_encoding_type_control);
    TEST_RUN(test_vmcs_encoding_type_read_only);
    TEST_RUN(test_vmcs_encoding_type_guest);
    TEST_RUN(test_vmcs_encoding_type_host);
    TEST_RUN(test_vmcs_encoding_index_pin_based);
    TEST_RUN(test_vmcs_encoding_index_guest_rip);
    TEST_RUN(test_vmcs_encoding_invalid_bit15_set);
    TEST_RUN(test_vmcs_encoding_valid);

    /* Error codes */
    TEST_RUN(test_vmx_error_codes_negative);

    /* Field encoding constant verification */
    TEST_RUN(test_vmcs_field_value_ctrl_pin_based);
    TEST_RUN(test_vmcs_field_value_ctrl_cpu_based);
    TEST_RUN(test_vmcs_field_value_guest_rip);
    TEST_RUN(test_vmcs_field_value_guest_rsp);
    TEST_RUN(test_vmcs_field_value_guest_cr0);
    TEST_RUN(test_vmcs_field_value_host_rip);
    TEST_RUN(test_vmcs_field_value_eptp);
    TEST_RUN(test_vmcs_field_value_msr_bitmap);
    TEST_RUN(test_vmcs_field_value_guest_s_cet);
    TEST_RUN(test_vmcs_field_value_guest_ssp);

    /* Exit reason codes */
    TEST_RUN(test_exit_reason_codes);

    /* MSR addresses */
    TEST_RUN(test_msr_address_values);

TEST_MAIN_END()
