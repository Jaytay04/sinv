#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "sign.h"
#include "stage.h"
#include "oneshot.h"
#include "bootnext.h"

typedef struct {
    const wchar_t *signer_cert;
} verify_context_t;

static void usage(void)
{
    fwprintf(stderr,
        L"usage:\n"
        L"  ib-install sign --signtool PATH --pfx PATH --password-env NAME "
        L"--signer-cert DER --loader PATH --payload PATH --out-dir DIR\n"
        L"  ib-install stage --signer-cert DER --shim PATH --loader PATH "
        L"--log PATH --payload PATH [--esp-volume auto|VOLUME | --test-root DIR]\n"
        L"  ib-install oneshot --mode replace|verify|bootnext|off "
        L"[--profile legacy|inventory|shim-inventory|vm-direct] "
        L"[--esp-volume auto|VOLUME | --test-root DIR] "
        L"[--partition-number N --partition-start N --partition-size N "
        L"--partition-guid GUID]\n"
        L"  vm-direct is only for disposable VM fixtures; never use it for physical deployment.\n"
        L"  ib-install enroll-mok\n");
}

static const wchar_t *option_value(int argc, wchar_t **argv, const wchar_t *name)
{
    int i;
    const wchar_t *found = NULL;
    for (i = 2; i < argc; ++i) {
        if (wcscmp(argv[i], name) == 0) {
            if (found != NULL || i + 1 >= argc || wcsncmp(argv[i + 1], L"--", 2u) == 0) {
                return NULL;
            }
            found = argv[++i];
        }
    }
    return found;
}

static size_t option_count(int argc, wchar_t **argv, const wchar_t *name)
{
    int i;
    size_t count = 0u;
    for (i = 2; i < argc; i += 2) {
        if (wcscmp(argv[i], name) == 0) ++count;
    }
    return count;
}

static int only_known_options(int argc, wchar_t **argv,
                              const wchar_t *const *known, size_t known_count)
{
    int i;
    for (i = 2; i < argc; i += 2) {
        size_t j;
        int match = 0;
        if (i + 1 >= argc) return 0;
        for (j = 0; j < known_count; ++j) {
            if (wcscmp(argv[i], known[j]) == 0) {
                match = 1;
                break;
            }
        }
        if (!match) return 0;
    }
    return 1;
}

static int verify_for_stage(const wchar_t *path, void *opaque,
                            wchar_t *error, size_t error_count)
{
    verify_context_t *context = (verify_context_t *)opaque;
    return ib_sign_verify_file(path, context->signer_cert, error, error_count);
}

static int verify_for_sign(const wchar_t *path, const wchar_t *signer_cert,
                           void *opaque, wchar_t *error, size_t error_count)
{
    (void)opaque;
    return ib_sign_verify_file(path, signer_cert, error, error_count);
}

static int run_sign(int argc, wchar_t **argv)
{
    static const wchar_t *known[] = {
        L"--signtool", L"--pfx", L"--password-env", L"--loader",
        L"--payload", L"--out-dir", L"--signer-cert"
    };
    ib_sign_request_t request;
    ib_sign_process_t process;
    wchar_t error[512] = L"";
    if (!only_known_options(argc, argv, known, _countof(known))) {
        usage();
        return 2;
    }
    ZeroMemory(&request, sizeof(request));
    request.signtool_path = option_value(argc, argv, L"--signtool");
    request.pfx_path = option_value(argc, argv, L"--pfx");
    request.signer_cert_path = option_value(argc, argv, L"--signer-cert");
    request.password_env = option_value(argc, argv, L"--password-env");
    request.loader_path = option_value(argc, argv, L"--loader");
    request.payload_path = option_value(argc, argv, L"--payload");
    request.out_dir = option_value(argc, argv, L"--out-dir");
    request.verify_signature = verify_for_sign;
    request.verify_context = NULL;
    process.run_process = ib_sign_default_run_process;
    process.context = NULL;
    if (!ib_sign_images(&request, &process, error, _countof(error))) {
        fwprintf(stderr, L"sign failed: %ls\n", error);
        return 1;
    }
    wprintf(L"Signed and verified loader.efi and payload.efi in %ls\n", request.out_dir);
    wprintf(L"MOK enrollment is not performed from Windows. On first boot, use "
            L"shim MokManager to enroll the public MOK certificate.\n");
    return 0;
}

