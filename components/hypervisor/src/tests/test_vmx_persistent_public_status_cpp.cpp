#include "vmx_persistent_public_status.h"
static_assert(sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)==48,"C++ header ABI");
static_assert(sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD)==72,"C++ record ABI");
int VmxPersistentPublicStatusCppAbi(void){return 0;}
