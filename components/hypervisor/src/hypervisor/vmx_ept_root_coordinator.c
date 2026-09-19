#include "vmx_ept_root_coordinator.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int Zero(const void* object, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)object;
    size_t i;
    for (i = 0; i < size; ++i)
        if (bytes[i] != 0)
            return 0;
    return 1;
}

static int Span(const void* object, size_t size, uintptr_t* begin, uintptr_t* end)
{
    uintptr_t first = (uintptr_t)object;
    if (object == 0 || size == 0 || first > UINTPTR_MAX - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t first_begin;
    uintptr_t first_end;
    uintptr_t second_begin;
    uintptr_t second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int SeparateDomainStorage(const void* object, size_t size,
    const VMX_EPT_DOMAIN* domain)
{
    size_t bound_bytes;
    size_t index_bytes;
    if (domain->Binding.VirtualBytes > SIZE_MAX ||
        domain->Binding.PageCount > SIZE_MAX / sizeof(VMX_EPT_BOUND_PAGE) ||
        domain->Binding.PageCount > SIZE_MAX / sizeof(VMX_EPT_PHYSICAL_PAGE))
        return 0;
    bound_bytes = (size_t)domain->Binding.PageCount *
        sizeof(VMX_EPT_BOUND_PAGE);
    index_bytes = (size_t)domain->Binding.PageCount *
        sizeof(VMX_EPT_PHYSICAL_PAGE);
    return Separate(object, size, domain->Binding.VirtualBase,
            (size_t)domain->Binding.VirtualBytes) &&
        Separate(object, size, domain->Binding.PagesByOrdinal, bound_bytes) &&
        Separate(object, size, domain->Binding.PhysicalIndex, index_bytes);
}

static int InitializeSpansSeparate(
    const VMX_EPT_DOMAIN* domain,
    const VMX_PROCESSOR_IDENTITY* identities,
    size_t identity_bytes,
    const VMX_EPT_ROOT_RECORD* records,
    size_t record_bytes,
    const VMX_EPT_ROOT_COORDINATOR* coordinator)
{
    return Separate(identities, identity_bytes, records, record_bytes) &&
        Separate(identities, identity_bytes, coordinator,
            sizeof(*coordinator)) &&
        Separate(records, record_bytes, coordinator,
            sizeof(*coordinator)) &&
        Separate(identities, identity_bytes, domain, sizeof(*domain)) &&
        Separate(records, record_bytes, domain, sizeof(*domain)) &&
        Separate(coordinator, sizeof(*coordinator), domain, sizeof(*domain));
}

static int IdentityValid(const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t expected_index)
{
    return identity->Reserved == 0 &&
        identity->Number < VMX_PROCESSOR_GROUP_MAX_NUMBER &&
        identity->SystemIndex == expected_index;
}

static VMX_EPT_ROOT_STATUS MapDomainStatus(VMX_EPT_DOMAIN_STATUS status)
{
    switch (status) {
    case VMX_EPT_DOMAIN_OK:
        return VMX_EPT_ROOT_OK;
    case VMX_EPT_DOMAIN_NOT_READY:
        return VMX_EPT_ROOT_NOT_READY;
    case VMX_EPT_DOMAIN_BUSY:
        return VMX_EPT_ROOT_BUSY;
    case VMX_EPT_DOMAIN_DIRTY:
        return VMX_EPT_ROOT_DIRTY;
    default:
        return VMX_EPT_ROOT_INVALID;
    }
}

static int CoordinatorAuthentic(const VMX_EPT_ROOT_COORDINATOR* coordinator)
{
    size_t bytes;
    uint32_t owned = 0;
    uint32_t i;
    uint32_t j;
    if (coordinator == 0 ||
        coordinator->Magic != VMX_EPT_ROOT_COORDINATOR_MAGIC ||
        coordinator->Version != VMX_EPT_ROOT_COORDINATOR_VERSION ||
        coordinator->Domain == 0 || coordinator->Records == 0 ||
        coordinator->Count == 0 ||
        coordinator->Count > SIZE_MAX / sizeof(VMX_EPT_ROOT_RECORD) ||
        coordinator->OwnedCount > coordinator->Count ||
        coordinator->Reserved[0] != 0 || coordinator->Reserved[1] != 0 ||
        VmxEptDomainValidate(coordinator->Domain) != VMX_EPT_DOMAIN_OK)
        return 0;
    bytes = (size_t)coordinator->Count * sizeof(VMX_EPT_ROOT_RECORD);
    if (!Separate(coordinator, sizeof(*coordinator),
            coordinator->Domain, sizeof(*coordinator->Domain)) ||
        !Separate(coordinator, sizeof(*coordinator),
            coordinator->Records, bytes) ||
        !Separate(coordinator->Records, bytes,
            coordinator->Domain, sizeof(*coordinator->Domain)) ||
        !SeparateDomainStorage(coordinator, sizeof(*coordinator),
            coordinator->Domain) ||
        !SeparateDomainStorage(coordinator->Records, bytes,
            coordinator->Domain))
        return 0;
    for (i = 0; i < coordinator->Count; ++i) {
        const VMX_EPT_ROOT_RECORD* record = &coordinator->Records[i];
        if (!IdentityValid(&record->Identity, i) ||
            record->RootOwned > 1 || record->Reserved32 != 0 ||
            record->Reserved != 0)
            return 0;
        owned += record->RootOwned;
        for (j = 0; j < i; ++j)
            if (record->Identity.Group ==
                    coordinator->Records[j].Identity.Group &&
                record->Identity.Number ==
                    coordinator->Records[j].Identity.Number)
                return 0;
    }
    if (owned != coordinator->OwnedCount ||
        (owned == 0 &&
         coordinator->Domain->Stage != VMX_EPT_DOMAIN_STAGE_READY) ||
        (owned != 0 &&
         coordinator->Domain->Stage != VMX_EPT_DOMAIN_STAGE_ROOT_OWNED))
        return 0;
    if (coordinator->PendingTransition == VMX_EPT_ROOT_TRANSITION_NONE)
        return coordinator->PendingIndex == VMX_EPT_ROOT_NO_INDEX;
    if (coordinator->PendingIndex >= coordinator->Count)
        return 0;
    if (coordinator->PendingTransition == VMX_EPT_ROOT_TRANSITION_ENTER)
        return coordinator->Records[coordinator->PendingIndex].RootOwned == 0;
    if (coordinator->PendingTransition == VMX_EPT_ROOT_TRANSITION_RELEASE)
        return coordinator->Records[coordinator->PendingIndex].RootOwned == 1;
    return 0;
}

static void ClearPending(VMX_EPT_ROOT_COORDINATOR* coordinator)
{
    coordinator->PendingIndex = VMX_EPT_ROOT_NO_INDEX;
    coordinator->PendingTransition = VMX_EPT_ROOT_TRANSITION_NONE;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorValidate(
    const VMX_EPT_ROOT_COORDINATOR* coordinator)
{
    return CoordinatorAuthentic(coordinator) ?
        VMX_EPT_ROOT_OK : VMX_EPT_ROOT_INVALID;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorInitialize(
    VMX_EPT_DOMAIN* domain,
    const VMX_PROCESSOR_IDENTITY* identities,
    uint32_t count,
    VMX_EPT_ROOT_RECORD* records,
    VMX_EPT_ROOT_COORDINATOR* coordinator)
{
    VMX_EPT_ROOT_COORDINATOR staged;
    size_t identity_bytes;
    size_t record_bytes;
    uint32_t i;
    uint32_t j;
    if (domain == 0 || identities == 0 || records == 0 || coordinator == 0 ||
        count == 0 || count > SIZE_MAX / sizeof(*identities) ||
        count > SIZE_MAX / sizeof(*records))
        return VMX_EPT_ROOT_INVALID;
    identity_bytes = (size_t)count * sizeof(*identities);
    record_bytes = (size_t)count * sizeof(*records);
    if (!InitializeSpansSeparate(domain, identities, identity_bytes, records,
            record_bytes, coordinator) ||
        !Zero(records, record_bytes) ||
        !Zero(coordinator, sizeof(*coordinator)) ||
        VmxEptDomainValidate(domain) != VMX_EPT_DOMAIN_OK ||
        domain->Stage != VMX_EPT_DOMAIN_STAGE_READY ||
        domain->PendingGeneration != 0 ||
        domain->MutationGeneration != domain->InvalidatedGeneration)
        return VMX_EPT_ROOT_INVALID;
    if (!SeparateDomainStorage(identities, identity_bytes, domain) ||
        !SeparateDomainStorage(records, record_bytes, domain) ||
        !SeparateDomainStorage(coordinator, sizeof(*coordinator), domain))
        return VMX_EPT_ROOT_INVALID;
    for (i = 0; i < count; ++i) {
        VMX_PROCESSOR_IDENTITY identity_snapshot = identities[i];
        if (!IdentityValid(&identity_snapshot, i))
            goto rollback;
        for (j = 0; j < i; ++j)
            if (identity_snapshot.Group == records[j].Identity.Group &&
                identity_snapshot.Number == records[j].Identity.Number)
                goto rollback;
        records[i].Identity = identity_snapshot;
    }
    memset(&staged, 0, sizeof(staged));
    staged.Domain = domain;
    staged.Records = records;
    staged.Count = count;
    staged.PendingIndex = VMX_EPT_ROOT_NO_INDEX;
    staged.Magic = VMX_EPT_ROOT_COORDINATOR_MAGIC;
    staged.Version = VMX_EPT_ROOT_COORDINATOR_VERSION;
    if (!CoordinatorAuthentic(&staged))
        goto rollback;
    *coordinator = staged;
    return VMX_EPT_ROOT_OK;

rollback:
    memset(records, 0, record_bytes);
    return VMX_EPT_ROOT_INVALID;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorPrepareEnter(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index)
{
    VMX_EPT_DOMAIN_STATUS status;
    if (!CoordinatorAuthentic(coordinator) ||
        system_index >= coordinator->Count)
        return VMX_EPT_ROOT_INVALID;
    if (coordinator->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE ||
        coordinator->Records[system_index].RootOwned != 0)
        return VMX_EPT_ROOT_BUSY;
    if (coordinator->OwnedCount == 0) {
        status = VmxEptDomainPrepareRootEnter(coordinator->Domain);
        if (status != VMX_EPT_DOMAIN_OK)
            return MapDomainStatus(status);
    }
    coordinator->PendingIndex = system_index;
    coordinator->PendingTransition = VMX_EPT_ROOT_TRANSITION_ENTER;
    return VMX_EPT_ROOT_OK;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorCompleteEnter(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index)
{
    VMX_EPT_DOMAIN_STATUS status;
    if (!CoordinatorAuthentic(coordinator) ||
        coordinator->PendingTransition != VMX_EPT_ROOT_TRANSITION_ENTER ||
        coordinator->PendingIndex != system_index)
        return VMX_EPT_ROOT_INVALID;
    if (coordinator->OwnedCount == 0) {
        status = VmxEptDomainAdoptRoot(coordinator->Domain);
        if (status != VMX_EPT_DOMAIN_OK)
            return MapDomainStatus(status);
    }
    coordinator->Records[system_index].RootOwned = 1;
    coordinator->OwnedCount++;
    ClearPending(coordinator);
    return VMX_EPT_ROOT_OK;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorPrepareRelease(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index)
{
    VMX_EPT_DOMAIN_STATUS status;
    if (!CoordinatorAuthentic(coordinator) ||
        system_index >= coordinator->Count)
        return VMX_EPT_ROOT_INVALID;
    if (coordinator->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE ||
        coordinator->Records[system_index].RootOwned != 1)
        return VMX_EPT_ROOT_BUSY;
    if (coordinator->OwnedCount == 1) {
        status = VmxEptDomainPrepareRootRelease(coordinator->Domain);
        if (status != VMX_EPT_DOMAIN_OK)
            return MapDomainStatus(status);
    }
    coordinator->PendingIndex = system_index;
    coordinator->PendingTransition = VMX_EPT_ROOT_TRANSITION_RELEASE;
    return VMX_EPT_ROOT_OK;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorCompleteRelease(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index)
{
    VMX_EPT_DOMAIN_STATUS status;
    if (!CoordinatorAuthentic(coordinator) ||
        coordinator->PendingTransition != VMX_EPT_ROOT_TRANSITION_RELEASE ||
        coordinator->PendingIndex != system_index)
        return VMX_EPT_ROOT_INVALID;
    if (coordinator->OwnedCount == 1) {
        status = VmxEptDomainCompleteRootRelease(coordinator->Domain);
        if (status != VMX_EPT_DOMAIN_OK)
            return MapDomainStatus(status);
    }
    coordinator->Records[system_index].RootOwned = 0;
    coordinator->OwnedCount--;
    ClearPending(coordinator);
    return VMX_EPT_ROOT_OK;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorCancel(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index)
{
    if (!CoordinatorAuthentic(coordinator) ||
        coordinator->PendingTransition == VMX_EPT_ROOT_TRANSITION_NONE ||
        coordinator->PendingIndex != system_index)
        return VMX_EPT_ROOT_INVALID;
    ClearPending(coordinator);
    return VMX_EPT_ROOT_OK;
}

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorDestroy(
    VMX_EPT_ROOT_COORDINATOR* coordinator)
{
    size_t bytes;
    if (coordinator == 0)
        return VMX_EPT_ROOT_INVALID;
    if (Zero(coordinator, sizeof(*coordinator)))
        return VMX_EPT_ROOT_OK;
    if (!CoordinatorAuthentic(coordinator))
        return VMX_EPT_ROOT_INVALID;
    if (coordinator->OwnedCount != 0 ||
        coordinator->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE)
        return VMX_EPT_ROOT_BUSY;
    bytes = (size_t)coordinator->Count * sizeof(*coordinator->Records);
    memset(coordinator->Records, 0, bytes);
    memset(coordinator, 0, sizeof(*coordinator));
    return VMX_EPT_ROOT_OK;
}