static int run_stage(int argc, wchar_t **argv)
{
    static const wchar_t *known[] = {
        L"--signer-cert", L"--shim", L"--loader", L"--log", L"--payload",
        L"--esp-volume", L"--test-root"
    };
    ib_stage_request_t request;
    verify_context_t verify;
    wchar_t found_volume[MAX_PATH];
    wchar_t error[512] = L"";
    const wchar_t *esp;
    if (!only_known_options(argc, argv, known, _countof(known))) {
        usage();
        return 2;
    }
    ZeroMemory(&request, sizeof(request));
    verify.signer_cert = option_value(argc, argv, L"--signer-cert");
    request.shim_path = option_value(argc, argv, L"--shim");
    request.loader_path = option_value(argc, argv, L"--loader");
    request.log_path = option_value(argc, argv, L"--log");
    request.payload_path = option_value(argc, argv, L"--payload");
    request.test_root = option_value(argc, argv, L"--test-root");
    esp = option_value(argc, argv, L"--esp-volume");
    if (esp != NULL && wcscmp(esp, L"auto") == 0) {
        if (!ib_stage_find_esp(found_volume, _countof(found_volume),
                               error, _countof(error))) {
            fwprintf(stderr, L"stage failed: %ls\n", error);
            return 1;
        }
        request.volume_path = found_volume;
    } else {
        request.volume_path = esp;
    }
    request.verify_signature = verify_for_stage;
    request.verify_context = &verify;
    if (!ib_stage_files(&request, error, _countof(error))) {
        fwprintf(stderr, L"stage failed: %ls\n", error);
        return 1;
    }
    wprintf(L"Staged EFI\\insecure-boot with verified read-back manifest.\n");
    wprintf(L"No firmware variables or Microsoft boot-manager files were changed.\n");
    return 0;
}

static int parse_u64(const wchar_t *text, unsigned long long *value)
{
    wchar_t *end = NULL;
    unsigned long long parsed;
    if (text == NULL || text[0] == L'\0' || text[0] == L'-') return 0;
    parsed = _wcstoui64(text, &end, 0);
    if (end == text || *end != L'\0') return 0;
    *value = parsed;
    return 1;
}

static int parse_guid(const wchar_t *text, unsigned char guid[16])
{
    unsigned long a, b, c, d[8];
    int matched;
    if (text == NULL) return 0;
    matched = swscanf_s(text,
        L"{%8lx-%4lx-%4lx-%2lx%2lx-%2lx%2lx%2lx%2lx%2lx%2lx}",
        &a, &b, &c, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7]);
    if (matched != 11) {
        matched = swscanf_s(text,
            L"%8lx-%4lx-%4lx-%2lx%2lx-%2lx%2lx%2lx%2lx%2lx%2lx",
            &a, &b, &c, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7]);
    }
    if (matched != 11) return 0;
    guid[0] = (unsigned char)a; guid[1] = (unsigned char)(a >> 8);
    guid[2] = (unsigned char)(a >> 16); guid[3] = (unsigned char)(a >> 24);
    guid[4] = (unsigned char)b; guid[5] = (unsigned char)(b >> 8);
    guid[6] = (unsigned char)c; guid[7] = (unsigned char)(c >> 8);
    { size_t i; for (i = 0; i < 8u; ++i) guid[8u + i] = (unsigned char)d[i]; }
    return 1;
}

