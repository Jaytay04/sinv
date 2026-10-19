/* shim_chain.c - Bind firmware child loading to shim's MOK verification. */
#include "shim_chain.h"

#define EFI_EXPECTED_VARIABLE_ATTRS     (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

static EFI_GUID g_global_variable_guid = {
    0x8be4df61u, 0x93cau, 0x11d2u,
    { 0xaau, 0x0du, 0x00u, 0xe0u, 0x98u, 0x03u, 0x2bu, 0x8cu }
};
static EFI_GUID g_shim_lock_guid = {
    0x605dab50u, 0xe046u, 0x4300u,
    { 0xabu, 0xb6u, 0x3du, 0xd8u, 0x10u, 0xddu, 0x8bu, 0x23u }
};
static EFI_GUID g_security2_guid = {
    0x94ab2f58u, 0x1438u, 0x4ef1u,
    /* EFI_SECURITY2_ARCH_PROTOCOL_GUID, PI Security2 architectural protocol. */
    { 0x91u, 0x52u, 0x18u, 0x94u, 0x1au, 0x3au, 0x0eu, 0x68u }
};

typedef EFI_STATUS (EFIAPI *efi_get_variable_fn)(
    CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 *Attributes,
    UINTN *DataSize, VOID *Data);
typedef EFI_STATUS (*shim_verify_fn)(VOID *Buffer, UINT32 Size);
typedef struct { shim_verify_fn Verify; } shim_lock_protocol_t;
typedef struct security2_protocol_tag security2_protocol_t;
typedef EFI_STATUS (EFIAPI *security2_file_authentication_fn)(
    const security2_protocol_t *This, const EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    VOID *FileBuffer, UINTN FileSize, BOOLEAN BootPolicy);
struct security2_protocol_tag {
    security2_file_authentication_fn FileAuthentication;
};

#if defined(IB_SHIM_CHAIN_HOST_TEST)
extern EFI_STATUS EFIAPI shim_chain_test_verify_thunk(
    shim_verify_fn Verify, VOID *Buffer, UINT32 Size);
#define call_shim_verify shim_chain_test_verify_thunk
#else
extern EFI_STATUS EFIAPI shim_verify_sysv_thunk(
    shim_verify_fn Verify, VOID *Buffer, UINT32 Size);
#define call_shim_verify shim_verify_sysv_thunk
#endif

typedef struct {
    security2_file_authentication_fn Original;
    VOID *Buffer;
    UINTN Size;
    shim_verify_fn Verify;
    BOOLEAN Active;
    BOOLEAN CallbackActive;
    BOOLEAN CallbackReentered;
} security2_hook_state_t;
static security2_hook_state_t g_security2_hook;
static BOOLEAN g_shim_load_active;
static const char *g_last_load_phase = "not_applicable";
static EFI_STATUS g_last_load_status = EFI_NOT_STARTED;

static void set_last_load_trace(const char *Phase, EFI_STATUS Status)
{
    g_last_load_phase = Phase;
    g_last_load_status = Status;
}

void shim_chain_get_last_load_trace(const char **Phase, EFI_STATUS *Status)
{
    if (Phase != NULL) *Phase = g_last_load_phase;
    if (Status != NULL) *Status = g_last_load_status;
}

static int locate_shim(shim_lock_protocol_t **Protocol,
                       EFI_STATUS *ApiStatus)
{
    VOID *interface = NULL;
    EFI_STATUS status;
    if (ApiStatus != NULL) *ApiStatus = EFI_NOT_STARTED;
    if (Protocol == NULL || gBS == NULL || gBS->LocateProtocol == NULL) {
        return 0;
    }
    *Protocol = NULL;
    status = gBS->LocateProtocol(&g_shim_lock_guid, NULL, &interface);
    if (ApiStatus != NULL) *ApiStatus = status;
    if (status == EFI_NOT_FOUND) return 0;
    if (status != EFI_SUCCESS || interface == NULL) {
        if (ApiStatus != NULL && status == EFI_SUCCESS) {
            *ApiStatus = EFI_SECURITY_VIOLATION;
        }
        return -1;
    }
    *Protocol = (shim_lock_protocol_t *)interface;
    if ((*Protocol)->Verify == NULL) {
        if (ApiStatus != NULL) *ApiStatus = EFI_SECURITY_VIOLATION;
        return -1;
    }
    return 1;
}

