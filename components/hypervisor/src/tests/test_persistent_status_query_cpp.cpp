#include "driver_interface.h"
static_assert(sizeof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST)==16,"C++ query ABI");
static_assert(offsetof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST,Flags)==8,"C++ query flags");
extern "C" int PersistentStatusQueryCppAbi(void) {
    HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST q{};
    return q.Version==0 && q.Size==0 && q.Flags==0 && q.Reserved==0 ? 0 : 1;
}