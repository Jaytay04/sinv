/*
 * test_vmx_processor_state.c — Processor state validation tests (R4a)
 *
 * Tests VmxValidateProcessorState with synthetic GDT/LDT tables,
 * including null, malformed, TI, and upper selector-index cases.
 */

#include "test_common.h"
#include "vmx_processor_state.h"

#include <string.h>  /* memset */

/* Build a minimal valid state for reuse. */
static void InitValidState(VMX_PROCESSOR_STATE* s)
{
    (void)memset(s, 0, sizeof(*s));
    s->Cs = 0x0010; /* index 2, RPL 0 */
    s->Ss = 0x0018; /* index 3, RPL 0 */
    s->Tr = 0x0040; /* index 8, RPL 0 */
    s->Es = s->Ds = s->Fs = s->Gs = 0;  /* null → unusable */

    /* CS: present, code, ring 0 */
    s->CsDesc.present = true;
    s->CsDesc.base = 0xFFFFF80000000000ULL;
    s->CsDesc.limit = 0xFFFFFFFFu;
    s->CsDesc.access_rights = 0xC09B;

    /* SS: present, data, ring 0 */
    s->SsDesc.present = true;
    s->SsDesc.base = 0xFFFFF80000000000ULL;
    s->SsDesc.limit = 0xFFFFFFFFu;
    s->SsDesc.access_rights = 0xC093;

    /* TR: present, 64-bit TSS */
    s->TrDesc.present = true;
    s->TrDesc.is_system = true;
    s->TrDesc.base = 0xFFFFF80001234000ULL;
    s->TrDesc.limit = 0x67;
    s->TrDesc.access_rights = 0x008B;

    /* Canonical table bases */
    s->GdtrBase = 0xFFFFF80000001000ULL;
    s->IdtrBase = 0xFFFFF80000002000ULL;

    s->FsBase = s->GsBase = 0;
}

/* --- Valid state --- */
static int test_valid_minimal(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s), "Valid minimal state");
    TEST_ASSERT_TRUE(s.IsValid, "IsValid true");
    /* Null segments should be unusable */
    TEST_ASSERT_UINT_EQ(s.EsDesc.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "Null ES → unusable");
    TEST_ASSERT_FALSE(s.EsDesc.present, "Null ES → not present");
    /* Guest selectors preserved (no sanitization) */
    TEST_ASSERT_UINT_EQ((unsigned int)s.Cs, 0x0010, "Guest CS preserved");
    return 0;
}

/* --- Null LDTR becomes unusable --- */
static int test_null_ldtr_unusable(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Ldtr = 0;
    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s), "Null LDTR valid");
    TEST_ASSERT_UINT_EQ(s.LdtrDesc.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "Null LDTR → unusable");
    return 0;
}

static int test_null_with_rpl_is_null(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Es = 0x0001; /* null selector with RPL=1 */
    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s), "Null+RPL=1 valid");
    TEST_ASSERT_UINT_EQ(s.EsDesc.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "Null+RPL=1 → unusable");
    return 0;
}

static int test_null_rpl3_is_null(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Ds = 0x0003; /* null selector with RPL=3 */
    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s), "Null+RPL=3 valid");
    TEST_ASSERT_UINT_EQ(s.DsDesc.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "Null+RPL=3 → unusable");
    return 0;
}

static int test_rejects_null_cs(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Cs = 0x0001; /* null selector RPL=1, descriptor says present */
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Null CS rejected");
    return 0;
}

static int test_rejects_null_ss(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Ss = 0x0003; /* null selector RPL=3, descriptor says present */
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Null SS rejected");
    return 0;
}

