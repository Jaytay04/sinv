#include <stdio.h>
#include <string.h>

#include "payload_diagnostic.h"

typedef struct {
    payload_diagnostic_launch_t launch;
    const char *token;
} launch_case_t;

static const launch_case_t g_launch_cases[] = {
    { PAYLOAD_DIAG_SKIPPED, "skipped" },
    { PAYLOAD_DIAG_ABSENT, "absent" },
    { PAYLOAD_DIAG_PRESENCE_ERROR, "presence_error" },
    { PAYLOAD_DIAG_ARGUMENT, "argument" },
    { PAYLOAD_DIAG_NOT_FOUND, "not_found" },
    { PAYLOAD_DIAG_READ, "read" },
    { PAYLOAD_DIAG_DEVICE_PATH, "device_path" },
    { PAYLOAD_DIAG_LOAD, "load" },
    { PAYLOAD_DIAG_IMAGE_FORMAT, "image_format" },
    { PAYLOAD_DIAG_PROTOCOL, "protocol" },
    { PAYLOAD_DIAG_START, "start" },
    { PAYLOAD_DIAG_OK, "ok" }
};

static const char *g_shim_phases[] = {
    "not_applicable",
    "argument",
    "reentrant",
    "firmware_load",
    "shim_lookup",
    "secure_boot_read",
    "firmware_direct",
    "enforcement",
    "shim_verify",
    "security2_lookup",
    "security2_interface"
};

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int bytes_equal(const UINT8 *Buffer, UINTN Length, UINT8 Value)
{
    UINTN index;
    for (index = 0; index < Length; index++) {
        if (Buffer[index] != Value) return 0;
    }
    return 1;
}

static int format_and_compare(const payload_diagnostic_t *diagnostic,
                              const char *expected)
{
    UINT8 actual[256];
    UINTN written = (UINTN)-1;
    UINTN expected_length = (UINTN)strlen(expected);

    memset(actual, 0xcc, sizeof(actual));
    if (!payload_diagnostic_format(diagnostic, actual, sizeof(actual),
                                   &written)) {
        fprintf(stderr, "FAIL: formatter rejected a valid diagnostic\n");
        return 0;
    }
    if (written != expected_length ||
        memcmp(actual, expected, expected_length) != 0 ||
        actual[written] != 0xcc) {
        fprintf(stderr, "FAIL: formatter bytes differ from expected record\n");
        return 0;
    }
    return 1;
}

static int format_startup_and_compare(
    const payload_diagnostic_t *diagnostic,
    payload_diagnostic_startup_state_t state,
    const hv_boot_diagnostic_t *record, const char *expected)
{
    UINT8 actual[PAYLOAD_DIAGNOSTIC_MAX_SIZE];
    UINTN written = (UINTN)-1;
    UINTN expected_length = (UINTN)strlen(expected);

    memset(actual, 0xcc, sizeof(actual));
    if (!payload_diagnostic_format_with_startup(
            diagnostic, state, record, actual, sizeof(actual), &written)) {
        fprintf(stderr, "FAIL: startup formatter rejected a valid diagnostic\n");
        return 0;
    }
    if (written != expected_length ||
        memcmp(actual, expected, expected_length) != 0 ||
        actual[written] != 0xcc) {
        fprintf(stderr, "FAIL: startup formatter bytes differ from expected record\n");
        return 0;
    }
    return 1;
}

static int check_category(const launch_case_t *test_case)
{
    payload_diagnostic_t diagnostic;
    char expected[256];
    UINT64 payload_status = (UINT64)EFI_ENCODE_ERROR(0x1234u);
    const char *shim_phase;
    EFI_STATUS shim_status;
    int count;

    diagnostic.payload_launch = test_case->launch;
    diagnostic.payload_status = (EFI_STATUS)payload_status;
    if (test_case->launch != PAYLOAD_DIAG_LOAD &&
        test_case->launch != PAYLOAD_DIAG_PROTOCOL &&
        test_case->launch != PAYLOAD_DIAG_START &&
        test_case->launch != PAYLOAD_DIAG_OK) {
        shim_phase = "not_applicable";
        shim_status = EFI_NOT_STARTED;
    } else {
        shim_phase = "firmware_load";
        shim_status = (EFI_STATUS)EFI_ENCODE_ERROR(0x5678u);
    }
    diagnostic.shim_load_phase = shim_phase;
    diagnostic.shim_load_status = shim_status;
    count = snprintf(expected, sizeof(expected),
        "schema=1\npayload_launch=%s\npayload_status=0x%016llX\n"
        "shim_load_phase=%s\nshim_load_status=0x%016llX\n",
        test_case->token,
        (unsigned long long)payload_status,
        shim_phase,
        (unsigned long long)(UINT64)diagnostic.shim_load_status);
    return count > 0 && (size_t)count < sizeof(expected) &&
           format_and_compare(&diagnostic, expected);
}

