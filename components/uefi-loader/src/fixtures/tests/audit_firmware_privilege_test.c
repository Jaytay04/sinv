#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>

#include "platform_win.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; \
} } while (0)

typedef struct {
    DWORD last_error;
    int fail_open;
    int fail_lookup;
    int fail_enable;
    int privilege_assigned;
    int fail_restore;
    int enabled;
    uint32_t open_calls;
    uint32_t lookup_calls;
    uint32_t enable_calls;
    uint32_t restore_calls;
    uint32_t close_calls;
} fixture_t;

static HANDLE fake_get_current_process(void *context)
{
    (void)context;
    return (HANDLE)(uintptr_t)1u;
}

static BOOL fake_open_process_token(void *context, HANDLE process,
                                    DWORD access, PHANDLE token)
{
    fixture_t *fixture = (fixture_t *)context;

    fixture->open_calls++;
    if (process != (HANDLE)(uintptr_t)1u ||
        access != (TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY) ||
        fixture->fail_open) {
        fixture->last_error = ERROR_ACCESS_DENIED;
        return FALSE;
    }
    *token = (HANDLE)(uintptr_t)2u;
    return TRUE;
}

static BOOL fake_lookup_privilege_value(void *context, LPCWSTR system_name,
                                        LPCWSTR name, PLUID luid)
{
    fixture_t *fixture = (fixture_t *)context;

    fixture->lookup_calls++;
    if (system_name != NULL ||
        wcscmp(name, L"SeSystemEnvironmentPrivilege") != 0 ||
        fixture->fail_lookup) {
        fixture->last_error = ERROR_NO_SUCH_PRIVILEGE;
        return FALSE;
    }
    luid->LowPart = 42u;
    luid->HighPart = 0;
    return TRUE;
}