static int test_accepts_exact_long_mode_null_ss(void)
{
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Ia32Efer = EFER_LONG_MODE_ACTIVE;
    s.CsDesc.access_rights = 0x209Bu;
    s.Ss = 0;
    s.SsDesc.base = 0x0000800000000000ULL;
    s.SsDesc.limit = UINT32_MAX;
    s.SsDesc.access_rights = 0xC093u;
    s.SsDesc.present = true;

    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s),
        "IA-32e CPL0 null SS accepted");
    TEST_ASSERT_UINT_EQ(s.SsDesc.base, 0, "Null SS base normalized");
    TEST_ASSERT_UINT_EQ(s.SsDesc.limit, 0, "Null SS limit normalized");
    TEST_ASSERT_UINT_EQ(s.SsDesc.access_rights,
        VMX_SEGMENT_ACCESS_UNUSABLE, "Null SS marked unusable");
    TEST_ASSERT_FALSE(s.SsDesc.present, "Null SS not present");
    return 0;
}

static int test_rejects_null_ss_outside_exact_long_mode_case(void)
{
    VMX_PROCESSOR_STATE s;

    InitValidState(&s);
    s.Ss = 0;
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s),
        "Null SS rejected without LMA");

    InitValidState(&s);
    s.Ia32Efer = EFER_LONG_MODE_ACTIVE;
    s.Ss = 0;
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s),
        "Null SS rejected without CS.L");

    InitValidState(&s);
    s.Ia32Efer = EFER_LONG_MODE_ACTIVE;
    s.CsDesc.access_rights = 0x209Bu;
    s.Cs = 0x0013u;
    s.Ss = 0;
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s),
        "Null SS rejected outside CPL0");

    InitValidState(&s);
    s.Ia32Efer = EFER_LONG_MODE_ACTIVE;
    s.CsDesc.access_rights = 0x209Bu;
    s.Ss = 0x0003u;
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s),
        "Null SS with nonzero RPL encoding rejected");
    return 0;
}

static int test_rejects_null_tr(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Tr = 0x0001; /* null selector, descriptor says present + system */
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Null TR rejected");
    return 0;
}

/* --- Null/unusable data segments --- */
static int test_null_ds_becomes_unusable(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s), "Null DS valid");
    TEST_ASSERT_UINT_EQ(s.DsDesc.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "DS null → unusable");
    return 0;
}

/* --- Reject TI-based selectors --- */
static int test_rejects_ti_cs(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Cs = 0x0014; /* index 2, TI=1 */
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "TI CS rejected");
    return 0;
}

static int test_rejects_ti_ss(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Ss = 0x001C; /* TI=1 */
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "TI SS rejected");
    return 0;
}

static int test_rejects_ti_tr(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Tr = 0x0044; /* TI=1 */
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "TI TR rejected");
    return 0;
}

/* --- Reject non-present CS/SS/TR --- */
static int test_rejects_nonpresent_cs(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.CsDesc.present = false; TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Non-present CS rejected"); s.CsDesc.present = true; s.CsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE; TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Unusable CS rejected");
    return 0;
}

static int test_rejects_nonpresent_ss(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.SsDesc.present = false; TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Non-present SS rejected"); s.SsDesc.present = true; s.SsDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE; TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Unusable SS rejected");
    return 0;
}

static int test_rejects_nonpresent_tr(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.TrDesc.present = false; TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Non-present TR rejected"); s.TrDesc.present = true; s.TrDesc.access_rights = VMX_SEGMENT_ACCESS_UNUSABLE; TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Unusable TR rejected");
    return 0;
}

static int test_rejects_nonsystem_tr(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.TrDesc.is_system = false;
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Non-system TR rejected");
    return 0;
}

/* --- Reject non-canonical bases --- */
static int test_rejects_noncanonical_optional_segment_bases(void)
{
    VMX_PROCESSOR_STATE s;
#define REJECT(S, D, L) do { InitValidState(&s); s.S = 0x0020; s.D.present = true; \
    s.D.base = 0x0000800000000000ULL; \
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), L); } while (0)
    REJECT(Es, EsDesc, "Noncanonical ES descriptor base");
    REJECT(Ds, DsDesc, "Noncanonical DS descriptor base");
    REJECT(Ldtr, LdtrDesc, "Noncanonical LDTR descriptor base");
    REJECT(Fs, FsDesc, "Noncanonical FS descriptor base");
    REJECT(Gs, GsDesc, "Noncanonical GS descriptor base");