static int check_shim_phase(const char *phase)
{
    payload_diagnostic_t diagnostic;
    char expected[256];
    int count;

    diagnostic.payload_launch = PAYLOAD_DIAG_OK;
    diagnostic.payload_status = EFI_SUCCESS;
    diagnostic.shim_load_phase = phase;
    diagnostic.shim_load_status = EFI_ENCODE_ERROR(0x89abu);
    count = snprintf(expected, sizeof(expected),
        "schema=1\npayload_launch=ok\npayload_status=0x%016llX\n"
        "shim_load_phase=%s\nshim_load_status=0x%016llX\n",
        (unsigned long long)(UINT64)EFI_SUCCESS, phase,
        (unsigned long long)(UINT64)diagnostic.shim_load_status);
    return count > 0 && (size_t)count < sizeof(expected) &&
           format_and_compare(&diagnostic, expected);
}

int main(void)
{
    static const char expected_golden[] =
        "schema=1\n"
        "payload_launch=start\n"
        "payload_status=0x8000000000004567\n"
        "shim_load_phase=shim_verify\n"
        "shim_load_status=0x80000000000089AB\n";
    static const char expected_preload[] =
        "schema=1\n"
        "payload_launch=read\n"
        "payload_status=0x8000000000001010\n"
        "shim_load_phase=not_applicable\n"
        "shim_load_status=0x8000000000000013\n";
    payload_diagnostic_t diagnostic;
    hv_boot_diagnostic_t startup;
    UINT8 guarded[258];
    UINT8 startup_guarded[PAYLOAD_DIAGNOSTIC_MAX_SIZE + 2u];
    UINTN written;
    UINTN i;

    diagnostic.payload_launch = PAYLOAD_DIAG_START;
    diagnostic.payload_status = (EFI_STATUS)EFI_ENCODE_ERROR(0x4567u);
    diagnostic.shim_load_phase = "shim_verify";
    diagnostic.shim_load_status = (EFI_STATUS)EFI_ENCODE_ERROR(0x89abu);
    if (!expect(format_and_compare(&diagnostic, expected_golden),
                "full record preserves exact high-bit EFI_STATUS values")) {
        return 1;
    }

    hv_boot_diagnostic_init(&startup);
    startup.Flags = HV_BOOT_DIAGNOSTIC_FLAGS;
    startup.Stage = HV_BOOT_STAGE_GPU_PREPARE;
    startup.Reason = HV_BOOT_REASON_OPERATION_FAILED;
    startup.Processor = 2u;
    startup.Status = (UINT64)EFI_ENCODE_ERROR(0x2345u);
    startup.Detail = 0x12345678u;
    startup.ExpectedCpus = 20u;
    startup.PreparedCpus = 18u;
    startup.VmxReadyCpus = 17u;
    startup.ResourceStatus = 0x80000001u;
    startup.PreparationStatus = 0x80000002u;
    startup.PreparationFailure = 10u;
    startup.ActivationStatus = 0x80000003u;
    startup.ActivationFailure = 7u;
    {
        char expected_startup[PAYLOAD_DIAGNOSTIC_MAX_SIZE];
        int count;
        diagnostic.payload_launch = PAYLOAD_DIAG_START;
        diagnostic.payload_status = (EFI_STATUS)startup.Status;
        diagnostic.shim_load_phase = "firmware_load";
        diagnostic.shim_load_status = EFI_ENCODE_ERROR(0x5678u);
        count = snprintf(expected_startup, sizeof(expected_startup),
            "schema=1\npayload_launch=start\npayload_status=0x%016llX\n"
            "shim_load_phase=firmware_load\nshim_load_status=0x%016llX\n"
            "startup_record=valid\nstartup_version=1\n"
            "startup_flags=0x00000003\nstartup_stage=gpu_prepare\n"
            "startup_reason=operation_failed\nstartup_status=0x%016llX\n"
            "startup_processor=2\nstartup_detail=305419896\n"
            "startup_expected_cpus=20\nstartup_prepared_cpus=18\n"
            "startup_vmx_ready_cpus=17\n"
            "startup_resource_status=0x80000001\n"
            "startup_preparation_status=0x80000002\n"
            "startup_preparation_failure=10\n"
            "startup_activation_status=0x80000003\n"
            "startup_activation_failure=7\n",
            (unsigned long long)(UINT64)diagnostic.payload_status,
            (unsigned long long)(UINT64)diagnostic.shim_load_status,
            (unsigned long long)startup.Status);
        if (!expect(count > 0 && (size_t)count < sizeof(expected_startup),
                    "expected startup record fits bounded output") ||
            !expect(format_startup_and_compare(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_VALID,
                        &startup, expected_startup),
                    "startup formatter preserves stage, status, and counters")) {
            return 1;
        }

        startup.Stage = HV_BOOT_STAGE_AP_PREPARE;
        startup.Detail = 0x89030f0fu;
        memset(startup_guarded, 0, sizeof(startup_guarded));
        written = 0u;
        if (!expect(payload_diagnostic_format_with_startup(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_VALID, &startup,
                        startup_guarded + 1u, sizeof(startup_guarded) - 2u,
                        &written),
                    "tagged IOMMU detail fits the existing startup formatter") ||
            !expect(written < sizeof(startup_guarded) - 2u &&
                        strstr((const char *)(startup_guarded + 1u),
                               "startup_stage=ap_prepare\n") != NULL &&
                        strstr((const char *)(startup_guarded + 1u),
                               "startup_detail=2298679055\n") != NULL,
                    "high-bit IOMMU detail survives as unsigned decimal")) {
            return 1;
        }
        startup.Stage = HV_BOOT_STAGE_GPU_PREPARE;
        startup.Detail = 0x12345678u;

        memset(startup_guarded, 0xa5, sizeof(startup_guarded));
        written = 77u;
        if (!expect(!payload_diagnostic_format_with_startup(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_VALID, &startup,
                        startup_guarded + 1u, (UINTN)count - 1u, &written),
                    "one-byte-short startup capacity is rejected") ||
            !expect(written == 0u &&
                        bytes_equal(startup_guarded,
                                    sizeof(startup_guarded), 0xa5u),
                    "startup capacity failure leaves buffer and guards untouched")) {
            return 1;
        }

        memset(startup_guarded, 0xa5, sizeof(startup_guarded));
        written = 77u;
        diagnostic.payload_status = EFI_SUCCESS;
        if (!expect(!payload_diagnostic_format_with_startup(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_VALID, &startup,
                        startup_guarded + 1u, sizeof(startup_guarded) - 2u,
                        &written),
                    "record status must match payload launch status") ||
            !expect(written == 0u &&
                        bytes_equal(startup_guarded,
                                    sizeof(startup_guarded), 0xa5u),
                    "status mismatch is rejected atomically")) {
            return 1;
        }

        diagnostic.payload_status = (EFI_STATUS)startup.Status;
        diagnostic.payload_launch = PAYLOAD_DIAG_OK;
        written = 77u;
        if (!expect(!payload_diagnostic_format_with_startup(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_VALID, &startup,
                        startup_guarded + 1u, sizeof(startup_guarded) - 2u,
                        &written),
                    "EFI error record must pair with failed direct launch") ||
            !expect(written == 0u &&
                        bytes_equal(startup_guarded,
                                    sizeof(startup_guarded), 0xa5u),
                    "launch-status mismatch is rejected atomically")) {
            return 1;
        }
    }

    diagnostic.payload_launch = PAYLOAD_DIAG_OK;
    diagnostic.payload_status = EFI_SUCCESS;
    diagnostic.shim_load_phase = "not_applicable";
    diagnostic.shim_load_status = EFI_NOT_STARTED;
    hv_boot_diagnostic_init(&startup);
    startup.Flags = HV_BOOT_DIAGNOSTIC_FLAGS;
    startup.Stage = HV_BOOT_STAGE_RETURN;
    startup.Reason = HV_BOOT_REASON_NONE;
    startup.Status = EFI_SUCCESS;
    {
        static const char expected_startup_success[] =
            "schema=1\npayload_launch=ok\npayload_status=0x0000000000000000\n"
            "shim_load_phase=not_applicable\n"
            "shim_load_status=0x8000000000000013\n"
            "startup_record=valid\nstartup_version=1\n"
            "startup_flags=0x00000003\nstartup_stage=return\n"
            "startup_reason=none\nstartup_status=0x0000000000000000\n"
            "startup_processor=4294967295\nstartup_detail=0\n"
            "startup_expected_cpus=0\nstartup_prepared_cpus=0\n"
            "startup_vmx_ready_cpus=0\n"
            "startup_resource_status=0x00000000\n"
            "startup_preparation_status=0x00000000\n"
            "startup_preparation_failure=0\n"
            "startup_activation_status=0x00000000\n"
            "startup_activation_failure=0\n";
        if (!expect(format_startup_and_compare(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_VALID, &startup,
                        expected_startup_success),
                    "successful returned record has a distinct stage and reason")) {
            return 1;
        }
    }

    {
        static const char expected_unavailable[] =
            "schema=1\npayload_launch=ok\npayload_status=0x0000000000000000\n"
            "shim_load_phase=not_applicable\n"
            "shim_load_status=0x8000000000000013\n"
            "startup_record=unavailable\n";
        static const char expected_invalid[] =
            "schema=1\npayload_launch=ok\npayload_status=0x0000000000000000\n"
            "shim_load_phase=not_applicable\n"
            "shim_load_status=0x8000000000000013\n"
            "startup_record=invalid\n";
        if (!expect(format_startup_and_compare(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_UNAVAILABLE,
                        NULL, expected_unavailable),
                    "unavailable record is explicit without fabricated details") ||
            !expect(format_startup_and_compare(
                        &diagnostic, PAYLOAD_DIAG_STARTUP_INVALID,
                        NULL, expected_invalid),
                    "invalid record is explicit without exposing corrupt fields")) {
            return 1;
        }
    }

    startup.Flags = HV_BOOT_DIAGNOSTIC_ENTRY_REACHED;
    written = 77u;
    memset(startup_guarded, 0xa5, sizeof(startup_guarded));
    if (!expect(!payload_diagnostic_format_with_startup(
                    &diagnostic, PAYLOAD_DIAG_STARTUP_VALID, &startup,
                    startup_guarded + 1u, sizeof(startup_guarded) - 2u,
                    &written),
                "record missing Returned flag is rejected") ||
        !expect(written == 0u &&
                    bytes_equal(startup_guarded,
                                sizeof(startup_guarded), 0xa5u),
                "invalid record leaves output and guards untouched")) {
        return 1;
    }

    for (i = 0; i < sizeof(g_launch_cases) / sizeof(g_launch_cases[0]); ++i) {
        if (!expect(check_category(&g_launch_cases[i]),
                    "every documented payload launch category formats")) {
            return 1;
        }
    }
    for (i = 0; i < sizeof(g_shim_phases) / sizeof(g_shim_phases[0]); ++i) {
        if (!expect(check_shim_phase(g_shim_phases[i]),
                    "every documented shim load phase formats")) {
            return 1;
        }
    }

    diagnostic.payload_launch = PAYLOAD_DIAG_READ;
    diagnostic.payload_status = (EFI_STATUS)EFI_ENCODE_ERROR(0x1010u);
    diagnostic.shim_load_phase = "shim_verify";
    diagnostic.shim_load_status = (EFI_STATUS)EFI_ENCODE_ERROR(0x2020u);
    if (!expect(format_and_compare(&diagnostic, expected_preload),
                "pre-load failure clears stale shim load state")) {
        return 1;
    }

    diagnostic.payload_launch = PAYLOAD_DIAG_START;
    diagnostic.payload_status = (EFI_STATUS)EFI_ENCODE_ERROR(0x4567u);
    diagnostic.shim_load_phase = "shim_verify";
    diagnostic.shim_load_status = (EFI_STATUS)EFI_ENCODE_ERROR(0x89abu);
    memset(guarded, 0xa5, sizeof(guarded));
    written = 77u;
    if (!expect(!payload_diagnostic_format(
                    &diagnostic, guarded + 1u, sizeof(expected_golden) - 2u,
                    &written),
                "one-byte-short capacity is rejected") ||
        !expect(written == 0u,
                "capacity failure reports zero written bytes") ||
        !expect(bytes_equal(guarded, sizeof(guarded), 0xa5u),
                "capacity failure leaves the destination and guards untouched")) {
        return 1;
    }

    written = 77u;
    diagnostic.payload_launch = (payload_diagnostic_launch_t)99;
    if (!expect(!payload_diagnostic_format(
                    &diagnostic, guarded + 1u, sizeof(guarded) - 2u,
                    &written),
                "unknown payload category is rejected") ||
        !expect(written == 0u,
                "invalid category reports zero written bytes")) {
        return 1;
    }

    diagnostic.payload_launch = PAYLOAD_DIAG_OK;
    diagnostic.shim_load_phase = "not_a_shim_phase";
    written = 77u;
    if (!expect(!payload_diagnostic_format(
                    &diagnostic, guarded + 1u, sizeof(guarded) - 2u,
                    &written),
                "unknown shim phase is rejected") ||
        !expect(written == 0u,
                "invalid shim phase reports zero written bytes")) {
        return 1;
    }

    written = 77u;
    if (!expect(!payload_diagnostic_format(
                    NULL, guarded + 1u, sizeof(guarded) - 2u, &written),
                "null diagnostic is rejected") ||
        !expect(written == 0u, "null diagnostic reports zero bytes")) {
        return 1;
    }

    puts("payload_diagnostic_test: PASS");
    return 0;
}