static int partition_from_volume(const wchar_t *volume,
                                 ib_bootnext_partition_t *partition)
{
    wchar_t device[MAX_PATH];
    size_t length;
    HANDLE handle;
    PARTITION_INFORMATION_EX info;
    STORAGE_DEVICE_NUMBER number;
    DISK_GEOMETRY geometry;
    DWORD returned = 0u;
    if (volume == NULL || partition == NULL ||
        wcscpy_s(device, _countof(device), volume) != 0) return 0;
    length = wcslen(device);
    while (length != 0u && device[length - 1u] == L'\\') device[--length] = L'\0';
    handle = CreateFileW(device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    ZeroMemory(&info, sizeof(info)); ZeroMemory(&number, sizeof(number));
    ZeroMemory(&geometry, sizeof(geometry));
    if (!DeviceIoControl(handle, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
                         &info, sizeof(info), &returned, NULL) ||
        info.PartitionStyle != PARTITION_STYLE_GPT ||
        !DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
                         &number, sizeof(number), &returned, NULL) ||
        !DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0,
                         &geometry, sizeof(geometry), &returned, NULL) ||
        geometry.BytesPerSector == 0u ||
        (info.StartingOffset.QuadPart % geometry.BytesPerSector) != 0 ||
        (info.PartitionLength.QuadPart % geometry.BytesPerSector) != 0) {
        CloseHandle(handle); return 0;
    }
    CloseHandle(handle);
    partition->partition_number = number.PartitionNumber;
    partition->partition_start = (unsigned long long)info.StartingOffset.QuadPart /
                                 geometry.BytesPerSector;
    partition->partition_size = (unsigned long long)info.PartitionLength.QuadPart /
                                geometry.BytesPerSector;
    memcpy(partition->partition_guid, &info.Gpt.PartitionId, 16u);
    return partition->partition_number != 0u && partition->partition_size != 0u;
}

static int synthetic_partition(int argc, wchar_t **argv,
                               ib_bootnext_partition_t *partition)
{
    unsigned long long number, start, size;
    if (!parse_u64(option_value(argc, argv, L"--partition-number"), &number) ||
        number == 0u || number > 0xffffffffull ||
        !parse_u64(option_value(argc, argv, L"--partition-start"), &start) ||
        !parse_u64(option_value(argc, argv, L"--partition-size"), &size) || size == 0u ||
        !parse_guid(option_value(argc, argv, L"--partition-guid"),
                    partition->partition_guid)) return 0;
    partition->partition_number = (unsigned long)number;
    partition->partition_start = start;
    partition->partition_size = size;
    return 1;
}

