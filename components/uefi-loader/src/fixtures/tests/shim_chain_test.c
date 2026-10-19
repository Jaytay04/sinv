#include <stdio.h>
#include <string.h>

#include "shim_chain.h"

typedef EFI_STATUS (*test_shim_verify_fn)(VOID *Buffer, UINT32 Size);
typedef EFI_STATUS (EFIAPI *test_get_variable_fn)(
    CHAR16 *Name, EFI_GUID *Guid, UINT32 *Attributes, UINTN *Size, VOID *Data);

typedef struct { test_shim_verify_fn Verify; } test_shim_protocol_t;
typedef struct test_security2_protocol_tag test_security2_protocol_t;
typedef EFI_STATUS (EFIAPI *test_auth_fn)(
    const test_security2_protocol_t *This, const EFI_DEVICE_PATH_PROTOCOL *Path,
    VOID *Buffer, UINTN Size, BOOLEAN BootPolicy);
struct test_security2_protocol_tag { test_auth_fn FileAuthentication; };

extern EFI_STATUS EFIAPI shim_verify_abi_self_test(void);

static EFI_BOOT_SERVICES g_boot;
static EFI_RUNTIME_SERVICES g_runtime;
static EFI_SYSTEM_TABLE g_system;
static test_shim_protocol_t g_shim;
static test_security2_protocol_t g_security2;
EFI_BOOT_SERVICES *gBS;
EFI_SYSTEM_TABLE *gST;

static UINT8 g_secure_boot;
static UINT8 g_setup_mode;
static UINT8 g_mok_state;
static int g_mok_present;
static UINT32 g_variable_attributes;
static UINTN g_variable_size;
static EFI_STATUS g_variable_status;
static int g_shim_present;
static int g_security2_present;
static EFI_STATUS g_verify_status;
static EFI_STATUS g_original_status;
static EFI_STATUS g_load_status;
static VOID *g_source;
static UINTN g_source_size;
static VOID *g_auth_buffer;
static UINTN g_auth_size;
static int g_nested_load;
static int g_auth_reenter;
static EFI_STATUS g_nested_status;
static UINTN g_verify_calls;
static UINTN g_original_calls;
static UINTN g_load_calls;
static UINTN g_security2_old_guid_calls;
static EFI_HANDLE g_child = (EFI_HANDLE)(UINTN)0x1234u;
static EFI_DEVICE_PATH_PROTOCOL *g_path =
    (EFI_DEVICE_PATH_PROTOCOL *)(UINTN)0x5678u;

static int char16_is(const CHAR16 *value, const char *ascii)
{
    UINTN i;
    for (i = 0; ascii[i] != '\0'; ++i) {
        if (value[i] != (UINT8)ascii[i]) return 0;
    }
    return value[i] == 0;
}

static int guid_is(const EFI_GUID *value, const EFI_GUID *expected)
{
    return value != NULL && expected != NULL &&
           memcmp(value, expected, sizeof(*expected)) == 0;
}

static const EFI_GUID g_expected_global_guid = {
    0x8be4df61u, 0x93cau, 0x11d2u,
    { 0xaau, 0x0du, 0x00u, 0xe0u, 0x98u, 0x03u, 0x2bu, 0x8cu }
};
static const EFI_GUID g_expected_shim_guid = {
    0x605dab50u, 0xe046u, 0x4300u,
    { 0xabu, 0xb6u, 0x3du, 0xd8u, 0x10u, 0xddu, 0x8bu, 0x23u }
};
static const EFI_GUID g_expected_security2_guid = {
    0x94ab2f58u, 0x1438u, 0x4ef1u,
    /* Independent PI Security2 contract, not copied from shim_chain.c:
     * https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/Security2.h */
    { 0x91u, 0x52u, 0x18u, 0x94u, 0x1au, 0x3au, 0x0eu, 0x68u }
};
static const EFI_GUID g_old_security2_guid = {
    0x94ab2f58u, 0x1438u, 0x4ef1u,
    { 0x91u, 0x52u, 0x18u, 0x94u, 0x1au, 0x3au, 0x8eu, 0x68u }
};

