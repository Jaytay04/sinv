#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "sign.h"

static int g_failures;

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
    ++g_failures; } } while (0)

typedef struct {
    int calls;
    int fail_call;
    int saw_secret;
    int verify_calls;
    int fail_verify_call;
} process_state_t;

static int fake_process(const wchar_t *application, wchar_t *command, void *opaque)
{
    process_state_t *state = (process_state_t *)opaque;
    CHECK(application != NULL);
    ++state->calls;
    if (wcsstr(command, L"test-secret") != NULL) state->saw_secret = 1;
    return state->calls == state->fail_call ? 7 : 0;
}

static int fake_verify(const wchar_t *path, const wchar_t *signer_cert,
                       void *opaque, wchar_t *error, size_t error_count)
{
    process_state_t *state = (process_state_t *)opaque;
    CHECK(path != NULL);
    CHECK(signer_cert != NULL);
    ++state->verify_calls;
    if (state->verify_calls == state->fail_verify_call) {
        if (error != NULL && error_count != 0u) {
            wcscpy_s(error, error_count, L"injected verification rejection");
        }
        return 0;
    }
    return 1;
}

static int join(wchar_t *out, size_t count, const wchar_t *left, const wchar_t *right)
{
    return swprintf_s(out, count, L"%ls\\%ls", left, right) > 0;
}

static int write_bytes(const wchar_t *path, const char *bytes)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD length = (DWORD)strlen(bytes), written = 0;
    int ok = file != INVALID_HANDLE_VALUE &&
             WriteFile(file, bytes, length, &written, NULL) && written == length;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return ok;
}

static int make_temp_directory(wchar_t path[MAX_PATH])
{
    wchar_t temp[MAX_PATH];
    return GetTempPathW(_countof(temp), temp) != 0 &&
           GetTempFileNameW(temp, L"ibg", 0, path) != 0 &&
           DeleteFileW(path) && CreateDirectoryW(path, NULL);
}

static void cleanup(const wchar_t *base, const wchar_t *out)
{
    wchar_t path[MAX_PATH];
    if (join(path, _countof(path), out, L"loader.efi")) DeleteFileW(path);
    if (join(path, _countof(path), out, L"payload.efi")) DeleteFileW(path);
    RemoveDirectoryW(out);
    if (join(path, _countof(path), base, L"signtool.exe")) DeleteFileW(path);
    if (join(path, _countof(path), base, L"mok.pfx")) DeleteFileW(path);
    if (join(path, _countof(path), base, L"mok.cer")) DeleteFileW(path);
    if (join(path, _countof(path), base, L"loader-in.efi")) DeleteFileW(path);
    if (join(path, _countof(path), base, L"payload-in.efi")) DeleteFileW(path);
    RemoveDirectoryW(base);
}

static void run_case(int fail_call, int fail_verify_call, int expect_success)
{
    wchar_t base[MAX_PATH], out[MAX_PATH], paths[5][MAX_PATH], error[256] = L"";
    ib_sign_request_t request;
    ib_sign_process_t process;
    process_state_t state;
    CHECK(make_temp_directory(base));
    CHECK(join(paths[0], MAX_PATH, base, L"signtool.exe"));
    CHECK(join(paths[1], MAX_PATH, base, L"mok.pfx"));
    CHECK(join(paths[2], MAX_PATH, base, L"loader-in.efi"));
    CHECK(join(paths[3], MAX_PATH, base, L"payload-in.efi"));
    CHECK(join(paths[4], MAX_PATH, base, L"mok.cer"));
    CHECK(join(out, MAX_PATH, base, L"signed"));
    CHECK(write_bytes(paths[0], "tool"));
    CHECK(write_bytes(paths[1], "pfx"));
    CHECK(write_bytes(paths[2], "loader"));
    CHECK(write_bytes(paths[3], "payload"));
    CHECK(write_bytes(paths[4], "cert"));
    CHECK(SetEnvironmentVariableW(L"IB_TEST_PFX_PASSWORD", L"test-secret"));
    ZeroMemory(&request, sizeof(request));
    request.signtool_path = paths[0]; request.pfx_path = paths[1];
    request.signer_cert_path = paths[4];
    request.password_env = L"IB_TEST_PFX_PASSWORD";
    request.loader_path = paths[2]; request.payload_path = paths[3];
    request.out_dir = out;
    ZeroMemory(&state, sizeof(state));
    state.fail_call = fail_call;
    state.fail_verify_call = fail_verify_call;
    process.run_process = fake_process; process.context = &state;
    request.verify_signature = fake_verify; request.verify_context = &state;
    CHECK(ib_sign_images(&request, &process, error, _countof(error)) == expect_success);
    CHECK(state.saw_secret);
    if (expect_success) {
        CHECK(state.calls == 2);
        CHECK(state.verify_calls == 2);
        CHECK(GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES);
    } else {
        CHECK(GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES);
    }
    SetEnvironmentVariableW(L"IB_TEST_PFX_PASSWORD", NULL);
    cleanup(base, out);
}

int main(void)
{
    run_case(0, 0, 1);
    run_case(2, 0, 0);
    run_case(0, 2, 0);
    if (g_failures != 0) {
        fprintf(stderr, "%d install sign test(s) failed\n", g_failures);
        return 1;
    }
    puts("install sign tests passed");
    return 0;
}
