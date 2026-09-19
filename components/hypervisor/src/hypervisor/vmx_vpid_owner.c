#include "vmx_vpid_owner.h"

#include <string.h>

typedef struct VMX_VPID_SPAN { uintptr_t Begin, End; } VMX_VPID_SPAN;

static int Zero(const void* Value, size_t Size)
{
    const unsigned char* Bytes = (const unsigned char*)Value;
    size_t Index;
    if (Value == NULL) return 0;
    for (Index = 0; Index < Size; ++Index)
        if (Bytes[Index] != 0) return 0;
    return 1;
}

static int Span(const void* Value, size_t Size, VMX_VPID_SPAN* Out)
{
    const uintptr_t Address = (uintptr_t)Value;
    if (Value == NULL || Out == NULL || Size == 0 ||
        Address > UINTPTR_MAX - Size) return 0;
    Out->Begin = Address; Out->End = Address + Size; return 1;
}

static int Separate(const void* A, size_t ASize, const void* B, size_t BSize)
{
    VMX_VPID_SPAN X, Y;
    return Span(A, ASize, &X) && Span(B, BSize, &Y) &&
        (X.End <= Y.Begin || Y.End <= X.Begin);
}

static int IdentityValid(const VMX_PROCESSOR_IDENTITY* Identity)
{
    return Identity != NULL && Identity->Reserved == 0 &&
        Identity->Number < VMX_PROCESSOR_GROUP_MAX_NUMBER;
}

static int IdentityEqual(const VMX_PROCESSOR_IDENTITY* Left,
    const VMX_PROCESSOR_IDENTITY* Right)
{
    return IdentityValid(Left) && IdentityValid(Right) &&
        Left->Group == Right->Group && Left->Number == Right->Number &&
        Left->SystemIndex == Right->SystemIndex;
}

static int CoreValid(const VMX_VPID_OWNER* Owner)
{
    size_t Bytes;
    return Owner != NULL && Owner->Magic == VMX_VPID_OWNER_MAGIC &&
        Owner->Version == VMX_VPID_OWNER_VERSION && Owner->Records != NULL &&
        Owner->Records == Owner->RecordsContract && Owner->Count != 0 &&
        Owner->Count <= (size_t)VMX_VPID_MAX &&
        Owner->Count == Owner->CountContract &&
        Owner->Count <= SIZE_MAX / sizeof(*Owner->Records) &&
        (Bytes = Owner->Count * sizeof(*Owner->Records)) != 0 &&
        Owner->RecordsBytes == Bytes && Owner->RecordsBytesContract == ~Bytes &&
        Separate(Owner, sizeof(*Owner), Owner->Records, Bytes);
}

static VMX_VPID_RECORD* Find(VMX_VPID_OWNER* Owner,
    const VMX_PROCESSOR_IDENTITY* Identity)
{
    size_t Index;
    if (!CoreValid(Owner) || !IdentityValid(Identity)) return NULL;
    for (Index = 0; Index < Owner->Count; ++Index)
        if (IdentityEqual(&Owner->Records[Index].Identity, Identity))
            return &Owner->Records[Index];
    return NULL;
}

VMX_VPID_OWNER_STATUS VmxVpidOwnerValidate(const VMX_VPID_OWNER* Owner)
{
    size_t Index;
    if (!CoreValid(Owner)) return VMX_VPID_OWNER_INVALID;
    for (Index = 0; Index < Owner->Count; ++Index) {
        const VMX_VPID_RECORD* Record = &Owner->Records[Index];
        size_t Previous;
        if (!IdentityValid(&Record->Identity) ||
            Record->Identity.SystemIndex != (uint32_t)Index ||
            Record->Vpid != (uint16_t)(Index + 1) ||
            Record->State > VMX_VPID_RECORD_RETIRE_PENDING ||
            Record->Reserved != 0) return VMX_VPID_OWNER_INVALID;
        for (Previous = 0; Previous < Index; ++Previous) {
            const VMX_VPID_RECORD* Other = &Owner->Records[Previous];
            if (Other->Vpid == Record->Vpid ||
                Other->Identity.SystemIndex == Record->Identity.SystemIndex ||
                (Other->Identity.Group == Record->Identity.Group &&
                 Other->Identity.Number == Record->Identity.Number))
                return VMX_VPID_OWNER_INVALID;
        }
    }
    return VMX_VPID_OWNER_OK;
}

