#include "vmx_persistent_guarded_platform.h"
#include <string.h>
typedef struct _SPAN{uintptr_t B,E;}SPAN;
static bool Span(const void*p,size_t n,SPAN*s)
{uintptr_t a=(uintptr_t)p;if(!p||!n||n>UINTPTR_MAX-a)return false;s->B=a;s->E=a+n;return true;}
static bool Overlap(SPAN a,SPAN b){return a.B<b.E&&b.B<a.E;}
static bool Zero(const void*p,size_t n)
{const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static bool Valid(const VMX_PERSISTENT_KERNEL_PLATFORM*p)
{return p&&p->QueryActiveCount&&p->MapSystemIndex&&p->Allocate&&p->Free&&
 p->CheckPassive&&p->Pin&&p->QueryCurrent&&p->Restore;}
static bool Equal(const VMX_PERSISTENT_KERNEL_PLATFORM*a,const VMX_PERSISTENT_KERNEL_PLATFORM*b)
{return a->Context==b->Context&&a->QueryActiveCount==b->QueryActiveCount&&
 a->MapSystemIndex==b->MapSystemIndex&&a->Allocate==b->Allocate&&a->Free==b->Free&&
 a->CheckPassive==b->CheckPassive&&a->Pin==b->Pin&&
 a->QueryCurrent==b->QueryCurrent&&a->Restore==b->Restore;}
static bool QueryCount(void*,size_t*);static bool Map(void*,uint32_t,VMX_PROCESSOR_IDENTITY*);
static void* Allocate(void*,size_t);static void Free(void*,void*);static bool Passive(void*);
static VMX_PERSISTENT_KERNEL_STATUS Pin(void*,const VMX_PROCESSOR_IDENTITY*,VMX_PERSISTENT_AFFINITY_TOKEN*);
static VMX_PERSISTENT_KERNEL_STATUS Current(void*,VMX_PROCESSOR_IDENTITY*);
static VMX_PERSISTENT_KERNEL_STATUS Restore(void*,VMX_PERSISTENT_AFFINITY_TOKEN*);static bool Consistent(const VMX_PERSISTENT_GUARDED_PLATFORM*g)
{
 return g&&g->Magic==VMX_PERSISTENT_GUARDED_PLATFORM_MAGIC&&
  g->Version==VMX_PERSISTENT_GUARDED_PLATFORM_VERSION&&Valid(&g->Source)&&
  Equal(&g->Source,&g->SourceContract)&&
  g->ForbiddenBegin==g->ForbiddenBeginContract&&
  g->ForbiddenEnd==g->ForbiddenEndContract&&g->ForbiddenBegin<g->ForbiddenEnd&&
  g->Platform.Context==(void*)g&&Equal(&g->Platform,&g->PlatformContract)&&
  g->Platform.QueryActiveCount==QueryCount&&g->Platform.MapSystemIndex==Map&&
  g->Platform.Allocate==Allocate&&g->Platform.Free==Free&&
  g->Platform.CheckPassive==Passive&&g->Platform.Pin==Pin&&
  g->Platform.QueryCurrent==Current&&g->Platform.Restore==Restore;
}
static bool QueryCount(void*x,size_t*n)
{VMX_PERSISTENT_GUARDED_PLATFORM*g=x;if(!Consistent(g)||!n)return false;
 return g->Source.QueryActiveCount(g->Source.Context,n);}
static bool Map(void*x,uint32_t i,VMX_PROCESSOR_IDENTITY*p)
{VMX_PERSISTENT_GUARDED_PLATFORM*g=x;if(!Consistent(g)||!p)return false;
 return g->Source.MapSystemIndex(g->Source.Context,i,p);}
static void* Allocate(void*x,size_t n)
{
 VMX_PERSISTENT_GUARDED_PLATFORM*g=x;SPAN a,f;void*p;
 if(!Consistent(g))return NULL;p=g->Source.Allocate(g->Source.Context,n);
 f.B=g->ForbiddenBegin;f.E=g->ForbiddenEnd;
 if(p&&(!Span(p,n,&a)||Overlap(a,f))){g->Source.Free(g->Source.Context,p);return NULL;}
 return p;
}
static void Free(void*x,void*p)
{VMX_PERSISTENT_GUARDED_PLATFORM*g=x;if(Consistent(g)&&p)g->Source.Free(g->Source.Context,p);}
static bool Passive(void*x)
{VMX_PERSISTENT_GUARDED_PLATFORM*g=x;return Consistent(g)&&g->Source.CheckPassive(g->Source.Context);}
static VMX_PERSISTENT_KERNEL_STATUS Pin(void*x,const VMX_PROCESSOR_IDENTITY*i,
 VMX_PERSISTENT_AFFINITY_TOKEN*t)
{VMX_PERSISTENT_GUARDED_PLATFORM*g=x;if(!Consistent(g)||!i||!t)return VMX_PERSISTENT_KERNEL_INVALID;
 return g->Source.Pin(g->Source.Context,i,t);}
static VMX_PERSISTENT_KERNEL_STATUS Current(void*x,VMX_PROCESSOR_IDENTITY*i)
{VMX_PERSISTENT_GUARDED_PLATFORM*g=x;if(!Consistent(g)||!i)return VMX_PERSISTENT_KERNEL_INVALID;
 return g->Source.QueryCurrent(g->Source.Context,i);}
static VMX_PERSISTENT_KERNEL_STATUS Restore(void*x,VMX_PERSISTENT_AFFINITY_TOKEN*t)
{VMX_PERSISTENT_GUARDED_PLATFORM*g=x;if(!Consistent(g)||!t)return VMX_PERSISTENT_KERNEL_INVALID;
 return g->Source.Restore(g->Source.Context,t);}
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentGuardedPlatformInitialize(
 const VMX_PERSISTENT_KERNEL_PLATFORM*s,const void*forbidden,size_t size,
 VMX_PERSISTENT_GUARDED_PLATFORM*out)
{
 VMX_PERSISTENT_GUARDED_PLATFORM g={0};SPAN a,b,f;uint32_t magic=0;
 if(!out)return VMX_PERSISTENT_KERNEL_INVALID;
 memcpy(&magic,&out->Magic,sizeof(magic));
 if(magic==VMX_PERSISTENT_GUARDED_PLATFORM_MAGIC)return VMX_PERSISTENT_GUARDED_PLATFORM_BUSY;
 if(!Zero(out,sizeof(*out))||!Valid(s)||!Span(s,sizeof(*s),&a)||
    !Span(out,sizeof(*out),&b)||!Span(forbidden,size,&f)||Overlap(a,b)||
    b.B<f.B||b.E>f.E)
    return VMX_PERSISTENT_KERNEL_INVALID;
 g.Magic=VMX_PERSISTENT_GUARDED_PLATFORM_MAGIC;
 g.Version=VMX_PERSISTENT_GUARDED_PLATFORM_VERSION;g.Source=g.SourceContract=*s;
 g.ForbiddenBegin=g.ForbiddenBeginContract=f.B;
 g.ForbiddenEnd=g.ForbiddenEndContract=f.E;
 g.Platform=(VMX_PERSISTENT_KERNEL_PLATFORM){out,QueryCount,Map,Allocate,Free,
    Passive,Pin,Current,Restore};g.PlatformContract=g.Platform;*out=g;return VMX_PERSISTENT_KERNEL_OK;
}