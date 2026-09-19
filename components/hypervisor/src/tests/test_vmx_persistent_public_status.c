#include "test_common.h"
#include "vmx_persistent_public_status.h"
#include "vmx_persistent_activate.h"
#include <string.h>
#define N 65
#define OUT_BYTES (VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE+N*VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE)
typedef struct _FIXTURE{VMX_PERSISTENT_CPU_RECORD Records[N];VMX_PERSISTENT_CONTEXT Contexts[N];VMX_PERSISTENT_OPERATION_RESULT Results[N];VMX_PERSISTENT_LIFECYCLE Lifecycle;VMX_PERSISTENT_PUBLIC_STATUS_VIEW View;}FIXTURE;
static void Setup(FIXTURE*f,size_t n)
{
 size_t i;memset(f,0,sizeof(*f));f->Lifecycle.State=VMX_PERSISTENT_LOADED;
 f->Lifecycle.Cpus=f->Records;f->Lifecycle.Count=n;
 for(i=0;i<n;i++){VMX_PROCESSOR_IDENTITY id={(uint16_t)(i/64),(uint8_t)(i%64),0,(uint32_t)i};
  f->Records[i].Identity=id;f->Records[i].Stage=VMX_PERSISTENT_CPU_PREPARED;
  f->Contexts[i].Identity=id;f->Contexts[i].Version=VMX_PERSISTENT_CONTEXT_VERSION;
  f->Results[i].Stage=VMX_PERSISTENT_OPERATION_NOT_STARTED;
  f->Results[i].PrimaryStatus=f->Results[i].CleanupStatus=VMX_PERSISTENT_OPERATION_OK;}
 f->View=(VMX_PERSISTENT_PUBLIC_STATUS_VIEW){VMX_PERSISTENT_PUBLIC_STATUS_VERSION,
  VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE,VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE,0,
  &f->Lifecycle,f->Contexts,n,f->Results,n,n};
}
static bool ZeroBytes(const void*p,size_t n){const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static int test_multi_group_deterministic_diagnostics(void)
{
 FIXTURE f;unsigned char a[OUT_BYTES+9],b[OUT_BYTES+9];size_t required=0;
 VMX_PERSISTENT_PUBLIC_STATUS_HEADER h;VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD r;
 Setup(&f,N);f.Lifecycle.State=VMX_PERSISTENT_ERROR;f.Lifecycle.Busy=true;
 f.Lifecycle.HasOperationError=true;f.Lifecycle.FirstOperationStatus=VMX_PERSISTENT_STATUS_NOT_READY;f.Lifecycle.FirstOperationIndex=64;
 f.Lifecycle.HasCleanupError=true;f.Lifecycle.FirstCleanupStatus=VMX_PERSISTENT_STATUS_INVARIANT;f.Lifecycle.FirstCleanupIndex=64;
 f.Records[64].Active=true;f.Records[64].CleanupRequired=true;f.Records[64].OwnedMask=7;
 f.Contexts[64].Published=true;f.Contexts[64].OwnedResources=63;f.Contexts[64].CpuState.RestorationRequired=true;
 f.Contexts[64].CpuState.IrqlRaised=true;f.Contexts[64].CpuState.VmxActive=true;f.Contexts[64].CpuState.ShutdownRequested=true;
 f.Results[64].Stage=VMX_PERSISTENT_OPERATION_FAILED;f.Results[64].PrimaryStatus=VMX_PERSISTENT_OPERATION_INSTRUCTION_FAILED;
 f.Results[64].CleanupStatus=VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;f.Results[64].InstructionLevelEntered=true;
 f.Results[64].Vmxon.Outcome=VMX_V3D_INSTRUCTION_SUCCESS;
 f.Results[64].Vmptrld.Outcome=VMX_V3D_INSTRUCTION_FAIL_INVALID;
 f.Results[64].Plan=(VMX_PERSISTENT_INSTRUCTION_REPORT){VMX_V3D_INSTRUCTION_FAIL_VALID,1,9};
 f.Results[64].CleanupVmclear=(VMX_PERSISTENT_INSTRUCTION_REPORT){VMX_V3D_INSTRUCTION_FAIL_VALID,1,7};
 f.Results[64].CleanupVmxoff.Outcome=VMX_V3D_INSTRUCTION_SUCCESS;
 TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,NULL,0,&required),VMX_PERSISTENT_PUBLIC_STATUS_BUFFER_TOO_SMALL,"query");
 TEST_ASSERT_UINT_EQ(required,OUT_BYTES,"required");memset(a,0xA5,sizeof(a));memset(b,0x5A,sizeof(b));
 TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,a,sizeof(a),&required),0,"encode a");
 TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,b,sizeof(b),&required),0,"encode b");
 TEST_ASSERT_TRUE(!memcmp(a,b,sizeof(a)),"deterministic bytes");memcpy(&h,a,sizeof(h));memcpy(&r,a+sizeof(h)+64*sizeof(r),sizeof(r));
 TEST_ASSERT_TRUE(h.Version==3&&h.ContextCount==65&&h.TotalSize==OUT_BYTES&&h.FirstOperationIndex==64&&h.FirstCleanupIndex==64,"header exact");
 TEST_ASSERT_TRUE(r.SystemIndex==64&&r.Group==1&&r.Number==0&&r.OwnedMask==7&&r.ContextOwnedResources==63,"multi group/owned");
 TEST_ASSERT_TRUE((r.ResidualFlags&(VMX_PERSISTENT_PUBLIC_RESIDUAL_RESTORE|VMX_PERSISTENT_PUBLIC_RESIDUAL_IRQL|VMX_PERSISTENT_PUBLIC_RESIDUAL_VMX_ACTIVE|VMX_PERSISTENT_PUBLIC_RESIDUAL_SHUTDOWN))!=0,"residuals");
 TEST_ASSERT_TRUE(r.PrimarySource==VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_VMPTRLD&&r.PrimaryOutcome==VMX_V3D_INSTRUCTION_FAIL_INVALID,"first primary failure");
 TEST_ASSERT_TRUE(r.CleanupSource==VMX_PERSISTENT_PUBLIC_DIAGNOSTIC_CLEANUP_VMCLEAR&&r.CleanupErrorValid==1&&r.CleanupError==7,"first cleanup failure");
 TEST_ASSERT_TRUE(ZeroBytes(a+OUT_BYTES,sizeof(a)-OUT_BYTES),"tail zero");return 0;
}
static bool InvalidClears(FIXTURE*f)
{unsigned char out[64];size_t required=99;memset(out,0xA5,sizeof(out));return VmxPersistentPublicStatusEncode(&f->View,out,sizeof(out),&required)==VMX_PERSISTENT_PUBLIC_STATUS_INVALID&&required==0&&ZeroBytes(out,sizeof(out));}
static int test_zero_undersized_invalid(void)
{
 FIXTURE f;unsigned char out[OUT_BYTES];size_t required=0;Setup(&f,0);
 TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,out,sizeof(out),&required),0,"zero count");
 TEST_ASSERT_UINT_EQ(required,sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER),"zero required");
 Setup(&f,N);memset(out,0xA5,sizeof(out));TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,out,OUT_BYTES-1,&required),VMX_PERSISTENT_PUBLIC_STATUS_BUFFER_TOO_SMALL,"undersized");
 TEST_ASSERT_TRUE(required==OUT_BYTES&&ZeroBytes(out,OUT_BYTES-1),"undersized zero");
 f.View.Version=4;TEST_ASSERT_TRUE(InvalidClears(&f),"version");f.View.Version=3;f.View.HeaderSize++;
 TEST_ASSERT_TRUE(InvalidClears(&f),"header size");f.View.HeaderSize=48;f.View.RecordSize++;
 TEST_ASSERT_TRUE(InvalidClears(&f),"record size");f.View.RecordSize=72;f.View.ContextCount=64;
 TEST_ASSERT_TRUE(InvalidClears(&f),"context capacity");f.View.ContextCount=65;f.View.OperationResultCount=64;
 TEST_ASSERT_TRUE(InvalidClears(&f),"result capacity");return 0;
}
static int test_malformed_identity_state_reports(void)
{
 FIXTURE f;Setup(&f,N);f.Records[3].Identity.Reserved=1;TEST_ASSERT_TRUE(InvalidClears(&f),"identity reserved");
 Setup(&f,N);f.Records[3].Identity.Number=64;TEST_ASSERT_TRUE(InvalidClears(&f),"number");
 Setup(&f,N);f.Records[3].Identity.SystemIndex=4;TEST_ASSERT_TRUE(InvalidClears(&f),"order");
 Setup(&f,N);f.Records[3].Identity.Group=f.Records[2].Identity.Group;f.Records[3].Identity.Number=f.Records[2].Identity.Number;
 f.Contexts[3].Identity=f.Records[3].Identity;TEST_ASSERT_TRUE(InvalidClears(&f),"duplicate");
 Setup(&f,N);f.Contexts[3].Identity.Group++;TEST_ASSERT_TRUE(InvalidClears(&f),"context identity");
 Setup(&f,N);f.Records[3].Reserved[1]=1;TEST_ASSERT_TRUE(InvalidClears(&f),"record reserved");
 Setup(&f,N);f.Results[3].Vmxon=(VMX_PERSISTENT_INSTRUCTION_REPORT){VMX_V3D_INSTRUCTION_SUCCESS,1,4};
 TEST_ASSERT_TRUE(InvalidClears(&f),"success error invalid");
 Setup(&f,N);f.Results[3].Vmxon=(VMX_PERSISTENT_INSTRUCTION_REPORT){VMX_V3D_INSTRUCTION_FAIL_VALID,0,0};
 {size_t required=0;TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,NULL,0,&required),
  VMX_PERSISTENT_PUBLIC_STATUS_BUFFER_TOO_SMALL,"fail valid without VM error accepted");}
 Setup(&f,N);f.Lifecycle.State=(VMX_PERSISTENT_LIFECYCLE_STATE)-1;TEST_ASSERT_TRUE(InvalidClears(&f),"lifecycle lower bound");
 Setup(&f,N);f.Results[3].Stage=(VMX_PERSISTENT_OPERATION_STAGE)-1;TEST_ASSERT_TRUE(InvalidClears(&f),"operation stage lower bound");return 0;
}
static int test_overflow_alias_preservation(void)
{
 FIXTURE f;unsigned char out[64],before[64];size_t required=77;VMX_PERSISTENT_PUBLIC_STATUS_VIEW saved;
 Setup(&f,N);memset(out,0xA5,sizeof(out));memcpy(before,out,sizeof(out));saved=f.View;
 TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,&f.View,sizeof(f.View),&required),VMX_PERSISTENT_PUBLIC_STATUS_INVALID,"output view alias");
 TEST_ASSERT_TRUE(!memcmp(&saved,&f.View,sizeof(saved))&&required==77,"alias preserves");
 TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&f.View,out,sizeof(out),(size_t*)(void*)(out+8)),VMX_PERSISTENT_PUBLIC_STATUS_INVALID,"required output alias");
 TEST_ASSERT_TRUE(!memcmp(out,before,sizeof(out)),"required alias preserves output");
 if(SIZE_MAX>UINT32_MAX){VMX_PERSISTENT_LIFECYCLE l={0};VMX_PERSISTENT_PUBLIC_STATUS_VIEW v=f.View;
  l.Count=UINT32_MAX;l.Cpus=(VMX_PERSISTENT_CPU_RECORD*)(uintptr_t)UINT64_C(0x100000000000);
  v.Lifecycle=&l;v.Contexts=(VMX_PERSISTENT_CONTEXT*)(uintptr_t)UINT64_C(0x300000000000);v.ContextCount=UINT32_MAX;
  v.OperationResults=(VMX_PERSISTENT_OPERATION_RESULT*)(uintptr_t)UINT64_C(0x700000000000);v.OperationResultCount=UINT32_MAX;v.Count=UINT32_MAX;
  required=9;memset(out,0xA5,sizeof(out));TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&v,out,sizeof(out),&required),VMX_PERSISTENT_PUBLIC_STATUS_OVERFLOW,"wire overflow");
  TEST_ASSERT_TRUE(required==0&&ZeroBytes(out,sizeof(out)),"overflow zero after separation");
  l.Count=SIZE_MAX;v.Count=v.ContextCount=v.OperationResultCount=SIZE_MAX;required=9;memset(out,0xA5,sizeof(out));
  TEST_ASSERT_INT_EQ(VmxPersistentPublicStatusEncode(&v,out,sizeof(out),&required),VMX_PERSISTENT_PUBLIC_STATUS_INVALID,"source span overflow");
  TEST_ASSERT_TRUE(required==9&&!memcmp(out,before,sizeof(out)),"unverifiable preserves");}
 return 0;
}
TEST_MAIN_BEGIN()
 TEST_RUN(test_multi_group_deterministic_diagnostics);
 TEST_RUN(test_zero_undersized_invalid);
 TEST_RUN(test_malformed_identity_state_reports);
 TEST_RUN(test_overflow_alias_preservation);
TEST_MAIN_END()