static EFI_STATUS EFIAPI mock_get_variable(
    CHAR16 *Name, EFI_GUID *Guid, UINT32 *Attributes, UINTN *Size, VOID *Data)
{
    UINT8 value;
    if (g_variable_status != EFI_SUCCESS) return g_variable_status;
    if (char16_is(Name, "SecureBoot") && guid_is(Guid, &g_expected_global_guid)) {
        value = g_secure_boot;
    } else if (char16_is(Name, "SetupMode") &&
               guid_is(Guid, &g_expected_global_guid)) {
        value = g_setup_mode;
    } else if (char16_is(Name, "MokSBStateRT") &&
               guid_is(Guid, &g_expected_shim_guid)) {
        if (!g_mok_present) return EFI_NOT_FOUND;
        value = g_mok_state;
    } else {
        return EFI_NOT_FOUND;
    }
    if (g_variable_size != 1u) {
        *Size = g_variable_size;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (Data == NULL || Size == NULL || Attributes == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *(UINT8 *)Data = value;
    *Size = 1u;
    *Attributes = g_variable_attributes;
    return EFI_SUCCESS;
}

static EFI_STATUS mock_verify(VOID *Buffer, UINT32 Size)
{
    ++g_verify_calls;
    if (Buffer != g_source || Size != g_source_size) return EFI_SECURITY_VIOLATION;
    return g_verify_status;
}

EFI_STATUS EFIAPI shim_chain_test_verify_thunk(
    test_shim_verify_fn Verify, VOID *Buffer, UINT32 Size)
{
    return Verify(Buffer, Size);
}

static EFI_STATUS EFIAPI mock_authentication(
    const test_security2_protocol_t *This, const EFI_DEVICE_PATH_PROTOCOL *Path,
    VOID *Buffer, UINTN Size, BOOLEAN BootPolicy)
{
    (void)This; (void)Path; (void)Buffer; (void)Size; (void)BootPolicy;
    ++g_original_calls;
    if (g_auth_reenter) {
        g_auth_reenter = 0;
        return g_security2.FileAuthentication(
            This, Path, Buffer, Size, BootPolicy);
    }
    return g_original_status;
}

static EFI_STATUS EFIAPI mock_load(
    BOOLEAN BootPolicy, EFI_HANDLE Parent, EFI_DEVICE_PATH_PROTOCOL *Path,
    VOID *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle)
{
    EFI_STATUS policy;
    (void)BootPolicy; (void)Parent; (void)Path;
    ++g_load_calls;
    if (g_nested_load) {
        g_nested_load = 0;
        g_nested_status = shim_chain_load_image(
            FALSE, NULL, g_path, SourceBuffer, SourceSize, ImageHandle);
    }
    if (g_auth_buffer != NULL) {
        policy = g_security2.FileAuthentication(
            &g_security2, g_path, g_auth_buffer, g_auth_size, FALSE);
        if (EFI_ERROR(policy)) return policy;
    }
    if (EFI_ERROR(g_load_status)) return g_load_status;
    *ImageHandle = g_child;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_locate_protocol(
    EFI_GUID *Guid, VOID *Registration, VOID **Interface)
{
    (void)Registration;
    if (guid_is(Guid, &g_expected_shim_guid)) {
        if (!g_shim_present) return EFI_NOT_FOUND;
        *Interface = &g_shim;
        return EFI_SUCCESS;
    }
    if (guid_is(Guid, &g_expected_security2_guid)) {
        if (!g_security2_present) return EFI_NOT_FOUND;
        *Interface = &g_security2;
        return EFI_SUCCESS;
    }
    if (guid_is(Guid, &g_old_security2_guid)) {
        ++g_security2_old_guid_calls;
        return EFI_NOT_FOUND;
    }
    return EFI_NOT_FOUND;
}

static int expect(int condition, const char *message)
{
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static int expect_trace(const char *ExpectedPhase, EFI_STATUS ExpectedStatus,
                        const char *Message)
{
    const char *phase = NULL;
    EFI_STATUS status = EFI_NOT_STARTED;
    shim_chain_get_last_load_trace(&phase, &status);
    return expect(phase != NULL && strcmp(phase, ExpectedPhase) == 0 &&
                  status == ExpectedStatus, Message);
}

static void reset_fixture(void)
{
    union { test_get_variable_fn function; VOID *opaque; } get_variable;
    memset(&g_boot, 0, sizeof(g_boot));
    memset(&g_runtime, 0, sizeof(g_runtime));
    memset(&g_system, 0, sizeof(g_system));
    g_boot.LoadImage = mock_load;
    g_boot.LocateProtocol = mock_locate_protocol;
    g_runtime.GetVariable = NULL;
    get_variable.function = mock_get_variable;
    g_runtime.GetVariable = get_variable.opaque;
    g_system.RuntimeServices = &g_runtime;
    gBS = &g_boot;
    gST = &g_system;
    g_shim.Verify = mock_verify;
    g_security2.FileAuthentication = mock_authentication;
    g_secure_boot = 1u;
    g_setup_mode = 0u;
    g_mok_state = 0u;
    g_mok_present = 0;
    g_variable_attributes = 6u;
    g_variable_size = 1u;
    g_variable_status = EFI_SUCCESS;
    g_shim_present = 1;
    g_security2_present = 1;
    g_verify_status = EFI_SUCCESS;
    g_original_status = EFI_SECURITY_VIOLATION;
    g_load_status = EFI_SUCCESS;
    g_source = (VOID *)(UINTN)0x2345u;
    g_source_size = 64u;
    g_auth_buffer = g_source;
    g_auth_size = g_source_size;
    g_nested_load = 0;
    g_auth_reenter = 0;
    g_nested_status = EFI_SUCCESS;
    g_verify_calls = 0;
    g_original_calls = 0;
    g_load_calls = 0;
    g_security2_old_guid_calls = 0;
}

int main(void)
{
    EFI_HANDLE image;
    EFI_STATUS status;

    if (!expect(shim_verify_abi_self_test() == EFI_SUCCESS,
                "SysV thunk preserves Microsoft x64 nonvolatile registers")) return 1;

    reset_fixture();
    if (!expect(shim_chain_is_enforcing(),
                "Secure Boot plus shim with absent MOK override enforces")) return 1;
    g_mok_present = 1;
    g_mok_state = 1u;
    if (!expect(!shim_chain_is_enforcing(), "MokSBStateRT disables enforcement")) return 1;
    g_mok_present = 1;
    g_mok_state = 0u;
    g_variable_attributes = 2u;
    if (!expect(!shim_chain_is_enforcing(), "MokSBStateRT attributes are validated")) return 1;
    g_mok_present = 0;
    g_variable_attributes = 6u;
    g_setup_mode = 1u;
    if (!expect(!shim_chain_is_enforcing(), "SetupMode disables enforcement")) return 1;
    g_setup_mode = 0u;
    g_mok_present = 0;
    g_secure_boot = 0u;
    if (!expect(!shim_chain_is_enforcing(), "SecureBoot zero disables enforcement")) return 1;
    g_secure_boot = 1u;
    g_variable_attributes = 2u;
    if (!expect(!shim_chain_is_enforcing(), "variable attributes are validated")) return 1;
    g_variable_attributes = 6u;
    g_variable_status = EFI_DEVICE_ERROR;
    if (!expect(!shim_chain_is_enforcing(), "variable read errors fail closed")) return 1;

    reset_fixture();
    g_shim_present = 0;
    g_original_status = EFI_SUCCESS;
    status = shim_chain_load_image(FALSE, NULL, g_path, NULL, 0u, &image);
    if (!expect(status == EFI_SUCCESS && image == g_child && g_load_calls == 1u,
                "without shim, use firmware LoadImage unchanged") ||
        !expect_trace("firmware_load", EFI_SUCCESS,
                      "no-shim load trace records firmware result")) return 1;

    reset_fixture();
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SUCCESS && image == g_child &&
                    g_verify_calls == 2u && g_original_calls == 1u &&
                    g_load_calls == 1u,
                "MOK verified source can pass shim Security2 bridge") ||
        !expect(g_security2.FileAuthentication == mock_authentication,
                "Security2 callback restored before returning") ||
        !expect(g_security2_old_guid_calls == 0u,
                "production requests canonical Security2 GUID, never old 8E GUID") ||
        !expect_trace("firmware_load", EFI_SUCCESS,
                      "successful shim load records firmware LoadImage result")) return 1;

    reset_fixture();
    g_original_status = EFI_SUCCESS;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SUCCESS && image == g_child &&
                    g_verify_calls == 1u && g_original_calls == 1u,
                "firmware policy success is preserved after preverification")) return 1;

    reset_fixture();
    g_original_status = EFI_ACCESS_DENIED;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SUCCESS && image == g_child &&
                    g_verify_calls == 2u && g_original_calls == 1u,
                "exact MOK-verified source can bridge firmware access denial")) return 1;

    reset_fixture();
    g_secure_boot = 0u;
    g_original_status = EFI_SUCCESS;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SUCCESS && image == g_child &&
                    g_verify_calls == 0u && g_load_calls == 1u &&
                    g_security2.FileAuthentication == mock_authentication,
                "explicit Secure Boot-off state uses firmware loading without shim override") ||
        !expect_trace("firmware_direct", EFI_SUCCESS,
                      "Secure Boot-off trace identifies direct firmware load")) return 1;

    reset_fixture();
    g_mok_present = 1;
    g_mok_state = 1u;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SECURITY_VIOLATION && g_verify_calls == 0u &&
                    g_load_calls == 0u,
                "MokSBStateRT cannot disable child verification") ||
        !expect_trace("enforcement", EFI_SECURITY_VIOLATION,
                      "enforcement gate failure retains its policy status")) return 1;

    reset_fixture();
    g_verify_status = EFI_ACCESS_DENIED;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SECURITY_VIOLATION && image == NULL &&
                    g_load_calls == 0u,
                "shim Verify failure prevents firmware LoadImage") ||
        !expect_trace("shim_verify", EFI_ACCESS_DENIED,
                      "trace preserves actual shim Verify denial before fail-closed return")) return 1;

    reset_fixture();
    g_security2_present = 0;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SECURITY_VIOLATION && g_load_calls == 0u,
                "missing Security2 policy fails closed under shim") ||
        !expect_trace("security2_lookup", EFI_NOT_FOUND,
                      "Security2 lookup trace distinguishes absent policy protocol")) return 1;

    reset_fixture();
    g_auth_buffer = (VOID *)(UINTN)0x9999u;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SECURITY_VIOLATION && g_verify_calls == 1u &&
                    g_security2.FileAuthentication == mock_authentication,
                "different source buffer cannot use shim authorization") ||
        !expect(g_load_calls == 1u, "foreign callback denial returns LoadImage")) return 1;

    reset_fixture();
    g_auth_reenter = 1;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SECURITY_VIOLATION && g_verify_calls == 1u &&
                    g_security2.FileAuthentication == mock_authentication,
                "reentrant Security2 callback cannot gain shim authorization")) return 1;

    reset_fixture();
    g_original_status = EFI_DEVICE_ERROR;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_DEVICE_ERROR && g_verify_calls == 1u,
                "non-policy Security2 errors are preserved")) return 1;

    reset_fixture();
    g_original_status = EFI_SUCCESS;
    g_load_status = EFI_LOAD_ERROR;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_LOAD_ERROR && image == NULL &&
                    g_security2.FileAuthentication == mock_authentication,
                "Security2 callback is restored when LoadImage fails") ||
        !expect_trace("firmware_load", EFI_LOAD_ERROR,
                      "firmware denial trace preserves raw LoadImage status")) return 1;
    g_load_status = EFI_SUCCESS;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SUCCESS && image == g_child,
                "shim load guard clears so a later child can load")) return 1;

    reset_fixture();
    g_nested_load = 1;
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   g_source_size, &image);
    if (!expect(status == EFI_SUCCESS && g_nested_status == EFI_ALREADY_STARTED,
                "nested shim loads fail closed") ||
        !expect_trace("firmware_load", EFI_SUCCESS,
                      "outer firmware result replaces nested reentrant trace")) return 1;

    reset_fixture();
    status = shim_chain_load_image(FALSE, NULL, g_path, NULL,
                                   g_source_size, &image);
    if (!expect(status == EFI_SECURITY_VIOLATION && g_load_calls == 0u,
                "shim path rejects null source buffer") ||
        !expect_trace("argument", EFI_SECURITY_VIOLATION,
                      "invalid source is recorded at the argument gate")) return 1;
    reset_fixture();
    status = shim_chain_load_image(FALSE, NULL, g_path, g_source,
                                   0x80000000ULL, &image);
    if (!expect(status == EFI_SECURITY_VIOLATION && g_load_calls == 0u,
                "shim path rejects buffers outside shim Verify signed-size range")) return 1;

    puts("shim_chain_test: PASS");
    return 0;
}
