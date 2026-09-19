#include "vmx_persistent_serializer.h"

#define VMX_SERIALIZER_UNINITIALIZED ((LONG)0)
#define VMX_SERIALIZER_INITIALIZING ((LONG)1)
#define VMX_SERIALIZER_READY ((LONG)2)
#define VMX_SERIALIZER_SHUTDOWN ((LONG)3)

static LONG VmxPersistentSerializerState(
    VMX_PERSISTENT_SERIALIZER* serializer)
{
    return InterlockedCompareExchange(
        &serializer->Initialized, VMX_SERIALIZER_READY, VMX_SERIALIZER_READY);
}

static PVOID VmxPersistentSerializerOwner(
    VMX_PERSISTENT_SERIALIZER* serializer)
{
    return InterlockedCompareExchangePointer(&serializer->Owner, NULL, NULL);
}

static bool VmxPersistentSerializerEnter(void* context)
{
    VMX_PERSISTENT_SERIALIZER* serializer = context;
    PVOID current;

    if (serializer == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return false;
    current = PsGetCurrentThread();
    if (VmxPersistentSerializerState(serializer) != VMX_SERIALIZER_READY ||
        VmxPersistentSerializerOwner(serializer) == current)
        return false;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&serializer->Lock);
    if (VmxPersistentSerializerState(serializer) != VMX_SERIALIZER_READY ||
        InterlockedCompareExchangePointer(
            &serializer->Owner, current, NULL) != NULL) {
        ExReleasePushLockExclusive(&serializer->Lock);
        KeLeaveCriticalRegion();
        return false;
    }
    return true;
}

static void VmxPersistentSerializerLeave(void* context)
{
    VMX_PERSISTENT_SERIALIZER* serializer = context;

    (void)InterlockedExchangePointer(&serializer->Owner, NULL);
    ExReleasePushLockExclusive(&serializer->Lock);
    KeLeaveCriticalRegion();
}

VMX_PERSISTENT_STATUS VmxPersistentSerializerInitialize(
    VMX_PERSISTENT_SERIALIZER* serializer)
{
    if (serializer == NULL)
        return VMX_PERSISTENT_STATUS_INVALID;
    if (InterlockedCompareExchange(&serializer->Initialized,
            VMX_SERIALIZER_INITIALIZING, VMX_SERIALIZER_UNINITIALIZED) !=
        VMX_SERIALIZER_UNINITIALIZED)
        return VMX_PERSISTENT_STATUS_BUSY;

    ExInitializePushLock(&serializer->Lock);
    (void)InterlockedExchangePointer(&serializer->Owner, NULL);
    InterlockedExchange(&serializer->Initialized, VMX_SERIALIZER_READY);
    return VMX_PERSISTENT_STATUS_OK;
}

VMX_PERSISTENT_STATUS VmxPersistentSerializerShutdown(
    VMX_PERSISTENT_SERIALIZER* serializer)
{
    PVOID current;
    LONG state;

    if (serializer == NULL)
        return VMX_PERSISTENT_STATUS_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    current = PsGetCurrentThread();
    if (VmxPersistentSerializerOwner(serializer) == current)
        return VMX_PERSISTENT_STATUS_BUSY;
    state = VmxPersistentSerializerState(serializer);
    if (state == VMX_SERIALIZER_SHUTDOWN)
        return VMX_PERSISTENT_STATUS_OK;
    if (state != VMX_SERIALIZER_READY)
        return VMX_PERSISTENT_STATUS_INVALID;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&serializer->Lock);
    state = VmxPersistentSerializerState(serializer);
    if (state == VMX_SERIALIZER_READY &&
        InterlockedCompareExchangePointer(
            &serializer->Owner, current, NULL) == NULL) {
        InterlockedExchange(&serializer->Initialized, VMX_SERIALIZER_SHUTDOWN);
        (void)InterlockedExchangePointer(&serializer->Owner, NULL);
        ExReleasePushLockExclusive(&serializer->Lock);
        KeLeaveCriticalRegion();
        return VMX_PERSISTENT_STATUS_OK;
    }
    ExReleasePushLockExclusive(&serializer->Lock);
    KeLeaveCriticalRegion();
    return state == VMX_SERIALIZER_SHUTDOWN ?
        VMX_PERSISTENT_STATUS_OK : VMX_PERSISTENT_STATUS_BUSY;
}

VMX_PERSISTENT_COORDINATOR_SERIALIZER VmxPersistentSerializerFactory(
    VMX_PERSISTENT_SERIALIZER* serializer)
{
    VMX_PERSISTENT_COORDINATOR_SERIALIZER result = {0};

    if (serializer == NULL ||
        VmxPersistentSerializerState(serializer) != VMX_SERIALIZER_READY)
        return result;
    result.Context = serializer;
    result.Enter = VmxPersistentSerializerEnter;
    result.Leave = VmxPersistentSerializerLeave;
    return result;
}