#undef REJECT
    return 0;
}
static int test_rejects_noncanonical_cs_base(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.CsDesc.base = 0x0000800000000000ULL;
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Non-canonical CS base");
    return 0;
}

static int test_rejects_noncanonical_gdtr_base(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.GdtrBase = 0x0000800000000000ULL;
    TEST_ASSERT_FALSE(VmxValidateProcessorState(&s), "Non-canonical GDTR base");
    return 0;
}

/* --- Guest selectors preserved (sanitization moved to host plan) --- */
static int test_preserves_guest_selector_rpl(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Cs = 0x0013; /* index 2, RPL=3 */
    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s), "RPL preserved: valid");
    TEST_ASSERT_UINT_EQ((unsigned int)s.Cs, 0x0013, "Guest CS RPL preserved");
    return 0;
}

static int test_preserves_upper_selector_bits(void) {
    VMX_PROCESSOR_STATE s;
    InitValidState(&s);
    s.Cs = 0xFF0B; /* index 0x1FE1, RPL=3 */
    s.CsDesc.present = true;
    s.CsDesc.base = 0xFFFFF80000000000ULL;
    TEST_ASSERT_TRUE(VmxValidateProcessorState(&s), "Upper bits valid");
    TEST_ASSERT_UINT_EQ((unsigned int)s.Cs, 0xFF0B, "Guest selector preserved");
    return 0;
}

/* --- Null input --- */
static int test_rejects_null_state(void) {
    TEST_ASSERT_FALSE(VmxValidateProcessorState(NULL), "NULL rejected");
    return 0;
}


/* --- Pure current-processor population fixture --- */
static uint8_t g_fixture_gdt[0x30];

static void FixtureDescriptor(uint32_t slot, uint64_t base, uint32_t limit,
                              uint8_t access, uint8_t flags)
{
    uint8_t* d = &g_fixture_gdt[slot * 8u];
    (void)memset(d, 0, 8);
    d[0] = (uint8_t)limit; d[1] = (uint8_t)(limit >> 8);
    d[2] = (uint8_t)base; d[3] = (uint8_t)(base >> 8);
    d[4] = (uint8_t)(base >> 16); d[5] = access;
    d[6] = (uint8_t)((limit >> 16) & 0x0Fu) | flags; d[7] = (uint8_t)(base >> 24);
}

static void FixtureTss(uint64_t base, uint32_t limit, uint8_t access)
{
    FixtureDescriptor(3, base, limit, access, 0);
    g_fixture_gdt[0x20] = (uint8_t)(base >> 32); g_fixture_gdt[0x21] = (uint8_t)(base >> 40);
    g_fixture_gdt[0x22] = (uint8_t)(base >> 48); g_fixture_gdt[0x23] = (uint8_t)(base >> 56);
}

static void InitPopulateFixture(VMX_RAW_PROCESSOR_STATE* raw,
                                VMX_PROCESSOR_MSR_STATE* msrs)
{
    (void)memset(g_fixture_gdt, 0, sizeof(g_fixture_gdt));
    FixtureDescriptor(1, 0, 0xFFFFFu, 0x9Bu, 0xC0u); FixtureDescriptor(2, 0, 0xFFFFFu, 0x93u, 0xC0u);
    FixtureTss(0xFFFF800012345678ULL, 0x67u, 0x89u);
    (void)memset(raw, 0, sizeof(*raw));
    raw->Cs = 0x0008; raw->Ss = 0x0010;
    raw->Tr = 0x0018; raw->GdtrBase = (uint64_t)(uintptr_t)g_fixture_gdt;
    raw->GdtrLimit = (uint16_t)(sizeof(g_fixture_gdt) - 1u); raw->IdtrBase = 0xFFFF800000002000ULL;
    raw->IdtrLimit = 0x0FFF; (void)memset(msrs, 0, sizeof(*msrs));
    msrs->Cr0 = 0x10000001ULL; msrs->Cr3 = 0x0000000012345000ULL;
    msrs->Cr4 = 0x2000ULL; msrs->Dr7 = 0x00000400ULL;
    msrs->Ia32Debugctl = 0x11ULL; msrs->Ia32Pat = 0x0706050403020100ULL;
    msrs->Ia32Efer = 0xD01ULL; msrs->SysenterCs = 0x10ULL;
    msrs->SysenterEsp = 0xFFFF800000003000ULL; msrs->SysenterEip = 0xFFFF800000004000ULL;
    msrs->FsBase = 0xFFFF800000005000ULL; msrs->GsBase = 0xFFFF800000006000ULL;
}

