#include "driver_interface.h"
#include "vmx_diagnostic.h"
#include <string.h>
extern int PersistentStatusQueryCppAbi(void);
#define ABI_ASSERT(x) _Static_assert((x), #x)
ABI_ASSERT((uint32_t)IOCTL_HYPERVISOR_LOAD==UINT32_C(0x80002000));
ABI_ASSERT((uint32_t)IOCTL_HYPERVISOR_UNLOAD==UINT32_C(0x80002004));
ABI_ASSERT((uint32_t)IOCTL_HYPERVISOR_HOOK==UINT32_C(0x80002008));
ABI_ASSERT((uint32_t)IOCTL_HYPERVISOR_UNHOOK==UINT32_C(0x8000200C));
ABI_ASSERT((uint32_t)IOCTL_HYPERVISOR_STATUS==UINT32_C(0x80002010));
ABI_ASSERT((uint32_t)IOCTL_HYPERVISOR_PROBE==UINT32_C(0x80002014));
ABI_ASSERT((uint32_t)IOCTL_HYPERVISOR_LAUNCH_PROBE==UINT32_C(0x80002018));
ABI_ASSERT(ERR_PERSISTENT_CONFIG_FAILED==UINT32_C(0x80000040));
ABI_ASSERT(ERR_PERSISTENT_ATTACH_FAILED==UINT32_C(0x80000041));
ABI_ASSERT(ERR_PERSISTENT_START_FAILED==UINT32_C(0x80000042));
ABI_ASSERT(ERR_PERSISTENT_ROLLBACK_FAILED==UINT32_C(0x80000043));
ABI_ASSERT(ERR_PERSISTENT_PREPARE_FAILED==UINT32_C(0x80000044));
ABI_ASSERT(ERR_PERSISTENT_KERNEL_FAILED==UINT32_C(0x80000045));
ABI_ASSERT(ERR_PERSISTENT_VMXON_FAILED==UINT32_C(0x80000046));
ABI_ASSERT(ERR_PERSISTENT_VMCLEAR_FAILED==UINT32_C(0x80000047));
ABI_ASSERT(ERR_PERSISTENT_VMPTRLD_FAILED==UINT32_C(0x80000048));
ABI_ASSERT(ERR_PERSISTENT_STATE_FAILED==UINT32_C(0x80000049));
ABI_ASSERT(ERR_PERSISTENT_PLAN_FAILED==UINT32_C(0x8000004A));
ABI_ASSERT(ERR_PERSISTENT_LAUNCH_FAILED==UINT32_C(0x8000004B));
ABI_ASSERT(ERR_PERSISTENT_CLEANUP_FAILED==UINT32_C(0x8000004C));
ABI_ASSERT(ERR_PERSISTENT_LIFECYCLE_FAILED==UINT32_C(0x8000004D));
ABI_ASSERT(sizeof(hypervisor_load_request_t)==4);
ABI_ASSERT(sizeof(hypervisor_unload_request_t)==4);
ABI_ASSERT(sizeof(hypervisor_hook_request_t)==16);
ABI_ASSERT(offsetof(hypervisor_hook_request_t,gpa)==0);
ABI_ASSERT(offsetof(hypervisor_hook_request_t,permissions)==8);
ABI_ASSERT(offsetof(hypervisor_hook_request_t,reserved)==9);
ABI_ASSERT(sizeof(hypervisor_unhook_request_t)==16);
ABI_ASSERT(offsetof(hypervisor_unhook_request_t,gpa)==0);
ABI_ASSERT(offsetof(hypervisor_unhook_request_t,reserved)==8);
ABI_ASSERT(sizeof(hypervisor_status_response_t)==16);
ABI_ASSERT(offsetof(hypervisor_status_response_t,driver_state)==0);
ABI_ASSERT(offsetof(hypervisor_status_response_t,active_cores)==4);
ABI_ASSERT(offsetof(hypervisor_status_response_t,hook_count)==8);
ABI_ASSERT(offsetof(hypervisor_status_response_t,error_code)==12);
ABI_ASSERT(sizeof(hypervisor_ioctl_response_t)==16);
ABI_ASSERT(offsetof(hypervisor_ioctl_response_t,status)==0);
ABI_ASSERT(offsetof(hypervisor_ioctl_response_t,active_cores)==4);
ABI_ASSERT(offsetof(hypervisor_ioctl_response_t,reserved)==8);
ABI_ASSERT(sizeof(hypervisor_probe_response_t)==88);
ABI_ASSERT(offsetof(hypervisor_probe_response_t,status)==0);
ABI_ASSERT(offsetof(hypervisor_probe_response_t,nt_status)==4);
ABI_ASSERT(offsetof(hypervisor_probe_response_t,runtime_stage)==8);
ABI_ASSERT(offsetof(hypervisor_probe_response_t,feature_control)==32);
ABI_ASSERT(offsetof(hypervisor_probe_response_t,adjusted_cr4)==80);
ABI_ASSERT(sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2)==280);
ABI_ASSERT(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,ShutdownExit)==232);
#undef ABI_ASSERT
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int main(void) {
    HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST q;
    HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST bad;
    (void)memset(&q,0,sizeof(q));
    q.Version=HYPERVISOR_PERSISTENT_STATUS_QUERY_VERSION;
    q.Size=HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST_SIZE;
    q.Flags=HYPERVISOR_PERSISTENT_STATUS_QUERY_FLAGS_NONE;
    CHECK(sizeof(q)==16 && offsetof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST,Reserved)==12);
    CHECK((uint32_t)IOCTL_HYPERVISOR_LOAD==UINT32_C(0x80002000));
    CHECK((uint32_t)IOCTL_HYPERVISOR_UNLOAD==UINT32_C(0x80002004));
    CHECK((uint32_t)IOCTL_HYPERVISOR_HOOK==UINT32_C(0x80002008));
    CHECK((uint32_t)IOCTL_HYPERVISOR_UNHOOK==UINT32_C(0x8000200C));
    CHECK((uint32_t)IOCTL_HYPERVISOR_STATUS==UINT32_C(0x80002010));
    CHECK((uint32_t)IOCTL_HYPERVISOR_PROBE==UINT32_C(0x80002014));
    CHECK((uint32_t)IOCTL_HYPERVISOR_LAUNCH_PROBE==UINT32_C(0x80002018));
    CHECK(ERR_PERSISTENT_CONFIG_FAILED==UINT32_C(0x80000040));
    CHECK(ERR_PERSISTENT_ATTACH_FAILED==UINT32_C(0x80000041));
    CHECK(ERR_PERSISTENT_START_FAILED==UINT32_C(0x80000042));
    CHECK(ERR_PERSISTENT_ROLLBACK_FAILED==UINT32_C(0x80000043));
    CHECK(ERR_PERSISTENT_PREPARE_FAILED==UINT32_C(0x80000044));
    CHECK(ERR_PERSISTENT_KERNEL_FAILED==UINT32_C(0x80000045));
    CHECK(ERR_PERSISTENT_VMXON_FAILED==UINT32_C(0x80000046));
    CHECK(ERR_PERSISTENT_VMCLEAR_FAILED==UINT32_C(0x80000047));
    CHECK(ERR_PERSISTENT_VMPTRLD_FAILED==UINT32_C(0x80000048));
    CHECK(ERR_PERSISTENT_STATE_FAILED==UINT32_C(0x80000049));
    CHECK(ERR_PERSISTENT_PLAN_FAILED==UINT32_C(0x8000004A));
    CHECK(ERR_PERSISTENT_LAUNCH_FAILED==UINT32_C(0x8000004B));
    CHECK(ERR_PERSISTENT_CLEANUP_FAILED==UINT32_C(0x8000004C));
    CHECK(ERR_PERSISTENT_LIFECYCLE_FAILED==UINT32_C(0x8000004D));
    CHECK(sizeof(hypervisor_hook_request_t)==16 &&
          offsetof(hypervisor_hook_request_t,permissions)==8);
    CHECK(sizeof(hypervisor_status_response_t)==16 &&
          offsetof(hypervisor_status_response_t,error_code)==12);
    CHECK(sizeof(hypervisor_probe_response_t)==88 &&
          offsetof(hypervisor_probe_response_t,feature_control)==32);
    CHECK(sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2)==280 &&
          offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,ShutdownExit)==232);
    CHECK((uint32_t)IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY==UINT32_C(0x8000201C));
    CHECK(IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY!=IOCTL_HYPERVISOR_LAUNCH_PROBE);
    CHECK(IoctlGetAction((uint32_t)IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY)==IOCTL_ACTION_UNKNOWN);
    CHECK(IoctlValidatePersistentStatusQueryRequest(&q,sizeof(q))==HYPERVISOR_PERSISTENT_STATUS_QUERY_VALID);
    CHECK(IoctlValidatePersistentStatusQueryRequest(NULL,sizeof(q))==HYPERVISOR_PERSISTENT_STATUS_QUERY_NULL_INPUT);
    CHECK(IoctlValidatePersistentStatusQueryRequest(&q,0)==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_LENGTH);
    CHECK(IoctlValidatePersistentStatusQueryRequest(&q,sizeof(q)-1)==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_LENGTH);
    CHECK(IoctlValidatePersistentStatusQueryRequest(&q,sizeof(q)+1)==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_LENGTH);
    bad=q;bad.Version=0;
    CHECK(IoctlValidatePersistentStatusQueryRequest(&bad,sizeof(bad))==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_VERSION);
    bad=q;bad.Version=2;
    CHECK(IoctlValidatePersistentStatusQueryRequest(&bad,sizeof(bad))==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_VERSION);
    bad=q;bad.Size=0;
    CHECK(IoctlValidatePersistentStatusQueryRequest(&bad,sizeof(bad))==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_SIZE);
    bad=q;bad.Size=sizeof(bad)+1;
    CHECK(IoctlValidatePersistentStatusQueryRequest(&bad,sizeof(bad))==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_SIZE);
    bad=q;bad.Flags=1;
    CHECK(IoctlValidatePersistentStatusQueryRequest(&bad,sizeof(bad))==HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_FLAGS);
    bad=q;bad.Reserved=1;
    CHECK(IoctlValidatePersistentStatusQueryRequest(&bad,sizeof(bad))==HYPERVISOR_PERSISTENT_STATUS_QUERY_NONZERO_RESERVED);
    CHECK(PersistentStatusQueryCppAbi()==0);
    return 0;
}