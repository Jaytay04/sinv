#include "vmx_ept_persistent_device_lifecycle.h"

static int StorageZero(const void* storage, SIZE_T size)
{
    const UCHAR* bytes = (const UCHAR*)storage;
    SIZE_T index;

    if (storage == NULL)
        return 0;
    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0)
            return 0;
    }
    return 1;
}

VMX_EPT_PERSISTENT_LIFECYCLE_STATUS
VmxEptPersistentDeviceLifecycleInitialize(
    VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_LIFECYCLE_WRONG_IRQL;
    if (context == NULL ||
        !StorageZero(&context->EptLifecycle, sizeof(context->EptLifecycle)))
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;

    RtlSecureZeroMemory(&context->EptDestroyReport,
        sizeof(context->EptDestroyReport));
    return VmxEptPersistentLifecycleOwnerInitialize(
        &context->Owner, &context->EptLifecycle,
        &context->EptDestroyReport);
}

VMX_EPT_PERSISTENT_LIFECYCLE_STATUS
VmxEptPersistentDeviceLifecycleDestroy(
    VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_LIFECYCLE_WRONG_IRQL;
    if (context == NULL ||
        VmxEptPersistentLifecycleOwnerValidate(&context->EptLifecycle) !=
            VMX_EPT_PERSISTENT_LIFECYCLE_OK)
        return VMX_EPT_PERSISTENT_LIFECYCLE_INVALID;

    RtlSecureZeroMemory(&context->EptDestroyReport,
        sizeof(context->EptDestroyReport));
    return VmxEptPersistentLifecycleOwnerDestroy(
        &context->EptLifecycle, &context->EptDestroyReport);
}