static int test_populate_copies_capture_and_decodes(void)
{
    VMX_RAW_PROCESSOR_STATE raw; VMX_PROCESSOR_MSR_STATE msrs; VMX_PROCESSOR_STATE state;
    uint64_t expected_tss[2];
    InitPopulateFixture(&raw, &msrs);
    (void)memcpy(expected_tss, &g_fixture_gdt[0x18], sizeof(expected_tss));
    TEST_ASSERT_TRUE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, &state),
                     "Populate valid processor state");
    TEST_ASSERT_UINT_EQ(state.Cr0, msrs.Cr0, "CR0 copied"); TEST_ASSERT_UINT_EQ(state.Cr3, msrs.Cr3, "CR3 copied");
    TEST_ASSERT_UINT_EQ(state.Cr4, msrs.Cr4, "CR4 copied"); TEST_ASSERT_UINT_EQ(state.Dr7, msrs.Dr7, "DR7 copied");
    TEST_ASSERT_UINT_EQ(state.Ia32Debugctl, msrs.Ia32Debugctl, "DEBUGCTL copied"); TEST_ASSERT_UINT_EQ(state.Ia32Pat, msrs.Ia32Pat, "PAT copied");
    TEST_ASSERT_UINT_EQ(state.Ia32Efer, msrs.Ia32Efer, "EFER copied"); TEST_ASSERT_UINT_EQ(state.SysenterCs, msrs.SysenterCs, "SYSENTER CS copied");
    TEST_ASSERT_UINT_EQ(state.SysenterEsp, msrs.SysenterEsp, "SYSENTER ESP copied"); TEST_ASSERT_UINT_EQ(state.SysenterEip, msrs.SysenterEip, "SYSENTER EIP copied");
    TEST_ASSERT_UINT_EQ(state.FsBase, msrs.FsBase, "FS base copied"); TEST_ASSERT_UINT_EQ(state.GsBase, msrs.GsBase, "GS base copied");
    TEST_ASSERT_UINT_EQ(state.CsDesc.access_rights, 0xC09Bu, "CS access rights"); TEST_ASSERT_UINT_EQ(state.CsDesc.limit, 0xFFFFFFFFu, "CS effective limit");
    TEST_ASSERT_UINT_EQ(state.SsDesc.access_rights, 0xC093u, "SS access rights"); TEST_ASSERT_UINT_EQ(state.TrDesc.base, 0xFFFF800012345678ULL, "TR full base");
    TEST_ASSERT_UINT_EQ(state.TrDesc.limit, 0x67u, "TR limit"); TEST_ASSERT_UINT_EQ(state.TrDesc.access_rights, 0x0089u, "TR access rights");
    TEST_ASSERT_UINT_EQ(state.TrSysDesc[0], expected_tss[0], "TR raw low qword"); TEST_ASSERT_UINT_EQ(state.TrSysDesc[1], expected_tss[1], "TR raw high qword");
    TEST_ASSERT_UINT_EQ(state.EsDesc.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE, "Null ES unusable");
    TEST_ASSERT_FALSE(state.EsDesc.present, "Null ES not present"); TEST_ASSERT_TRUE(state.IsValid, "Populated state valid");
    return 0;
}

