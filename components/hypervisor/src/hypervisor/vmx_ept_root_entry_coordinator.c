#include "vmx_ept_root_entry_coordinator.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int Span(const void* value, size_t size,
    uintptr_t* begin, uintptr_t* end)
{
    uintptr_t first = (uintptr_t)value;
    if (value == NULL || size == 0 || first > UINTPTR_MAX - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t first_begin, first_end, second_begin, second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int Preflight(const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_ROOT_ENTRY_PLATFORM* platform,
    VMX_EPT_ROOT_ENTRY_REPORT* report,
    VMX_PROCESSOR_IDENTITY* identity_snapshot,
    VMX_EPT_ROOT_ENTRY_PLATFORM* platform_snapshot)
{
    size_t context_bytes;
    if (identity == NULL || platform == NULL || report == NULL ||
        identity_snapshot == NULL || platform_snapshot == NULL ||
        !Separate(identity, sizeof(*identity), platform, sizeof(*platform)) ||
        !Separate(identity, sizeof(*identity), report, sizeof(*report)) ||
        !Separate(platform, sizeof(*platform), report, sizeof(*report)))
        return 0;
    *identity_snapshot = *identity;
    *platform_snapshot = *platform;
    if (identity_snapshot->Reserved != 0 ||
        identity_snapshot->Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        platform_snapshot->Context == NULL ||
        platform_snapshot->ContextBytes == 0 ||
        platform_snapshot->ContextBytes > SIZE_MAX ||
        platform_snapshot->PrepareEnter == NULL ||
        platform_snapshot->NativeEnter == NULL ||
        platform_snapshot->CompleteEnter == NULL ||
        platform_snapshot->NativeRelease == NULL ||
        platform_snapshot->RecoverAfterRelease == NULL ||
        platform_snapshot->Reserved[0] != 0 ||
        platform_snapshot->Reserved[1] != 0)
        return 0;
    context_bytes = (size_t)platform_snapshot->ContextBytes;
    return Separate(identity, sizeof(*identity),
               platform_snapshot->Context, context_bytes) &&
        Separate(platform, sizeof(*platform),
            platform_snapshot->Context, context_bytes) &&
        Separate(report, sizeof(*report),
            platform_snapshot->Context, context_bytes);
}

VMX_EPT_ROOT_ENTRY_STATUS VmxEptCoordinateRootEntry(
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_ROOT_ENTRY_PLATFORM* platform,
    VMX_EPT_ROOT_ENTRY_REPORT* report)
{
    VMX_PROCESSOR_IDENTITY snapshot;
    VMX_EPT_ROOT_ENTRY_PLATFORM callbacks;
    VMX_EPT_ROOT_ENTRY_REPORT staged;

    if (!Preflight(identity, platform, report, &snapshot, &callbacks))
        return VMX_EPT_ROOT_ENTRY_INVALID;
    memset(&staged, 0, sizeof(staged));
    staged.PrepareAttempted = 1;
    staged.PrepareStatus = callbacks.PrepareEnter(
        callbacks.Context, &snapshot);
    if (staged.PrepareStatus != 0) {
        *report = staged;
        return VMX_EPT_ROOT_ENTRY_PREPARE_FAILED;
    }
    staged.TransitionPending = 1;
    staged.NativeEnterAttempted = 1;
    staged.NativeEnterStatus = callbacks.NativeEnter(
        callbacks.Context, &snapshot);
    if (staged.NativeEnterStatus != 0) {
        staged.CompleteAttempted = 1;
        staged.CompleteStatus = callbacks.CompleteEnter(
            callbacks.Context, &snapshot, 0);
        if (staged.CompleteStatus == 0)
            staged.TransitionPending = 0;
        *report = staged;
        return staged.CompleteStatus == 0 ?
            VMX_EPT_ROOT_ENTRY_NATIVE_ENTER_FAILED :
            VMX_EPT_ROOT_ENTRY_CANCEL_FAILED;
    }
    staged.RootActive = 1;
    staged.CompleteAttempted = 1;
    staged.CompleteStatus = callbacks.CompleteEnter(
        callbacks.Context, &snapshot, 1);
    if (staged.CompleteStatus == 0) {
        staged.TransitionPending = 0;
        *report = staged;
        return VMX_EPT_ROOT_ENTRY_OK;
    }
    staged.NativeReleaseAttempted = 1;
    staged.NativeReleaseStatus = callbacks.NativeRelease(
        callbacks.Context, &snapshot);
    if (staged.NativeReleaseStatus != 0) {
        *report = staged;
        return VMX_EPT_ROOT_ENTRY_NATIVE_RELEASE_FAILED;
    }
    staged.RootActive = 0;
    staged.RecoveryAttempted = 1;
    staged.RecoveryStatus = callbacks.RecoverAfterRelease(
        callbacks.Context, &snapshot);
    if (staged.RecoveryStatus != 0) {
        *report = staged;
        return VMX_EPT_ROOT_ENTRY_RECOVERY_FAILED;
    }
    staged.TransitionPending = 0;
    *report = staged;
    return VMX_EPT_ROOT_ENTRY_COMPLETE_FAILED;
}
