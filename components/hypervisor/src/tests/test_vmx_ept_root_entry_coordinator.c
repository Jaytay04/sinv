#include "test_common.h"
#include "vmx_ept_root_entry_coordinator.h"

#include <string.h>

typedef struct FAKE {
    char Calls[8]; unsigned Count;
    int32_t Prepare, Enter, CompleteSuccess, CompleteCancel, Release, Recover;
    uint32_t CompleteFlag;
} FAKE;

static int32_t Step(FAKE* f, char call, int32_t status)
{ f->Calls[f->Count++] = call; return status; }
static int32_t Prepare(void* c, const VMX_PROCESSOR_IDENTITY* i)
{ (void)i; return Step((FAKE*)c, 'P', ((FAKE*)c)->Prepare); }
static int32_t Enter(void* c, const VMX_PROCESSOR_IDENTITY* i)
{ (void)i; return Step((FAKE*)c, 'E', ((FAKE*)c)->Enter); }
static int32_t Complete(void* c, const VMX_PROCESSOR_IDENTITY* i, uint32_t ok)
{ FAKE* f=(FAKE*)c;(void)i;f->CompleteFlag=ok;return Step(f,'C',ok?f->CompleteSuccess:f->CompleteCancel); }
static int32_t Release(void* c, const VMX_PROCESSOR_IDENTITY* i)
{ (void)i; return Step((FAKE*)c, 'L', ((FAKE*)c)->Release); }
static int32_t Recover(void* c, const VMX_PROCESSOR_IDENTITY* i)
{ (void)i; return Step((FAKE*)c, 'R', ((FAKE*)c)->Recover); }
static VMX_EPT_ROOT_ENTRY_PLATFORM Platform(FAKE* f)
{
    VMX_EPT_ROOT_ENTRY_PLATFORM p={0};p.Context=f;p.ContextBytes=sizeof(*f);
    p.PrepareEnter=Prepare;p.NativeEnter=Enter;p.CompleteEnter=Complete;
    p.NativeRelease=Release;p.RecoverAfterRelease=Recover;return p;
}
static VMX_PROCESSOR_IDENTITY Identity(void)
{ VMX_PROCESSOR_IDENTITY i={0};i.Group=1;i.Number=2;i.SystemIndex=3;return i; }
static VMX_EPT_ROOT_ENTRY_REPORT Sentinel(void)
{ VMX_EPT_ROOT_ENTRY_REPORT r;memset(&r,0xA5,sizeof(r));return r; }
static void Calls(const FAKE* f,const char* expected,const char* label)
{ TEST_ASSERT_TRUE(f->Count==strlen(expected)&&memcmp(f->Calls,expected,f->Count)==0,label); }
static int test_success_and_preflight(void)
{
    FAKE f={0};VMX_PROCESSOR_IDENTITY i=Identity();VMX_EPT_ROOT_ENTRY_PLATFORM p=Platform(&f);
    VMX_EPT_ROOT_ENTRY_REPORT r=Sentinel(),before=r;
    p.Context=&r;p.ContextBytes=sizeof(r);
    TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_INVALID,"overlap invalid");
    TEST_ASSERT_TRUE(memcmp(&r,&before,sizeof(r))==0,"preflight preserves report");
    p=Platform(&f);
    TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_OK,"entry success");
    Calls(&f,"PEC","success order");TEST_ASSERT_UINT_EQ(f.CompleteFlag,1,"complete success flag");
    TEST_ASSERT_UINT_EQ(r.RootActive,1,"success root active");TEST_ASSERT_UINT_EQ(r.TransitionPending,0,"success no pending");
    return 0;
}
static int test_prepare_and_enter_failures(void)
{
    FAKE f={0};VMX_PROCESSOR_IDENTITY i=Identity();VMX_EPT_ROOT_ENTRY_PLATFORM p=Platform(&f);
    VMX_EPT_ROOT_ENTRY_REPORT r;
    f.Prepare=-11;TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_PREPARE_FAILED,"prepare failure");
    Calls(&f,"P","prepare stops");TEST_ASSERT_UINT_EQ(r.TransitionPending,0,"prepare failure atomic");
    memset(&f,0,sizeof(f));p=Platform(&f);f.Enter=-12;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_NATIVE_ENTER_FAILED,"enter failure canceled");
    Calls(&f,"PEC","enter cancel order");TEST_ASSERT_UINT_EQ(f.CompleteFlag,0,"cancel flag");TEST_ASSERT_UINT_EQ(r.TransitionPending,0,"cancel clears pending");
    memset(&f,0,sizeof(f));p=Platform(&f);f.Enter=-12;f.CompleteCancel=-13;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_CANCEL_FAILED,"cancel failure");
    TEST_ASSERT_UINT_EQ(r.TransitionPending,1,"failed cancel pending");
    return 0;
}
static int test_completion_rollback(void)
{
    FAKE f={0};VMX_PROCESSOR_IDENTITY i=Identity();VMX_EPT_ROOT_ENTRY_PLATFORM p=Platform(&f);
    VMX_EPT_ROOT_ENTRY_REPORT r;f.CompleteSuccess=-21;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_COMPLETE_FAILED,"complete rollback");
    Calls(&f,"PECLR","rollback order");TEST_ASSERT_UINT_EQ(r.RootActive,0,"rollback inactive");TEST_ASSERT_UINT_EQ(r.TransitionPending,0,"recovery clears pending");
    memset(&f,0,sizeof(f));p=Platform(&f);f.CompleteSuccess=-21;f.Release=-22;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_NATIVE_RELEASE_FAILED,"release failure");
    Calls(&f,"PECL","release failure stops");TEST_ASSERT_UINT_EQ(r.RootActive,1,"failed release active");TEST_ASSERT_UINT_EQ(r.TransitionPending,1,"failed release pending");
    memset(&f,0,sizeof(f));p=Platform(&f);f.CompleteSuccess=-21;f.Recover=-23;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateRootEntry(&i,&p,&r),VMX_EPT_ROOT_ENTRY_RECOVERY_FAILED,"recovery failure");
    Calls(&f,"PECLR","recovery failure order");TEST_ASSERT_UINT_EQ(r.RootActive,0,"released before recovery");TEST_ASSERT_UINT_EQ(r.TransitionPending,1,"failed recovery pending");
    return 0;
}
int main(void)
{ return test_success_and_preflight()|test_prepare_and_enter_failures()|test_completion_rollback(); }
