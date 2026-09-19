#include "test_common.h"
#include "vmx_ept_runtime_adoption_coordinator.h"
#include "vmx_persistent_activate.h"
#include "vmx_persistent_context.h"
#include "vmx_persistent_lifecycle.h"
#include <string.h>
#define ALL_RESOURCES                                                          \
  ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4))
#define ALL_OWNERS (VMX_PERSISTENT_OWNER_CONTEXT | VMX_PERSISTENT_OWNER_RUNTIME)
typedef struct FAKE {
  char Calls[8];
  unsigned Count;
  int32_t Acquire, Adopt, Rollback, Release;
  uint32_t Published;
  VMX_PERSISTENT_ROOT_PROOF Proof;
  VMX_EPT_RUNTIME_ROOT_PLAN Seen;
  VMX_PROCESSOR_IDENTITY Identity;
} FAKE;
static VMX_PROCESSOR_IDENTITY Identity(void) {
  VMX_PROCESSOR_IDENTITY i = {1, 2, 0, 65};
  return i;
}
static VMX_PERSISTENT_ROOT_PROOF Proof(void) {
  VMX_PERSISTENT_ROOT_PROOF p = {0};
  p.Version = VMX_PERSISTENT_ROOT_PROOF_VERSION;
  p.Identity = Identity();
  p.VmxonPhysicalAddress = 0x2000;
  p.VmcsPhysicalAddress = p.CurrentVmcsPhysicalAddress = 0x3000;
  p.ContextVersion = VMX_PERSISTENT_CONTEXT_VERSION;
  p.ContextOwnedResources = ALL_RESOURCES;
  p.ContextPublished = p.VmxActive = p.VmxCurrent = p.RecordActive =
      p.CurrentVmcsValid = p.OperationsAuthenticated = 1;
  p.RecordStage = VMX_PERSISTENT_CPU_ACTIVE;
  p.RecordOwnedMask = ALL_OWNERS;
  return p;
}
static int32_t Acquire(void *c, const VMX_PROCESSOR_IDENTITY *i,
                       VMX_PERSISTENT_ROOT_PROOF *p) {
  FAKE *f = c;
  f->Calls[f->Count++] = 'A';
  f->Identity = *i;
  *p = f->Proof;
  return f->Acquire;
}
static int32_t Adopt(void *c, const VMX_EPT_RUNTIME_ROOT_PLAN *p,
                     uint32_t *out) {
  FAKE *f = c;
  f->Calls[f->Count++] = 'D';
  f->Seen = *p;
  *out = f->Published;
  return f->Adopt;
}
static int32_t Rollback(void *c, const VMX_EPT_RUNTIME_ROOT_PLAN *p) {
  FAKE *f = c;
  f->Calls[f->Count++] = 'B';
  f->Seen = *p;
  return f->Rollback;
}
static int32_t Release(void *c, const VMX_PROCESSOR_IDENTITY *i) {
  FAKE *f = c;
  f->Calls[f->Count++] = 'R';
  f->Identity = *i;
  return f->Release;
}
static VMX_EPT_RUNTIME_ADOPTION_PLATFORM Platform(FAKE *f) {
  VMX_EPT_RUNTIME_ADOPTION_PLATFORM p = {0};
  p.Context = f;
  p.ContextBytes = sizeof(*f);
  p.AcquireLease = Acquire;
  p.AdoptExisting = Adopt;
  p.RollbackBookkeeping = Rollback;
  p.ReleaseLease = Release;
  return p;
}
static void Calls(FAKE *f, const char *s) {
  TEST_ASSERT_TRUE(f->Count == strlen(s) && memcmp(f->Calls, s, f->Count) == 0,
                   s);
}
static VMX_EPT_RUNTIME_ADOPTION_REPORT Sentinel(void) {
  VMX_EPT_RUNTIME_ADOPTION_REPORT r;
  memset(&r, 0xA5, sizeof(r));
  return r;
}
static void Success(void) {
  FAKE f = {0};
  VMX_PROCESSOR_IDENTITY i = Identity();
  VMX_EPT_RUNTIME_ADOPTION_PLATFORM p;
  VMX_EPT_RUNTIME_ADOPTION_REPORT r = Sentinel(), before = r;
  f.Proof = Proof();
  f.Published = 1;
  p = Platform(&f);
  p.Context = &r;
  p.ContextBytes = sizeof(r);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_INVALID, "overlap");
  TEST_ASSERT_TRUE(memcmp(&r, &before, sizeof(r)) == 0, "preflight preserves");
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_OK, "success");
  Calls(&f, "ADR");
  TEST_ASSERT_UINT_EQ(r.Plan.NativeEnterRequired, 0, "no second VMXON");
  TEST_ASSERT_UINT_EQ(r.LeaseHeld, 0, "released");
  TEST_ASSERT_UINT_EQ(r.RootAdopted, 1, "adopted");
  TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(&f.Identity, &i),
                   "multi-group identity");
}
static void Failures(void) {
  FAKE f = {0};
  VMX_PROCESSOR_IDENTITY i = Identity();
  VMX_EPT_RUNTIME_ADOPTION_PLATFORM p;
  VMX_EPT_RUNTIME_ADOPTION_REPORT r;
  f.Proof = Proof();
  f.Acquire = -1;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_ACQUIRE_FAILED, "acquire");
  Calls(&f, "A");
  memset(&f, 0, sizeof(f));
  f.Proof = Proof();
  f.Proof.VmxActive = 0;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_PLAN_FAILED, "plan");
  Calls(&f, "AR");
  memset(&f, 0, sizeof(f));
  f.Proof = Proof();
  f.Adopt = -2;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_ADOPT_FAILED, "unpublished");
  Calls(&f, "ADR");
  memset(&f, 0, sizeof(f));
  f.Proof = Proof();
  f.Adopt = -2;
  f.Published = 1;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_ADOPT_FAILED,
                     "published rollback");
  Calls(&f, "ADBR");
  TEST_ASSERT_UINT_EQ(r.RootAdopted, 0, "rolled back");
  memset(&f, 0, sizeof(f));
  f.Proof = Proof();
  f.Published = 2;
  f.Rollback = -3;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_ROLLBACK_FAILED, "rollback fail");
  Calls(&f, "ADBR");
  TEST_ASSERT_UINT_EQ(r.RootAdopted, 1, "conservative adopted");
  memset(&f, 0, sizeof(f));
  f.Proof = Proof();
  f.Published = 1;
  f.Release = -4;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_RELEASE_FAILED,
                     "release precedence");
  Calls(&f, "ADR");
  TEST_ASSERT_UINT_EQ(r.LeaseHeld, 1, "lease retained");
}
static void PublicationAndReleaseFailures(void) {
  FAKE f = {0};
  VMX_PROCESSOR_IDENTITY i = Identity();
  VMX_EPT_RUNTIME_ADOPTION_PLATFORM p;
  VMX_EPT_RUNTIME_ADOPTION_REPORT r;
  f.Proof = Proof();
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_ADOPT_FAILED,
                     "success without publication");
  Calls(&f, "ADR");
  memset(&f, 0, sizeof(f));
  f.Proof = Proof();
  f.Proof.VmxActive = 0;
  f.Release = -5;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_RELEASE_FAILED,
                     "release overrides plan failure");
  Calls(&f, "AR");
  TEST_ASSERT_INT_EQ(r.PlanStatus, VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE,
                     "prior plan status retained");
  memset(&f, 0, sizeof(f));
  f.Proof = Proof();
  f.Adopt = -6;
  f.Release = -7;
  p = Platform(&f);
  TEST_ASSERT_INT_EQ(VmxEptCoordinateRuntimeRootAdoption(&i, &p, &r),
                     VMX_EPT_RUNTIME_ADOPTION_RELEASE_FAILED,
                     "release overrides adopt failure");
  Calls(&f, "ADR");
  TEST_ASSERT_INT_EQ(r.AdoptStatus, -6, "prior adopt status retained");
}
int main(void) {
  Success();
  Failures();
  PublicationAndReleaseFailures();
  return 0;
}
