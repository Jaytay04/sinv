#include <string.h>
#include "test_common.h"
#include "vmx_ept_runtime_root_plan.h"
#include "vmx_persistent_context.h"
#include "vmx_persistent_activate.h"
#include "vmx_persistent_lifecycle.h"

#define ALL_RESOURCES ((1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4))
#define ALL_OWNERS (VMX_PERSISTENT_OWNER_CONTEXT | VMX_PERSISTENT_OWNER_RUNTIME)

static VMX_PERSISTENT_ROOT_PROOF Valid(void)
{
    VMX_PERSISTENT_ROOT_PROOF p;
    memset(&p,0,sizeof(p));
    p.Version=VMX_PERSISTENT_ROOT_PROOF_VERSION;
    p.Identity.Group=1;p.Identity.Number=2;p.Identity.SystemIndex=65;
    p.VmxonPhysicalAddress=0x2000;p.VmcsPhysicalAddress=0x3000;
    p.CurrentVmcsPhysicalAddress=0x3000;
    p.ContextVersion=VMX_PERSISTENT_CONTEXT_VERSION;
    p.ContextOwnedResources=ALL_RESOURCES;p.ContextPublished=1;
    p.VmxActive=1;p.VmxCurrent=1;p.RecordStage=VMX_PERSISTENT_CPU_ACTIVE;
    p.RecordOwnedMask=ALL_OWNERS;p.RecordActive=1;
    p.CurrentVmcsValid=1;p.OperationsAuthenticated=1;
    return p;
}
static VMX_EPT_RUNTIME_ROOT_PLAN Sentinel(void)
{VMX_EPT_RUNTIME_ROOT_PLAN p;memset(&p,0xA5,sizeof(p));return p;}

static void Success(void)
{
    VMX_PERSISTENT_ROOT_PROOF p=Valid();
    VMX_EPT_RUNTIME_ROOT_PLAN out=Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptRuntimeRootPlanBuild(&p,&out),
        VMX_EPT_RUNTIME_ROOT_PLAN_OK,"valid active proof");
    TEST_ASSERT_UINT_EQ(out.Version,VMX_EPT_RUNTIME_ROOT_PLAN_VERSION,"version");
    TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(&out.Identity,&p.Identity),"identity");
    TEST_ASSERT_UINT_EQ(out.AdoptExistingRoot,1,"adopt");
    TEST_ASSERT_UINT_EQ(out.NativeEnterRequired,0,"no second VMXON");
    TEST_ASSERT_UINT_EQ(out.VmcsPhysicalAddress,p.VmcsPhysicalAddress,"VMCS");
}
static void Preserve(void)
{
    VMX_PERSISTENT_ROOT_PROOF p=Valid();VMX_EPT_RUNTIME_ROOT_PLAN out,before;
#define REJECT(field,value,status) do{p=Valid();out=Sentinel();before=out;p.field=(value); \
 TEST_ASSERT_INT_EQ(VmxEptRuntimeRootPlanBuild(&p,&out),(status),#field); \
 TEST_ASSERT_TRUE(memcmp(&out,&before,sizeof(out))==0,#field " preserves");}while(0)
    out=Sentinel();before=out;
    TEST_ASSERT_INT_EQ(VmxEptRuntimeRootPlanBuild(NULL,&out),
        VMX_EPT_RUNTIME_ROOT_PLAN_INVALID,"null");
    TEST_ASSERT_TRUE(memcmp(&out,&before,sizeof(out))==0,"null preserves");
    REJECT(Version,2,VMX_EPT_RUNTIME_ROOT_PLAN_INVALID);
    REJECT(Identity.Reserved,1,VMX_EPT_RUNTIME_ROOT_PLAN_INVALID);
    REJECT(VmxonPhysicalAddress,0x2100,VMX_EPT_RUNTIME_ROOT_PLAN_INVALID);
    REJECT(ContextOwnedResources,0,VMX_EPT_RUNTIME_ROOT_PLAN_DIRTY);
    REJECT(CleanupRequired,1,VMX_EPT_RUNTIME_ROOT_PLAN_DIRTY);
    REJECT(VmxActive,0,VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE);
    REJECT(VmxCurrent,0,VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE);
    REJECT(RecordStage,VMX_PERSISTENT_CPU_PREPARED,VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE);
    REJECT(CurrentVmcsPhysicalAddress,0x4000,VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE);
    REJECT(OperationsAuthenticated,0,VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE);
#undef REJECT
}
int main(void){Success();Preserve();return 0;}