static BOOL fake_adjust_token_privileges(
    void *context, HANDLE token, BOOL disable_all,
    PTOKEN_PRIVILEGES new_state, DWORD buffer_length,
    PTOKEN_PRIVILEGES previous_state, PDWORD return_length)
{
    fixture_t *fixture = (fixture_t *)context;

    if (token != (HANDLE)(uintptr_t)2u || disable_all || new_state == NULL) {
        fixture->last_error = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    if (return_length != NULL && previous_state != NULL) {
        fixture->enable_calls++;
        if (buffer_length < sizeof(*previous_state) || fixture->fail_enable) {
            fixture->last_error = ERROR_ACCESS_DENIED;
            return FALSE;
        }
        if (!fixture->privilege_assigned) {
            *return_length = 0u;
            fixture->last_error = ERROR_NOT_ALL_ASSIGNED;
            return TRUE;
        }
        if (fixture->enabled) {
            *return_length = 0u;
            fixture->last_error = ERROR_SUCCESS;
            return TRUE;
        }
        previous_state->PrivilegeCount = 1u;
        previous_state->Privileges[0].Luid.LowPart = 42u;
        previous_state->Privileges[0].Luid.HighPart = 0;
        previous_state->Privileges[0].Attributes = fixture->enabled
            ? SE_PRIVILEGE_ENABLED : 0u;
        *return_length = (DWORD)sizeof(*previous_state);
        fixture->enabled = 1;
        fixture->last_error = ERROR_SUCCESS;
        return TRUE;
    }

    fixture->restore_calls++;
    if (fixture->fail_restore) {
        fixture->last_error = ERROR_ACCESS_DENIED;
        return FALSE;
    }
    fixture->enabled =
        (new_state->Privileges[0].Attributes & SE_PRIVILEGE_ENABLED) != 0u;
    fixture->last_error = ERROR_SUCCESS;
    return TRUE;
}

static BOOL fake_close_handle(void *context, HANDLE handle)
{
    fixture_t *fixture = (fixture_t *)context;

    if (handle != (HANDLE)(uintptr_t)2u) {
        fixture->last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    fixture->close_calls++;
    return TRUE;
}

static DWORD fake_get_last_error(void *context)
{
    return ((fixture_t *)context)->last_error;
}

static void fake_set_last_error(void *context, DWORD error)
{
    ((fixture_t *)context)->last_error = error;
}

static void make_api(fixture_t *fixture,
                    ib_audit_firmware_privilege_api_t *api)
{
    memset(api, 0, sizeof(*api));
    api->context = fixture;
    api->get_current_process = fake_get_current_process;
    api->open_process_token = fake_open_process_token;
    api->lookup_privilege_value = fake_lookup_privilege_value;
    api->adjust_token_privileges = fake_adjust_token_privileges;
    api->close_handle = fake_close_handle;
    api->get_last_error = fake_get_last_error;
    api->set_last_error = fake_set_last_error;
}

static int test_enable_and_restore_prior_disabled_state(void)
{
    fixture_t fixture;
    ib_audit_firmware_privilege_api_t api;
    ib_audit_firmware_privilege_t privilege;
    DWORD error = ERROR_SUCCESS;

    memset(&fixture, 0, sizeof(fixture));
    fixture.privilege_assigned = 1;
    make_api(&fixture, &api);
    CHECK(ib_audit_windows_firmware_privilege_begin_with_api(
              &privilege, &api, &error));
    CHECK(error == ERROR_SUCCESS && fixture.enabled == 1);
    CHECK(privilege.active && privilege.restore_required);
    CHECK(ib_audit_windows_firmware_privilege_end(&privilege, &error));
    CHECK(error == ERROR_SUCCESS && fixture.enabled == 0);
    CHECK(fixture.enable_calls == 1u && fixture.restore_calls == 1u);
    CHECK(fixture.close_calls == 1u);
    return 0;
}

static int test_already_enabled_privilege_needs_no_restore(void)
{
    fixture_t fixture;
    ib_audit_firmware_privilege_api_t api;
    ib_audit_firmware_privilege_t privilege;
    DWORD error = ERROR_SUCCESS;

    memset(&fixture, 0, sizeof(fixture));
    fixture.privilege_assigned = 1;
    fixture.enabled = 1;
    make_api(&fixture, &api);
    CHECK(ib_audit_windows_firmware_privilege_begin_with_api(
              &privilege, &api, &error));
    CHECK(error == ERROR_SUCCESS && fixture.enabled == 1);
    CHECK(privilege.active && !privilege.restore_required);
    CHECK(ib_audit_windows_firmware_privilege_end(&privilege, &error));
    CHECK(fixture.enabled == 1 && fixture.restore_calls == 0u);
    CHECK(fixture.close_calls == 1u);
    return 0;
}

static int test_begin_failures_and_unassigned_privilege(void)
{
    fixture_t fixture;
    ib_audit_firmware_privilege_api_t api;
    ib_audit_firmware_privilege_t privilege;
    DWORD error = ERROR_SUCCESS;

    memset(&fixture, 0, sizeof(fixture));
    fixture.privilege_assigned = 1;
    fixture.fail_open = 1;
    make_api(&fixture, &api);
    CHECK(!ib_audit_windows_firmware_privilege_begin_with_api(
        &privilege, &api, &error));
    CHECK(error == ERROR_ACCESS_DENIED && fixture.close_calls == 0u);

    memset(&fixture, 0, sizeof(fixture));
    fixture.privilege_assigned = 1;
    fixture.fail_lookup = 1;
    make_api(&fixture, &api);
    CHECK(!ib_audit_windows_firmware_privilege_begin_with_api(
        &privilege, &api, &error));
    CHECK(error == ERROR_NO_SUCH_PRIVILEGE && fixture.close_calls == 1u);

    memset(&fixture, 0, sizeof(fixture));
    fixture.privilege_assigned = 1;
    fixture.fail_enable = 1;
    make_api(&fixture, &api);
    CHECK(!ib_audit_windows_firmware_privilege_begin_with_api(
        &privilege, &api, &error));
    CHECK(error == ERROR_ACCESS_DENIED && fixture.close_calls == 1u);

    memset(&fixture, 0, sizeof(fixture));
    fixture.privilege_assigned = 0;
    make_api(&fixture, &api);
    CHECK(!ib_audit_windows_firmware_privilege_begin_with_api(
        &privilege, &api, &error));
    CHECK(error == ERROR_NOT_ALL_ASSIGNED);
    CHECK(fixture.enabled == 0 && fixture.close_calls == 1u);
    return 0;
}

static int test_restore_failure_is_reported(void)
{
    fixture_t fixture;
    ib_audit_firmware_privilege_api_t api;
    ib_audit_firmware_privilege_t privilege;
    DWORD error = ERROR_SUCCESS;

    memset(&fixture, 0, sizeof(fixture));
    fixture.privilege_assigned = 1;
    fixture.fail_restore = 1;
    make_api(&fixture, &api);
    CHECK(ib_audit_windows_firmware_privilege_begin_with_api(
              &privilege, &api, &error));
    CHECK(!ib_audit_windows_firmware_privilege_end(&privilege, &error));
    CHECK(error == ERROR_ACCESS_DENIED);
    CHECK(fixture.restore_calls == 1u && fixture.close_calls == 1u);
    return 0;
}

int main(void)
{
    if (test_enable_and_restore_prior_disabled_state() != 0 ||
        test_already_enabled_privilege_needs_no_restore() != 0 ||
        test_begin_failures_and_unassigned_privilege() != 0 ||
        test_restore_failure_is_reported() != 0) return 1;
    puts("PASS: firmware-environment privilege is scoped and restored");
    return 0;
}