VMX_VPID_OWNER_STATUS VmxVpidOwnerInitialize(
    const VMX_PROCESSOR_IDENTITY* Identities, size_t Count,
    VMX_VPID_RECORD* Records, VMX_VPID_OWNER* Owner)
{
    VMX_VPID_OWNER Staged = { 0 };
    size_t Bytes, Index;
    if (Identities == NULL || Records == NULL || Owner == NULL || Count == 0 ||
        Count > (size_t)VMX_VPID_MAX || Count > SIZE_MAX / sizeof(*Records) ||
        Count > SIZE_MAX / sizeof(*Identities) || !Zero(Owner, sizeof(*Owner)))
        return VMX_VPID_OWNER_INVALID;
    Bytes = Count * sizeof(*Records);
    if (!Zero(Records, Bytes) ||
        !Separate(Identities, Count * sizeof(*Identities), Records, Bytes) ||
        !Separate(Identities, Count * sizeof(*Identities), Owner, sizeof(*Owner)) ||
        !Separate(Records, Bytes, Owner, sizeof(*Owner)))
        return VMX_VPID_OWNER_INVALID;
    for (Index = 0; Index < Count; ++Index) {
        size_t Previous;
        if (!IdentityValid(&Identities[Index]) ||
            Identities[Index].SystemIndex != (uint32_t)Index)
            return VMX_VPID_OWNER_INVALID;
        for (Previous = 0; Previous < Index; ++Previous)
            if (Identities[Previous].SystemIndex == Identities[Index].SystemIndex ||
                (Identities[Previous].Group == Identities[Index].Group &&
                 Identities[Previous].Number == Identities[Index].Number))
                return VMX_VPID_OWNER_INVALID;
    }
    for (Index = 0; Index < Count; ++Index) {
        Records[Index].Identity = Identities[Index];
        Records[Index].Vpid = (uint16_t)(Index + 1);
        Records[Index].State = VMX_VPID_RECORD_FREE;
    }
    Staged.Magic = VMX_VPID_OWNER_MAGIC; Staged.Version = VMX_VPID_OWNER_VERSION;
    Staged.Records = Records; Staged.RecordsContract = Records;
    Staged.Count = Count; Staged.CountContract = Count;
    Staged.RecordsBytes = Bytes; Staged.RecordsBytesContract = ~Bytes;
    *Owner = Staged;
    if (VmxVpidOwnerValidate(Owner) != VMX_VPID_OWNER_OK) {
        (void)memset(Records, 0, Bytes); (void)memset(Owner, 0, sizeof(*Owner));
        return VMX_VPID_OWNER_INVALID;
    }
    return VMX_VPID_OWNER_OK;
}

VMX_VPID_OWNER_STATUS VmxVpidOwnerAcquire(VMX_VPID_OWNER* Owner,
    const VMX_PROCESSOR_IDENTITY* Identity, uint16_t* OutVpid)
{
    VMX_VPID_RECORD* Record;
    if (!CoreValid(Owner) || OutVpid == NULL ||
        !Separate(OutVpid, sizeof(*OutVpid), Owner, sizeof(*Owner)) ||
        !Separate(OutVpid, sizeof(*OutVpid), Owner->Records, Owner->RecordsBytes))
        return VMX_VPID_OWNER_INVALID;
    *OutVpid = 0;
    if (VmxVpidOwnerValidate(Owner) != VMX_VPID_OWNER_OK)
        return VMX_VPID_OWNER_INVALID;
    Record = Find(Owner, Identity);
    if (Record == NULL) return VMX_VPID_OWNER_INVALID;
    if (Record->State == VMX_VPID_RECORD_OWNED)
        return VMX_VPID_OWNER_ALREADY_OWNED;
    if (Record->State == VMX_VPID_RECORD_RETIRE_PENDING)
        return VMX_VPID_OWNER_RETIRE_PENDING;
    Record->State = VMX_VPID_RECORD_OWNED;
    *OutVpid = Record->Vpid;
    return VMX_VPID_OWNER_OK;
}

VMX_VPID_OWNER_STATUS VmxVpidOwnerRetire(VMX_VPID_OWNER* Owner,
    const VMX_PROCESSOR_IDENTITY* Identity, uint16_t Vpid)
{
    VMX_VPID_RECORD* Record;
    if (VmxVpidOwnerValidate(Owner) != VMX_VPID_OWNER_OK || Vpid == 0)
        return VMX_VPID_OWNER_INVALID;
    Record = Find(Owner, Identity);
    if (Record == NULL || Record->Vpid != Vpid) return VMX_VPID_OWNER_INVALID;
    if (Record->State == VMX_VPID_RECORD_FREE) return VMX_VPID_OWNER_NOT_OWNED;
    if (Record->State == VMX_VPID_RECORD_RETIRE_PENDING)
        return VMX_VPID_OWNER_RETIRE_PENDING;
    Record->State = VMX_VPID_RECORD_RETIRE_PENDING;
    return VMX_VPID_OWNER_OK;
}

VMX_VPID_OWNER_STATUS VmxVpidOwnerAcknowledgeInvalidation(
    VMX_VPID_OWNER* Owner, const VMX_PROCESSOR_IDENTITY* Identity, uint16_t Vpid)
{
    VMX_VPID_RECORD* Record;
    if (VmxVpidOwnerValidate(Owner) != VMX_VPID_OWNER_OK || Vpid == 0)
        return VMX_VPID_OWNER_INVALID;
    Record = Find(Owner, Identity);
    if (Record == NULL || Record->Vpid != Vpid) return VMX_VPID_OWNER_INVALID;
    if (Record->State != VMX_VPID_RECORD_RETIRE_PENDING)
        return Record->State == VMX_VPID_RECORD_OWNED ?
            VMX_VPID_OWNER_BUSY : VMX_VPID_OWNER_NOT_OWNED;
    Record->State = VMX_VPID_RECORD_FREE;
    return VMX_VPID_OWNER_OK;
}

VMX_VPID_OWNER_STATUS VmxVpidOwnerDestroy(VMX_VPID_OWNER* Owner)
{
    size_t Index, Bytes;
    VMX_VPID_RECORD* Records;
    if (VmxVpidOwnerValidate(Owner) != VMX_VPID_OWNER_OK)
        return VMX_VPID_OWNER_INVALID;
    for (Index = 0; Index < Owner->Count; ++Index)
        if (Owner->Records[Index].State != VMX_VPID_RECORD_FREE)
            return VMX_VPID_OWNER_BUSY;
    Records = Owner->Records; Bytes = Owner->RecordsBytes;
    (void)memset(Records, 0, Bytes); (void)memset(Owner, 0, sizeof(*Owner));
    return VMX_VPID_OWNER_OK;
}