static int run_oneshot(int argc, wchar_t **argv)
{
    static const wchar_t *known[] = {
        L"--mode", L"--profile", L"--esp-volume", L"--test-root", L"--partition-number",
        L"--partition-start", L"--partition-size", L"--partition-guid"
    };
    ib_oneshot_request_t request;
    wchar_t found_volume[MAX_PATH];
    wchar_t error[512] = L"";
    const wchar_t *mode;
    const wchar_t *esp;
    const wchar_t *profile_text;
    ib_oneshot_profile_t profile = IB_ONESHOT_PROFILE_LEGACY;
    ib_bootnext_profile_t boot_profile = IB_BOOTNEXT_PROFILE_LEGACY;
    if (!only_known_options(argc, argv, known, _countof(known))) { usage(); return 2; }
    if (option_count(argc, argv, L"--profile") > 1u) {
        fwprintf(stderr, L"one-shot failed: --profile may be specified only once\n");
        return 2;
    }
    ZeroMemory(&request, sizeof(request));
    request.verify_signature = verify_for_sign;
    mode = option_value(argc, argv, L"--mode");
    profile_text = option_value(argc, argv, L"--profile");
    if (profile_text != NULL) {
        if (wcscmp(profile_text, L"legacy") == 0) {
            profile = IB_ONESHOT_PROFILE_LEGACY;
        } else if (wcscmp(profile_text, L"inventory") == 0) {
            profile = IB_ONESHOT_PROFILE_INVENTORY;
            boot_profile = IB_BOOTNEXT_PROFILE_INVENTORY;
        } else if (wcscmp(profile_text, L"shim-inventory") == 0) {
            profile = IB_ONESHOT_PROFILE_SHIM_INVENTORY;
            boot_profile = IB_BOOTNEXT_PROFILE_SHIM_INVENTORY;
        } else if (wcscmp(profile_text, L"vm-direct") == 0) {
            profile = IB_ONESHOT_PROFILE_VM_DIRECT;
            boot_profile = IB_BOOTNEXT_PROFILE_VM_DIRECT;
        } else {
            fwprintf(stderr, L"one-shot failed: unknown profile %ls\n", profile_text);
            return 2;
        }
    }
    if (profile != IB_ONESHOT_PROFILE_LEGACY && mode != NULL &&
        !ib_oneshot_profile_supports_mode(profile, mode)) {
        fwprintf(stderr, L"one-shot failed: this profile supports only bootnext and off\n");
        return 2;
    }
    request.test_root = option_value(argc, argv, L"--test-root");
    esp = option_value(argc, argv, L"--esp-volume");
    if (esp != NULL && wcscmp(esp, L"auto") == 0) {
        if (!ib_stage_find_esp(found_volume, _countof(found_volume), error, _countof(error))) {
            fwprintf(stderr, L"one-shot failed: %ls\n", error); return 1;
        }
        request.volume_path = found_volume;
    } else request.volume_path = esp;
    if (mode != NULL && wcscmp(mode, L"replace") == 0) {
        ib_oneshot_report_t report;
        if (!ib_oneshot_replace(&request, &report, error, _countof(error))) {
            fwprintf(stderr, L"one-shot replace failed: %ls\n", error); return 1;
        }
        wprintf(L"One-shot shim replacement installed; verified backup retained for loader restore.\n");
        return 0;
    }
    if (mode != NULL && wcscmp(mode, L"verify") == 0) {
        if (!ib_oneshot_cleanup_sibling(&request, error, _countof(error)) ||
            !ib_oneshot_verify(&request, error, _countof(error))) {
            fwprintf(stderr, L"post-boot verification failed: %ls\n", error); return 1;
        }
        wprintf(L"Post-boot restore hash and owned-residue cleanup passed.\n"); return 0;
    }
    if (mode != NULL && (wcscmp(mode, L"bootnext") == 0 || wcscmp(mode, L"off") == 0)) {
        ib_bootnext_partition_t partition;
        ib_bootnext_variable_ops_t ops;
        ib_bootnext_request_t boot_request;
        unsigned short number;
        ZeroMemory(&partition, sizeof(partition));
        if (request.test_root != NULL) {
            if (!synthetic_partition(argc, argv, &partition)) {
                fwprintf(stderr, L"one-shot failed: test mode requires synthetic GPT metadata\n");
                return 2;
            }
            fwprintf(stderr, L"Refusing firmware-variable writes in --test-root mode; use injected unit-test callbacks.\n");
            return 1;
        }
        if (wcscmp(mode, L"bootnext") == 0 &&
            !ib_oneshot_validate_staged_profile(&request, profile,
                                                error, _countof(error))) {
            fwprintf(stderr, L"BootNext preflight failed: %ls\n", error);
            return 1;
        }
        if (request.volume_path == NULL || !partition_from_volume(request.volume_path, &partition) ||
            !ib_bootnext_enable_privilege(error, _countof(error))) {
            fwprintf(stderr, L"one-shot failed: GPT metadata or firmware privilege unavailable: %ls\n", error);
            return 1;
        }
        ib_bootnext_default_ops(&ops); boot_request.partition = partition; boot_request.ops = &ops;
        if (wcscmp(mode, L"bootnext") == 0) {
            if (!ib_bootnext_set_profile(&boot_request, boot_profile, &number,
                                         error, _countof(error))) {
                fwprintf(stderr, L"BootNext failed: %ls\n", error); return 1;
            }
            wprintf(L"BootNext set to owned Boot%04X; BootOrder was not changed.\n", number);
        } else {
            if (!ib_bootnext_clear_owned_profile(&boot_request, boot_profile,
                                                 error, _countof(error))) {
                fwprintf(stderr, L"BootNext cleanup failed: %ls\n", error); return 1;
            }
            wprintf(L"Owned BootNext/Boot#### state cleared; BootOrder was not changed.\n");
        }
        return 0;
    }
    usage(); return 2;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }
    if (wcscmp(argv[1], L"sign") == 0) {
        return run_sign(argc, argv);
    }
    if (wcscmp(argv[1], L"stage") == 0) {
        return run_stage(argc, argv);
    }
    if (wcscmp(argv[1], L"oneshot") == 0) {
        return run_oneshot(argc, argv);
    }
    if (wcscmp(argv[1], L"enroll-mok") == 0) {
        fwprintf(stderr, L"Refusing direct MokList or MokListRT writes. Enroll the "
                         L"public MOK certificate through shim MokManager on first boot.\n");
        return 1;
    }
    usage();
    return 2;
}