static int read_firmware_byte(
    CHAR16 *Name, EFI_GUID *Guid, UINT8 *Value, EFI_STATUS *ApiStatus)
{
    union { VOID *opaque; efi_get_variable_fn function; } conversion;
    efi_get_variable_fn get_variable;
    UINT32 attributes = 0u;
    UINTN data_size = sizeof(*Value);
    EFI_STATUS status;

    if (ApiStatus != NULL) *ApiStatus = EFI_NOT_STARTED;
    if (Name == NULL || Guid == NULL || Value == NULL || gST == NULL ||
        gST->RuntimeServices == NULL ||
        gST->RuntimeServices->GetVariable == NULL) {
        if (ApiStatus != NULL) *ApiStatus = EFI_SECURITY_VIOLATION;
        return -1;
    }
    conversion.opaque = gST->RuntimeServices->GetVariable;
    get_variable = conversion.function;
    status = get_variable(Name, Guid, &attributes,
                          &data_size, Value);
    if (ApiStatus != NULL) *ApiStatus = status;
    if (status == EFI_NOT_FOUND) return 0;
    if (status != EFI_SUCCESS || data_size != sizeof(*Value) ||
        attributes != EFI_EXPECTED_VARIABLE_ATTRS) {
        if (ApiStatus != NULL && status == EFI_SUCCESS) {
            *ApiStatus = EFI_SECURITY_VIOLATION;
        }
        return -1;
    }
    return 1;
}

static int read_secure_boot_state(UINT8 *SecureBoot, EFI_STATUS *ApiStatus)
{
    static CHAR16 secure_boot_name[] = {
        'S','e','c','u','r','e','B','o','o','t',0
    };
    return read_firmware_byte(secure_boot_name, &g_global_variable_guid,
                              SecureBoot, ApiStatus);
}

int shim_chain_is_enforcing(void)
{
    static CHAR16 setup_mode_name[] = {
        'S','e','t','u','p','M','o','d','e',0
    };
    static CHAR16 mok_sb_state_name[] = {
        'M','o','k','S','B','S','t','a','t','e','R','T',0
    };
    UINT8 secure_boot = 0u;
    UINT8 setup_mode = 0u;
    UINT8 mok_state = 0u;
    int status;
    shim_lock_protocol_t *shim = NULL;

    status = read_secure_boot_state(&secure_boot, NULL);
    if (status != 1 || secure_boot != 1u) return 0;
    if (read_firmware_byte(setup_mode_name, &g_global_variable_guid,
                           &setup_mode, NULL) != 1 || setup_mode != 0u) {
        return 0;
    }
    status = read_firmware_byte(mok_sb_state_name, &g_shim_lock_guid,
                                &mok_state, NULL);
    if (status < 0 || (status == 1 && mok_state != 0u)) return 0;
    return locate_shim(&shim, NULL) == 1 && shim != NULL && shim->Verify != NULL;
}

static EFI_STATUS EFIAPI shim_security2_file_authentication(
    const security2_protocol_t *This, const EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    VOID *FileBuffer, UINTN FileSize, BOOLEAN BootPolicy)
{
    EFI_STATUS status;
    if (!g_security2_hook.Active || g_security2_hook.Original == NULL) {
        return EFI_SECURITY_VIOLATION;
    }
    if (g_security2_hook.CallbackActive) {
        g_security2_hook.CallbackReentered = TRUE;
        return EFI_SECURITY_VIOLATION;
    }
    g_security2_hook.CallbackActive = TRUE;
    g_security2_hook.CallbackReentered = FALSE;
    status = g_security2_hook.Original(
        This, DevicePath, FileBuffer, FileSize, BootPolicy);
    if (!g_security2_hook.CallbackReentered &&
        (status == EFI_SECURITY_VIOLATION || status == EFI_ACCESS_DENIED) &&
        FileBuffer == g_security2_hook.Buffer &&
        FileSize == g_security2_hook.Size && FileBuffer != NULL &&
        FileSize != 0u && FileSize <= 0x7fffffffu &&
        call_shim_verify(g_security2_hook.Verify, FileBuffer,
                         (UINT32)FileSize) == EFI_SUCCESS) {
        status = EFI_SUCCESS;
    }
    if (g_security2_hook.CallbackReentered) {
        status = EFI_SECURITY_VIOLATION;
    }
    g_security2_hook.CallbackActive = FALSE;
    g_security2_hook.CallbackReentered = FALSE;
    return status;
}

