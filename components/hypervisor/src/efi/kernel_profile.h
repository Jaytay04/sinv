#ifndef HYPERSINV_EFI_KERNEL_PROFILE_H
#define HYPERSINV_EFI_KERNEL_PROFILE_H

#include <stdint.h>

namespace hypersinv::windows {

struct KernelProfile {
    uint32_t SchemaVersion = 1;
    uint32_t BuildNumber;
    uint32_t Ubr;
    uint32_t ImageTimestamp;
    uint32_t ImageSize;
    uint8_t PdbGuid[16];
    uint32_t PdbAge;
    uint32_t EprocessSize;
    uint32_t EprocessPcb;
    uint32_t EprocessActiveProcessLinks;
    uint32_t EprocessUniqueProcessId;
    uint32_t EprocessImageFileName;
    uint32_t EprocessImageFileNameSize;
    uint32_t KprocessDirectoryTableBase;
    uint32_t EprocessDirectoryTableBase;
    uint32_t PsActiveProcessHeadRva;
};

static_assert(sizeof(KernelProfile) == 76,
              "kernel profile layout drift");

} // namespace hypersinv::windows

#endif /* HYPERSINV_EFI_KERNEL_PROFILE_H */
