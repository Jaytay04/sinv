/* Independent x64 ABI offsets from the UEFI runtime-services contract. */
#include <stddef.h>
#include <stdio.h>
#include "Uefi.h"

#if defined(_M_X64) || defined(__x86_64__)
_Static_assert(sizeof(EFI_TABLE_HEADER) == 24u, "EFI table header size");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, GetVariable) == 72u, "GetVariable ABI slot");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, GetNextVariableName) == 80u, "GetNextVariableName ABI slot");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, SetVariable) == 88u, "SetVariable ABI slot");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, GetNextHighMonotonicCount) == 96u, "MonotonicCount ABI slot");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, ResetSystem) == 104u, "ResetSystem ABI slot");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, UpdateCapsule) == 112u, "UpdateCapsule ABI slot");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, QueryCapsuleCapabilities) == 120u, "QueryCapsule ABI slot");
_Static_assert(offsetof(EFI_RUNTIME_SERVICES, QueryVariableInfo) == 128u, "QueryVariableInfo ABI slot");
_Static_assert(sizeof(EFI_RUNTIME_SERVICES) == 136u, "Runtime table size");
#endif

int main(void)
{
    puts("uefi_runtime_abi_test: PASS");
    return 0;
}