EFI_STATUS EFIAPI shim_chain_load_image(
    BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, VOID *SourceBuffer,
    UINTN SourceSize, EFI_HANDLE *ImageHandle)
{
    shim_lock_protocol_t *shim = NULL;
    VOID *security2_interface = NULL;
    security2_protocol_t *security2;
    EFI_STATUS status;
    EFI_STATUS api_status;
    UINT8 secure_boot = 0u;
    int shim_state;
    int secure_boot_status;

    set_last_load_trace("not_applicable", EFI_NOT_STARTED);
    if (gBS == NULL || gBS->LoadImage == NULL || ImageHandle == NULL) {
        set_last_load_trace("argument", EFI_INVALID_PARAMETER);
        return EFI_INVALID_PARAMETER;
    }
    *ImageHandle = NULL;
    if (g_shim_load_active) {
        set_last_load_trace("reentrant", EFI_ALREADY_STARTED);
        return EFI_ALREADY_STARTED;
    }

    shim_state = locate_shim(&shim, &api_status);
    if (shim_state == 0) {
        status = gBS->LoadImage(BootPolicy, ParentImageHandle, DevicePath,
                                SourceBuffer, SourceSize, ImageHandle);
        set_last_load_trace("firmware_load", status);
        return status;
    }
    if (shim_state < 0) {
        set_last_load_trace("shim_lookup", api_status);
        return EFI_SECURITY_VIOLATION;
    }

    /* Guard every path after discovering shim, including policy queries and
     * the explicit firmware-only Secure Boot-off path. */
    g_shim_load_active = TRUE;
    secure_boot_status = read_secure_boot_state(&secure_boot, &api_status);
    if (secure_boot_status != 1) {
        set_last_load_trace("secure_boot_read", api_status);
        g_shim_load_active = FALSE;
        return EFI_SECURITY_VIOLATION;
    }
    if (secure_boot == 0u) {
        status = gBS->LoadImage(BootPolicy, ParentImageHandle, DevicePath,
                                SourceBuffer, SourceSize, ImageHandle);
        set_last_load_trace("firmware_direct", status);
        g_shim_load_active = FALSE;
        return status;
    }
    if (secure_boot != 1u || !shim_chain_is_enforcing()) {
        set_last_load_trace("enforcement", EFI_SECURITY_VIOLATION);
        g_shim_load_active = FALSE;
        return EFI_SECURITY_VIOLATION;
    }
    if (SourceBuffer == NULL || SourceSize == 0u ||
        SourceSize > 0x7fffffffu) {
        set_last_load_trace("argument", EFI_SECURITY_VIOLATION);
        g_shim_load_active = FALSE;
        return EFI_SECURITY_VIOLATION;
    }
    status = call_shim_verify(shim->Verify, SourceBuffer,
                              (UINT32)SourceSize);
    if (status != EFI_SUCCESS) {
        set_last_load_trace("shim_verify", status);
        g_shim_load_active = FALSE;
        return EFI_SECURITY_VIOLATION;
    }
    if (gBS->LocateProtocol == NULL) {
        set_last_load_trace("security2_lookup", EFI_SECURITY_VIOLATION);
        g_shim_load_active = FALSE;
        return EFI_SECURITY_VIOLATION;
    }
    status = gBS->LocateProtocol(&g_security2_guid, NULL,
                                 &security2_interface);
    if (status != EFI_SUCCESS || security2_interface == NULL) {
        set_last_load_trace("security2_lookup",
            status != EFI_SUCCESS ? status : EFI_SECURITY_VIOLATION);
        g_shim_load_active = FALSE;
        return EFI_SECURITY_VIOLATION;
    }
    security2 = (security2_protocol_t *)security2_interface;
    if (security2->FileAuthentication == NULL ||
        security2->FileAuthentication == shim_security2_file_authentication) {
        set_last_load_trace("security2_interface", EFI_SECURITY_VIOLATION);
        g_shim_load_active = FALSE;
        return EFI_SECURITY_VIOLATION;
    }

    g_security2_hook.Original = security2->FileAuthentication;
    g_security2_hook.Buffer = SourceBuffer;
    g_security2_hook.Size = SourceSize;
    g_security2_hook.Verify = shim->Verify;
    g_security2_hook.Active = TRUE;
    g_security2_hook.CallbackActive = FALSE;
    g_security2_hook.CallbackReentered = FALSE;
    security2->FileAuthentication = shim_security2_file_authentication;

    status = gBS->LoadImage(BootPolicy, ParentImageHandle, DevicePath,
                            SourceBuffer, SourceSize, ImageHandle);

    /* Restore firmware policy before callers can StartImage. */
    security2->FileAuthentication = g_security2_hook.Original;
    g_security2_hook.Active = FALSE;
    g_security2_hook.CallbackActive = FALSE;
    g_security2_hook.CallbackReentered = FALSE;
    g_security2_hook.Original = NULL;
    g_security2_hook.Buffer = NULL;
    g_security2_hook.Size = 0u;
    g_security2_hook.Verify = NULL;
    g_shim_load_active = FALSE;
    /* An inner rejected LoadImage may have updated the trace while firmware
     * was running. The outer operation owns the final result. */
    set_last_load_trace("firmware_load", status);
    return status;
}