static int test_populate_rejects_ti_and_null_gdt(void)
{
    VMX_RAW_PROCESSOR_STATE raw; VMX_PROCESSOR_MSR_STATE msrs; VMX_PROCESSOR_STATE state;
    InitPopulateFixture(&raw, &msrs);
    raw.Cs |= 0x0004u;
    TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, &state),
                      "TI selector rejected");
    InitPopulateFixture(&raw, &msrs);
    TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, NULL, &msrs, &state),
                      "Null GDT rejected"); InitPopulateFixture(&raw, &msrs); TEST_ASSERT_FALSE(VmxPopulateProcessorState(NULL, g_fixture_gdt, &msrs, &state), "Null raw rejected"); TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, NULL, &state), "Null MSRs rejected"); TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, NULL), "Null output rejected");
    return 0;
}

static int test_populate_rejects_truncated_tss(void)
{
    VMX_RAW_PROCESSOR_STATE raw; VMX_PROCESSOR_MSR_STATE msrs; VMX_PROCESSOR_STATE state;
    InitPopulateFixture(&raw, &msrs);
    raw.GdtrLimit = 0x1Fu;
    TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, &state),
                      "Truncated 16-byte TSS rejected");
    return 0;
}

static int test_populate_rejects_invalid_tr_descriptor(void)
{
    VMX_RAW_PROCESSOR_STATE raw; VMX_PROCESSOR_MSR_STATE msrs; VMX_PROCESSOR_STATE state;
    InitPopulateFixture(&raw, &msrs);
    g_fixture_gdt[0x1D] = 0x91u; /* S=1, non-system descriptor */
    TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, &state),
                      "Non-system TR rejected");
    InitPopulateFixture(&raw, &msrs);
    g_fixture_gdt[0x1D] = 0x09u; /* TSS type, present clear */
    TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, &state),
                      "Non-present TR rejected");
    return 0;
}

static int test_populate_rejects_noncanonical_fs_gs(void)
{
    VMX_RAW_PROCESSOR_STATE raw; VMX_PROCESSOR_MSR_STATE msrs; VMX_PROCESSOR_STATE state;
    InitPopulateFixture(&raw, &msrs);
    msrs.FsBase = 0x0000800000000000ULL;
    TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, &state),
                      "Noncanonical FS base rejected");
    InitPopulateFixture(&raw, &msrs);
    msrs.GsBase = 0x0000800000000000ULL;
    TEST_ASSERT_FALSE(VmxPopulateProcessorState(&raw, g_fixture_gdt, &msrs, &state),
                      "Noncanonical GS base rejected");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_valid_minimal);
    TEST_RUN(test_null_ds_becomes_unusable);
    TEST_RUN(test_null_ldtr_unusable);
    TEST_RUN(test_null_with_rpl_is_null);
    TEST_RUN(test_null_rpl3_is_null);
    TEST_RUN(test_rejects_null_cs);
    TEST_RUN(test_rejects_null_ss);
    TEST_RUN(test_accepts_exact_long_mode_null_ss);
    TEST_RUN(test_rejects_null_ss_outside_exact_long_mode_case);
    TEST_RUN(test_rejects_null_tr);
    TEST_RUN(test_rejects_ti_cs);
    TEST_RUN(test_rejects_ti_ss);
    TEST_RUN(test_rejects_ti_tr);
    TEST_RUN(test_rejects_nonpresent_cs);
    TEST_RUN(test_rejects_nonpresent_ss);
    TEST_RUN(test_rejects_nonpresent_tr);
    TEST_RUN(test_rejects_nonsystem_tr);
    TEST_RUN(test_rejects_noncanonical_cs_base);
    TEST_RUN(test_rejects_noncanonical_optional_segment_bases);
    TEST_RUN(test_rejects_noncanonical_gdtr_base);
    TEST_RUN(test_preserves_guest_selector_rpl);
    TEST_RUN(test_preserves_upper_selector_bits);
    TEST_RUN(test_rejects_null_state);
    TEST_RUN(test_populate_copies_capture_and_decodes);
    TEST_RUN(test_populate_rejects_ti_and_null_gdt);
    TEST_RUN(test_populate_rejects_truncated_tss);
    TEST_RUN(test_populate_rejects_invalid_tr_descriptor);
    TEST_RUN(test_populate_rejects_noncanonical_fs_gs);
TEST_MAIN_END()
