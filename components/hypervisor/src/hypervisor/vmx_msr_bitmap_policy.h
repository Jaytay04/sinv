#ifndef HYPERSINV_VMX_MSR_BITMAP_POLICY_H
#define HYPERSINV_VMX_MSR_BITMAP_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_MSR_BITMAP_SIZE 4096u

typedef enum VMX_MSR_BITMAP_POLICY_STATUS {
    VMX_MSR_BITMAP_POLICY_OK = 0,
    VMX_MSR_BITMAP_POLICY_INVALID = 1
} VMX_MSR_BITMAP_POLICY_STATUS;

/* Build a selective Intel MSR bitmap. Addressable native MSRs remain clear and
 * execute directly in non-root mode. Policy-owned MSRs are intercepted for
 * both reads and writes. Intel unconditionally exits for MSRs outside the two
 * bitmap-addressable ranges, so the Hyper-V synthetic range needs no bit. */
VMX_MSR_BITMAP_POLICY_STATUS VmxMsrBitmapPolicyBuild(
    uint8_t* Bitmap,
    size_t BitmapSize);

/* Model whether Intel's bitmap semantics cause an access to VM-exit. */
bool VmxMsrBitmapPolicyAccessExits(
    const uint8_t* Bitmap,
    size_t BitmapSize,
    uint32_t Msr,
    bool Write);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_MSR_BITMAP_POLICY_H */
