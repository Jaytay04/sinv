#include "gpu_boot.h"
#include "gpu_native.h"

#include <stddef.h>

#ifndef HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED
#define HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED 0
#endif

#if !defined(HYPERSINV_GPU_DIAGNOSTIC_NATIVE) || \
    (HYPERSINV_GPU_DIAGNOSTIC_NATIVE == 0)

EFI_STATUS HvGpuBootPrepare(HV_EFI_CONTEXT *context,
                            EFI_SYSTEM_TABLE *systemTable,
                            uint64_t ecamBase,
                            uint64_t ecamBytes)
{
    (void)systemTable;
    (void)ecamBase;
    (void)ecamBytes;
    if (context == NULL) return EFI_INVALID_PARAMETER;
    context->GpuStatePhysical = 0;
    return EFI_SUCCESS;
}

EFI_STATUS HvGpuBootArm(HV_EFI_CONTEXT *context)
{
    return context != NULL ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

void HvGpuBootReleaseBeforeLaunch(HV_EFI_CONTEXT *context)
{
    (void)context;
}

#else

/* The protocol layouts below follow UEFI 2.10, sections 9.5, 10.5 and 14.4.
 * Uefi.h intentionally omits these protocols, so retain their specified
 * function-pointer order without importing a second UEFI type universe. */
typedef enum HV_EFI_PCI_IO_WIDTH {
    HvEfiPciIoWidthUint8 = 0,
    HvEfiPciIoWidthUint16 = 1,
    HvEfiPciIoWidthUint32 = 2,
    HvEfiPciIoWidthUint64 = 3
} HV_EFI_PCI_IO_WIDTH;

typedef struct HV_EFI_PCI_IO_PROTOCOL HV_EFI_PCI_IO_PROTOCOL;

typedef EFI_STATUS (EFIAPI *HV_EFI_PCI_CONFIG_ACCESS)(
    HV_EFI_PCI_IO_PROTOCOL *This, HV_EFI_PCI_IO_WIDTH Width,
    UINT32 Offset, UINTN Count, VOID *Buffer);

typedef EFI_STATUS (EFIAPI *HV_EFI_PCI_MEM_ACCESS)(
    HV_EFI_PCI_IO_PROTOCOL *This, HV_EFI_PCI_IO_WIDTH Width,
    UINT8 BarIndex, UINT64 Offset, UINTN Count, VOID *Buffer);

typedef EFI_STATUS (EFIAPI *HV_EFI_PCI_GET_LOCATION)(
    HV_EFI_PCI_IO_PROTOCOL *This, UINTN *Segment, UINTN *Bus,
    UINTN *Device, UINTN *Function);

typedef EFI_STATUS (EFIAPI *HV_EFI_PCI_GET_BAR_ATTRIBUTES)(
    HV_EFI_PCI_IO_PROTOCOL *This, UINT8 BarIndex, UINT64 *Supports,
    VOID **Resources);

struct HV_EFI_PCI_IO_PROTOCOL {
    VOID *PollMem;
    VOID *PollIo;
    struct { HV_EFI_PCI_MEM_ACCESS Read; HV_EFI_PCI_MEM_ACCESS Write; } Mem;
    struct { HV_EFI_PCI_MEM_ACCESS Read; HV_EFI_PCI_MEM_ACCESS Write; } Io;
    struct { HV_EFI_PCI_CONFIG_ACCESS Read; HV_EFI_PCI_CONFIG_ACCESS Write; } Pci;
    VOID *CopyMem;
    VOID *Map;
    VOID *Unmap;
    VOID *AllocateBuffer;
    VOID *FreeBuffer;
    VOID *Flush;
    HV_EFI_PCI_GET_LOCATION GetLocation;
    VOID *Attributes;
    HV_EFI_PCI_GET_BAR_ATTRIBUTES GetBarAttributes;
    VOID *SetBarAttributes;
    UINT64 RomSize;
    VOID *RomImage;
};

typedef struct HV_EFI_GOP_PIXEL_MASK {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} HV_EFI_GOP_PIXEL_MASK;

typedef struct HV_EFI_GOP_MODE_INFO {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelFormat;
    HV_EFI_GOP_PIXEL_MASK PixelInformation;
    UINT32 PixelsPerScanLine;
} HV_EFI_GOP_MODE_INFO;

typedef struct HV_EFI_GOP_MODE {
    UINT32 MaxMode;
    UINT32 Mode;
    HV_EFI_GOP_MODE_INFO *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} HV_EFI_GOP_MODE;

typedef struct HV_EFI_GOP_PROTOCOL {
    VOID *QueryMode;
    VOID *SetMode;
    VOID *Blt;
    HV_EFI_GOP_MODE *Mode;
} HV_EFI_GOP_PROTOCOL;

typedef struct HV_EFI_DEVICE_PATH_NODE {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
} HV_EFI_DEVICE_PATH_NODE;

#pragma pack(push, 1)
typedef struct HV_EFI_ACPI_QWORD_RESOURCE {
    UINT8 Descriptor;
    UINT16 Length;
    UINT8 ResourceType;
    UINT8 GeneralFlags;
    UINT8 SpecificFlags;
    UINT64 Granularity;
    UINT64 Minimum;
    UINT64 Maximum;
    UINT64 Translation;
    UINT64 AddressLength;
} HV_EFI_ACPI_QWORD_RESOURCE;
#pragma pack(pop)

typedef struct HV_GPU_BOOT_BDF {
    UINTN Segment;
    UINTN Bus;
    UINTN Device;
    UINTN Function;
} HV_GPU_BOOT_BDF;

typedef struct HV_GPU_BOOT_BAR {
    uint64_t Base;
    uint64_t Bytes;
} HV_GPU_BOOT_BAR;

typedef struct HV_GPU_BOOT_PLANE_SNAPSHOT {
    uint32_t Control;
    uint32_t Stride;
    uint32_t Size;
    uint32_t Surface;
    uint32_t Offset;
    uint32_t Position;
    uint32_t ColorControl;
    uint32_t AuxDistance;
} HV_GPU_BOOT_PLANE_SNAPSHOT;

static EFI_GUID gHvGpuPciIoGuid = {
    UINT32_C(0x4cf5b200), UINT16_C(0x68b8), UINT16_C(0x4ca5),
    { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a }
};
static EFI_GUID gHvGpuDevicePathGuid = {
    UINT32_C(0x09576e91), UINT16_C(0x6d3f), UINT16_C(0x11d2),
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
static EFI_GUID gHvGpuGopGuid = {
    UINT32_C(0x9042a9de), UINT16_C(0x23dc), UINT16_C(0x4a38),
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};

#define HV_GPU_BOOT_PAGE_SIZE UINT64_C(4096)
#define HV_GPU_BOOT_LOW_LIMIT UINT64_C(0x40000000)
#define HV_GPU_BOOT_MIN_FRAMEBUFFER UINT64_C(0x00100000)
#define HV_GPU_BOOT_GGTT_APERTURE_OFFSET UINT64_C(0x00800000)
#define HV_GPU_BOOT_GGTT_APERTURE_MIN_BYTES UINT64_C(0x00800000)
#define HV_GPU_BOOT_MAX_POLL_COUNT UINT32_C(500)
#define HV_GPU_BOOT_POLL_US ((UINTN)1000)
#define HV_GPU_BOOT_PIPECONF_A UINT32_C(0x70008)
#define HV_GPU_BOOT_PIPESRC_A UINT32_C(0x6001c)
#define HV_GPU_BOOT_PIPE_STRIDE UINT32_C(0x1000)
#define HV_GPU_BOOT_PIPE_ENABLE UINT32_C(0x80000000)
#define HV_GPU_BOOT_PIPESRC_DIMENSION_MASK UINT32_C(0x0000ffff)
#define HV_GPU_BOOT_DISPLAY_ECAM_OFFSET UINT64_C(0x00010000)
#define HV_GPU_BOOT_VGACNTRL UINT32_C(0x41000)
#define HV_GPU_BOOT_VGA_DISABLE UINT32_C(0x80000000)
#define HV_GPU_BOOT_PLANE1_A UINT32_C(0x70180)
#define HV_GPU_BOOT_PLANE2_A UINT32_C(0x70280)
#define HV_GPU_BOOT_PLANE3_A UINT32_C(0x70380)
#define HV_GPU_BOOT_CURSOR_A UINT32_C(0x70080)
#define HV_GPU_BOOT_PLANE_STRIDE_OFFSET UINT32_C(0x08)
#define HV_GPU_BOOT_PLANE_POSITION_OFFSET UINT32_C(0x0c)
#define HV_GPU_BOOT_PLANE_SIZE_OFFSET UINT32_C(0x10)
#define HV_GPU_BOOT_PLANE_SURFACE_OFFSET UINT32_C(0x1c)
#define HV_GPU_BOOT_PLANE_LIVE_OFFSET UINT32_C(0x2c)
#define HV_GPU_BOOT_PLANE_AUX_DISTANCE_OFFSET UINT32_C(0x40)
#define HV_GPU_BOOT_PLANE_COLOR_OFFSET UINT32_C(0x4c)
#define HV_GPU_BOOT_PLANE_ENABLE UINT32_C(0x80000000)
#define HV_GPU_BOOT_PCI_VENDOR_OFFSET UINT32_C(0x00)
#define HV_GPU_BOOT_PCI_COMMAND_OFFSET UINT32_C(0x04)
#define HV_GPU_BOOT_PCI_BAR0_OFFSET UINT32_C(0x10)
#define HV_GPU_BOOT_PCI_BAR2_OFFSET UINT32_C(0x18)
#define HV_GPU_BOOT_PCI_CLASS_OFFSET UINT32_C(0x0b)
#define HV_GPU_BOOT_PCI_COMMAND_IO UINT16_C(0x0001)
#define HV_GPU_BOOT_PCI_COMMAND_MEMORY UINT16_C(0x0002)
#define HV_GPU_BOOT_PCI_COMMAND_BUSMASTER UINT16_C(0x0004)
#define HV_GPU_BOOT_PCI_COMMAND_INTERRUPT_DISABLE UINT16_C(0x0400)
#define HV_GPU_BOOT_VTD_GSTS_OFFSET UINT64_C(0x01c)
#define HV_GPU_BOOT_VTD_RTADDR_OFFSET UINT64_C(0x020)
#define HV_GPU_BOOT_VTD_GSTS_RTPS UINT32_C(0x40000000)
#define HV_GPU_BOOT_VTD_GSTS_TES UINT32_C(0x80000000)
#define HV_GPU_BOOT_CLASS_DISPLAY UINT8_C(0x03)
#define HV_GPU_BOOT_HEADER_NORMAL UINT8_C(0x00)
#define HV_GPU_BOOT_GGC_OFFSET UINT32_C(0x50)
#define HV_GPU_BOOT_BDSM_OFFSET UINT32_C(0xb0)
#define HV_GPU_BOOT_BGSM_OFFSET UINT32_C(0xb4)
#define HV_GPU_BOOT_STOLEN_LOCK UINT32_C(0x00000001)
#define HV_GPU_BOOT_STOLEN_BASE_MASK UINT32_C(0xfff00000)
#define HV_GPU_BOOT_GGMS_SHIFT UINT32_C(6)
#define HV_GPU_BOOT_GGMS_MASK UINT32_C(0x000000c0)
#define HV_GPU_BOOT_ACPI_QWORD_DESCRIPTOR UINT8_C(0x8a)
#define HV_GPU_BOOT_ACPI_END_TAG UINT8_C(0x79)
#define HV_GPU_BOOT_ACPI_ADDRESS_MEMORY UINT8_C(0x00)
#define HV_GPU_BOOT_GOP_PIXEL_BGR_RESERVED UINT32_C(1)
#define HV_GPU_BOOT_NVIDIA_VENDOR UINT16_C(0x10de)

/* Boot-only variables. They never enter .hvscan and are not referenced after
 * the guest is launched. Keeping firmware protocol handles here lets the
 * pre-launch failure path reconnect the GOP before releasing its resources. */
static HV_GPU_NATIVE_STATE *gHvGpuBootState;
static EFI_BOOT_SERVICES *gHvGpuBootServices;
static EFI_HANDLE gHvGpuIntelController;
static EFI_HANDLE gHvGpuIntelGopHandle;
static HV_EFI_PCI_IO_PROTOCOL *gHvGpuIntelPciIo;
static uint32_t gHvGpuBootGopDisconnected;
static uint32_t gHvGpuBootArmStarted;
static uint32_t gHvGpuBootPteWritten;
static uint16_t gHvGpuOriginalPciCommand;
static uint32_t gHvGpuPciCommandCaptured;
static uint32_t gHvGpuPciBmeQuiesced;
static uint32_t gHvGpuPciCommandNeedsRestore;
static HV_GPU_BOOT_PLANE_SNAPSHOT gHvGpuPreviousPlane1;
static uint32_t gHvGpuPreviousVgaControl;
static uint32_t gHvGpuPreviousPlane2Control;
static uint32_t gHvGpuPreviousPlane2Surface;
static uint32_t gHvGpuPreviousPlane3Control;
static uint32_t gHvGpuPreviousPlane3Surface;
static uint32_t gHvGpuPreviousCursorControl;
static uint32_t gHvGpuArmSnapshotValid;

static void HvGpuBootZero(void *buffer, size_t bytes)
{
    volatile uint8_t *cursor = (volatile uint8_t *)buffer;
    while (bytes-- != 0) *cursor++ = 0;
}

static void HvGpuBootFill(void *buffer, uint8_t value, size_t bytes)
{
    volatile uint8_t *cursor = (volatile uint8_t *)buffer;
    while (bytes-- != 0) *cursor++ = value;
}

static uint16_t HvGpuBootReadLe16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t HvGpuBootReadLe32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t HvGpuBootReadLe64(const uint8_t *bytes)
{
    return (uint64_t)HvGpuBootReadLe32(bytes) |
        ((uint64_t)HvGpuBootReadLe32(bytes + 4) << 32);
}

static uint32_t HvGpuBootAdd(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (sum == NULL || left > UINT64_MAX - right) return 0;
    *sum = left + right;
    return 1;
}

static uint32_t HvGpuBootAlignUp(uint64_t value, uint64_t alignment,
                                 uint64_t *aligned)
{
    uint64_t mask;
    if (aligned == NULL || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) return 0;
    mask = alignment - 1;
    if (value > UINT64_MAX - mask) return 0;
    *aligned = (value + mask) & ~mask;
    return 1;
}

static EFI_STATUS HvGpuBootAllocate(UINTN pages,
                                    EFI_PHYSICAL_ADDRESS *physical)
{
    EFI_PHYSICAL_ADDRESS maximum = HV_GPU_BOOT_LOW_LIMIT - 1;
    EFI_STATUS status;
    if (gHvGpuBootServices == NULL || physical == NULL || pages == 0 ||
        gHvGpuBootServices->AllocatePages == NULL ||
        gHvGpuBootServices->FreePages == NULL) return EFI_INVALID_PARAMETER;
    status = gHvGpuBootServices->AllocatePages(
        AllocateMaxAddress, EfiRuntimeServicesData, pages, &maximum);
    if (EFI_ERROR(status)) return status;
    if (maximum < HV_GPU_BOOT_MIN_FRAMEBUFFER ||
        (maximum & (HV_GPU_BOOT_PAGE_SIZE - 1)) != 0 ||
        maximum >= HV_GPU_BOOT_LOW_LIMIT ||
        (uint64_t)pages * HV_GPU_BOOT_PAGE_SIZE >
            HV_GPU_BOOT_LOW_LIMIT - maximum) {
        if (gHvGpuBootServices->FreePages != NULL)
            (void)gHvGpuBootServices->FreePages(maximum, pages);
        return EFI_UNSUPPORTED;
    }
    *physical = maximum;
    return EFI_SUCCESS;
}

static uint32_t HvGpuBootReadPciConfig(HV_EFI_PCI_IO_PROTOCOL *pci,
                                      uint32_t offset, UINTN count,
                                      VOID *buffer)
{
    if (pci == NULL || pci->Pci.Read == NULL || buffer == NULL ||
        count == 0 || offset > UINT32_C(0x100) ||
        count > UINT32_C(0x100) - offset) return 0;
    return pci->Pci.Read(pci, HvEfiPciIoWidthUint8, offset, count, buffer) ==
        EFI_SUCCESS;
}

static uint32_t HvGpuBootGetBdf(HV_EFI_PCI_IO_PROTOCOL *pci,
                                HV_GPU_BOOT_BDF *bdf)
{
    if (pci == NULL || pci->GetLocation == NULL || bdf == NULL) return 0;
    return pci->GetLocation(pci, &bdf->Segment, &bdf->Bus, &bdf->Device,
                            &bdf->Function) == EFI_SUCCESS;
}

static uint32_t HvGpuBootBdfEqual(const HV_GPU_BOOT_BDF *left,
                                 const HV_GPU_BOOT_BDF *right)
{
    return left != NULL && right != NULL &&
        left->Segment == right->Segment && left->Bus == right->Bus &&
        left->Device == right->Device && left->Function == right->Function;
}

static uint32_t HvGpuBootPciFromDevicePath(
    EFI_DEVICE_PATH_PROTOCOL *devicePath,
    EFI_HANDLE *controller,
    HV_EFI_PCI_IO_PROTOCOL **pci)
{
    EFI_DEVICE_PATH_PROTOCOL *remaining = devicePath;
    EFI_HANDLE handle = NULL;
    HV_EFI_PCI_IO_PROTOCOL *interface = NULL;
    EFI_STATUS status;
    if (devicePath == NULL || controller == NULL || pci == NULL ||
        gHvGpuBootServices == NULL ||
        gHvGpuBootServices->LocateDevicePath == NULL ||
        gHvGpuBootServices->HandleProtocol == NULL) return 0;
    status = gHvGpuBootServices->LocateDevicePath(
        &gHvGpuPciIoGuid, &remaining, &handle);
    if (EFI_ERROR(status) || handle == NULL) return 0;
    status = gHvGpuBootServices->HandleProtocol(
        handle, &gHvGpuPciIoGuid, (VOID **)&interface);
    if (EFI_ERROR(status) || interface == NULL) return 0;
    *controller = handle;
    *pci = interface;
    return 1;
}

static uint32_t HvGpuBootPciFromHandle(
    EFI_HANDLE handle,
    EFI_HANDLE *controller,
    HV_EFI_PCI_IO_PROTOCOL **pci)
{
    EFI_DEVICE_PATH_PROTOCOL *devicePath = NULL;
    EFI_STATUS status;
    if (handle == NULL || controller == NULL || pci == NULL ||
        gHvGpuBootServices == NULL ||
        gHvGpuBootServices->HandleProtocol == NULL) return 0;
    status = gHvGpuBootServices->HandleProtocol(
        handle, &gHvGpuDevicePathGuid, (VOID **)&devicePath);
    if (EFI_ERROR(status) || devicePath == NULL) return 0;
    return HvGpuBootPciFromDevicePath(devicePath, controller, pci);
}

static uint32_t HvGpuBootReadPciIdentity(HV_EFI_PCI_IO_PROTOCOL *pci,
                                        uint16_t *vendor,
                                        uint16_t *device)
{
    uint8_t bytes[4];
    if (!HvGpuBootReadPciConfig(pci, HV_GPU_BOOT_PCI_VENDOR_OFFSET,
                                sizeof(bytes), bytes)) return 0;
    *vendor = HvGpuBootReadLe16(bytes);
    *device = HvGpuBootReadLe16(bytes + 2);
    return 1;
}

static uint32_t HvGpuBootReadBar32(HV_EFI_PCI_IO_PROTOCOL *pci,
                                   UINT8 bar,
                                   uint64_t offset,
                                   uint32_t *value)
{
    if (pci == NULL || pci->Mem.Read == NULL || value == NULL ||
        offset > UINT64_MAX - 4) return 0;
    return pci->Mem.Read(pci, HvEfiPciIoWidthUint32, bar, offset, 1,
                         value) == EFI_SUCCESS;
}

static uint32_t HvGpuBootWriteBar32(HV_EFI_PCI_IO_PROTOCOL *pci,
                                    UINT8 bar,
                                    uint64_t offset,
                                    uint32_t value)
{
    if (pci == NULL || pci->Mem.Write == NULL || offset > UINT64_MAX - 4)
        return 0;
    return pci->Mem.Write(pci, HvEfiPciIoWidthUint32, bar, offset, 1,
                          &value) == EFI_SUCCESS;
}

static uint32_t HvGpuBootReadBar64(HV_EFI_PCI_IO_PROTOCOL *pci,
                                   UINT8 bar,
                                   uint64_t offset,
                                   uint64_t *value)
{
    if (pci == NULL || pci->Mem.Read == NULL || value == NULL ||
        offset > UINT64_MAX - 8) return 0;
    return pci->Mem.Read(pci, HvEfiPciIoWidthUint64, bar, offset, 1,
                         value) == EFI_SUCCESS;
}

static uint32_t HvGpuBootWriteBar64(HV_EFI_PCI_IO_PROTOCOL *pci,
                                    UINT8 bar,
                                    uint64_t offset,
                                    uint64_t value)
{
    if (pci == NULL || pci->Mem.Write == NULL || offset > UINT64_MAX - 8)
        return 0;
    return pci->Mem.Write(pci, HvEfiPciIoWidthUint64, bar, offset, 1,
                          &value) == EFI_SUCCESS;
}

static uint32_t HvGpuBootParseBarResource(HV_EFI_PCI_IO_PROTOCOL *pci,
                                          uint8_t barIndex,
                                          uint64_t rawBase,
                                          HV_GPU_BOOT_BAR *bar)
{
    VOID *resources = NULL;
    const uint8_t *cursor;
    uint32_t descriptorCount = 0;
    uint32_t found = 0;
    uint64_t base = 0;
    uint64_t bytes = 0;
    EFI_STATUS status;
    if (pci == NULL || pci->GetBarAttributes == NULL || bar == NULL)
        return 0;
    status = pci->GetBarAttributes(pci, barIndex, NULL, &resources);
    if (EFI_ERROR(status) || resources == NULL) return 0;
    cursor = (const uint8_t *)resources;
    while (descriptorCount++ < 8u) {
        uint8_t descriptor = cursor[0];
        if (descriptor == HV_GPU_BOOT_ACPI_END_TAG) break;
        if (descriptor != HV_GPU_BOOT_ACPI_QWORD_DESCRIPTOR ||
            HvGpuBootReadLe16(cursor + 1) !=
                (uint16_t)(sizeof(HV_EFI_ACPI_QWORD_RESOURCE) - 3u))
            break;
        if (cursor[3] != HV_GPU_BOOT_ACPI_ADDRESS_MEMORY) break;
        {
            const uint64_t granularity = HvGpuBootReadLe64(cursor + 6);
            const uint64_t minimum = HvGpuBootReadLe64(cursor + 14);
            const uint64_t maximum = HvGpuBootReadLe64(cursor + 22);
            const uint64_t translation = HvGpuBootReadLe64(cursor + 30);
            const uint64_t length = HvGpuBootReadLe64(cursor + 38);
            uint64_t translated;
            uint64_t end;
            if ((granularity != 32 && granularity != 64) || length == 0 ||
                translation != 0 ||
                !HvGpuBootAdd(minimum, translation, &translated) ||
                !HvGpuBootAdd(translated, length, &end) ||
                end > HV_GPU_GUEST_FILTER_PHYS_LIMIT ||
                (translated & (HV_GPU_BOOT_PAGE_SIZE - 1)) != 0 ||
                (length & (HV_GPU_BOOT_PAGE_SIZE - 1)) != 0 ||
                minimum > maximum || end - 1 > maximum ||
                (rawBase != minimum && rawBase != translated)) break;
            base = translated;
            bytes = length;
            ++found;
        }
        cursor += sizeof(HV_EFI_ACPI_QWORD_RESOURCE);
    }
    (void)gHvGpuBootServices->FreePool(resources);
    if (cursor[0] != HV_GPU_BOOT_ACPI_END_TAG || found != 1) return 0;
    bar->Base = base;
    bar->Bytes = bytes;
    return 1;
}

static uint32_t HvGpuBootReadBarBase(const uint8_t *config,
                                    uint32_t offset,
                                    uint64_t *base)
{
    uint32_t low;
    uint32_t type;
    uint64_t value;
    if (config == NULL || base == NULL) return 0;
    low = HvGpuBootReadLe32(config + offset);
    if (low == 0 || low == UINT32_MAX || (low & 1u) != 0) return 0;
    type = (low >> 1) & 3u;
    if (type == 0) value = (uint64_t)(low & UINT32_C(0xfffffff0));
    else if (type == 2) {
        value = ((uint64_t)HvGpuBootReadLe32(config + offset + 4) << 32) |
            (uint64_t)(low & UINT32_C(0xfffffff0));
    } else return 0;
    if (value == 0 || (value & (HV_GPU_BOOT_PAGE_SIZE - 1)) != 0 ||
        value >= HV_GPU_GUEST_FILTER_PHYS_LIMIT) return 0;
    *base = value;
    return 1;
}

static uint32_t HvGpuBootFindConsoleNvidia(
    EFI_SYSTEM_TABLE *systemTable,
    const HV_GPU_BOOT_BDF *nvidiaGopBdfs,
    uint32_t nvidiaGopCount)
{
    EFI_HANDLE controller = NULL;
    HV_EFI_PCI_IO_PROTOCOL *pci = NULL;
    HV_GPU_BOOT_BDF consoleBdf;
    uint16_t vendor;
    uint16_t device;
    uint32_t index;
    if (systemTable == NULL || systemTable->ConsoleOutHandle == NULL ||
        !HvGpuBootPciFromHandle(systemTable->ConsoleOutHandle,
                                &controller, &pci) ||
        !HvGpuBootGetBdf(pci, &consoleBdf) ||
        !HvGpuBootReadPciIdentity(pci, &vendor, &device) ||
        vendor != HV_GPU_BOOT_NVIDIA_VENDOR) return 0;
    (void)controller;
    (void)device;
    for (index = 0; index < nvidiaGopCount; ++index) {
        if (HvGpuBootBdfEqual(&consoleBdf, &nvidiaGopBdfs[index])) return 1;
    }
    return 0;
}

static uint32_t HvGpuBootReadGopMode(HV_EFI_GOP_PROTOCOL *gop,
                                    uint32_t *width,
                                    uint32_t *height)
{
    HV_EFI_GOP_MODE_INFO *info;
    uint64_t minimumBuffer;
    if (gop == NULL || gop->Mode == NULL || gop->Mode->Info == NULL ||
        width == NULL || height == NULL ||
        gop->Mode->SizeOfInfo < sizeof(HV_EFI_GOP_MODE_INFO)) return 0;
    info = gop->Mode->Info;
    if (info->PixelFormat != HV_GPU_BOOT_GOP_PIXEL_BGR_RESERVED ||
        info->HorizontalResolution < 2 || info->VerticalResolution < 2 ||
        info->HorizontalResolution > UINT32_C(65536) ||
        info->VerticalResolution > UINT32_C(65536) ||
        info->PixelsPerScanLine < info->HorizontalResolution ||
        gop->Mode->FrameBufferBase == 0) return 0;
    if ((uint64_t)info->PixelsPerScanLine * info->VerticalResolution * 4u >
        gop->Mode->FrameBufferSize) return 0;
    minimumBuffer = (uint64_t)info->HorizontalResolution *
        info->VerticalResolution * 4u;
    if (minimumBuffer > HV_GPU_MAX_BYTES) return 0;
    *width = info->HorizontalResolution;
    *height = info->VerticalResolution;
    return 1;
}

static uint32_t HvGpuBootFindPipes(HV_EFI_PCI_IO_PROTOCOL *pci,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t *pipeOut)
{
    uint32_t pipe;
    uint32_t activeCount = 0;
    uint32_t matchingPipe = UINT32_MAX;
    for (pipe = 0; pipe < 4; ++pipe) {
        uint32_t pipeConf;
        uint32_t pipeSrc;
        uint32_t currentWidth;
        uint32_t currentHeight;
        uint32_t planeControl;
        uint32_t planeSize;
        uint32_t planeSurface;
        uint32_t planeLive;
        if (!HvGpuBootReadBar32(pci, 0,
                HV_GPU_BOOT_PIPECONF_A + (uint64_t)pipe *
                    HV_GPU_BOOT_PIPE_STRIDE, &pipeConf) ||
            !HvGpuBootReadBar32(pci, 0,
                HV_GPU_BOOT_PIPESRC_A + (uint64_t)pipe *
                    HV_GPU_BOOT_PIPE_STRIDE, &pipeSrc)) return 0;
        if ((pipeConf & HV_GPU_BOOT_PIPE_ENABLE) == 0) continue;
        ++activeCount;
        currentWidth = ((pipeSrc >> 16) &
                        HV_GPU_BOOT_PIPESRC_DIMENSION_MASK) + 1u;
        currentHeight = (pipeSrc & HV_GPU_BOOT_PIPESRC_DIMENSION_MASK) + 1u;
        if (currentWidth == width && currentHeight == height) {
            const uint64_t plane = (uint64_t)HV_GPU_BOOT_PLANE1_A +
                (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE;
            if (!HvGpuBootReadBar32(pci, 0, plane, &planeControl) ||
                !HvGpuBootReadBar32(pci, 0,
                    plane + HV_GPU_BOOT_PLANE_SIZE_OFFSET, &planeSize) ||
                !HvGpuBootReadBar32(pci, 0,
                    plane + HV_GPU_BOOT_PLANE_SURFACE_OFFSET, &planeSurface) ||
                !HvGpuBootReadBar32(pci, 0,
                    plane + HV_GPU_BOOT_PLANE_LIVE_OFFSET, &planeLive) ||
                (planeControl & HV_GPU_BOOT_PLANE_ENABLE) == 0 ||
                planeSize != (((height - 1u) << 16) | (width - 1u)) ||
                planeSurface != planeLive) return 0;
            matchingPipe = pipe;
        }
    }
    if (activeCount != 1 || matchingPipe == UINT32_MAX) return 0;
    *pipeOut = matchingPipe;
    return 1;
}

/* Gen12 exposes seven universal planes per pipe. This boot wrapper changes
 * plane 1 only, so it accepts the strict profile where planes 2-7 and the
 * cursor are already disabled. That prevents another fetch path from
 * addressing memory outside the root-owned framebuffer DMA domain. */
static uint32_t HvGpuBootOtherPlanesDisabled(HV_EFI_PCI_IO_PROTOCOL *pci,
                                             uint32_t pipe)
{
    uint32_t plane;
    uint32_t control;
    if (pci == NULL || pipe > 3) return 0;
    for (plane = 2; plane <= 7; ++plane) {
        const uint64_t offset = UINT64_C(0x70180) +
            (uint64_t)(plane - 1u) * UINT64_C(0x100) +
            (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE;
        if (!HvGpuBootReadBar32(pci, 0, offset, &control) ||
            (control & HV_GPU_BOOT_PLANE_ENABLE) != 0) return 0;
    }
    if (!HvGpuBootReadBar32(pci, 0,
            HV_GPU_BOOT_CURSOR_A + (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE,
            &control) || (control & HV_GPU_BOOT_PLANE_ENABLE) != 0) return 0;
    return 1;
}

static uint32_t HvGpuBootReadHostBridge(uint64_t ecamBase,
                                       uint32_t offset,
                                       uint32_t *value)
{
    uint64_t address;
    uint64_t end;
    if (value == NULL || (offset & 3u) != 0 ||
        !HvGpuBootAdd(ecamBase, offset, &address) ||
        !HvGpuBootAdd(address, 4, &end) ||
        end > HV_GPU_GUEST_FILTER_PHYS_LIMIT) return 0;
    *value = *(volatile uint32_t *)(uintptr_t)address;
    return 1;
}

static uint32_t HvGpuBootGgmsBytes(uint32_t ggc, uint64_t *bytes)
{
    const uint32_t code = (ggc & HV_GPU_BOOT_GGMS_MASK) >>
        HV_GPU_BOOT_GGMS_SHIFT;
    if (bytes == NULL || code == 0) return 0;
    *bytes = UINT64_C(0x00200000) << (code - 1u);
    return 1;
}

static uint32_t HvGpuBootReadStolenGgtt(uint64_t ecamBase,
                                       uint32_t *ggcOut,
                                       uint64_t *baseOut,
                                       uint64_t *bytesOut)
{
    uint32_t ggc;
    uint32_t bdsm;
    uint32_t bgsm;
    uint64_t bytes;
    uint64_t dataBase;
    uint64_t gttBase;
    if (!HvGpuBootReadHostBridge(ecamBase, HV_GPU_BOOT_GGC_OFFSET, &ggc) ||
        !HvGpuBootReadHostBridge(ecamBase, HV_GPU_BOOT_BDSM_OFFSET, &bdsm) ||
        !HvGpuBootReadHostBridge(ecamBase, HV_GPU_BOOT_BGSM_OFFSET, &bgsm) ||
        (ggc & HV_GPU_BOOT_STOLEN_LOCK) == 0 ||
        (bdsm & HV_GPU_BOOT_STOLEN_LOCK) == 0 ||
        (bgsm & HV_GPU_BOOT_STOLEN_LOCK) == 0 ||
        !HvGpuBootGgmsBytes(ggc, &bytes)) return 0;
    dataBase = (uint64_t)(bdsm & HV_GPU_BOOT_STOLEN_BASE_MASK);
    gttBase = (uint64_t)(bgsm & HV_GPU_BOOT_STOLEN_BASE_MASK);
    if (dataBase <= gttBase || dataBase - gttBase != bytes ||
        gttBase < UINT64_C(0x40000000) ||
        dataBase > HV_GPU_GUEST_FILTER_PHYS_LIMIT) return 0;
    *ggcOut = ggc;
    *baseOut = gttBase;
    *bytesOut = bytes;
    return 1;
}

static void HvGpuBootInitializeEntities(HV_GPU_NATIVE_STATE *state)
{
    HV_OVERLAY_ENTITY *entity;
    uint32_t label[] = { 'R', 'O', 'O', 'T', ' ', 'G', 'P', 'U' };
    uint32_t index;
    HvGpuBootZero(&state->Entities, sizeof(state->Entities));
    state->Entities.Count = 1;
    entity = &state->Entities.Entities[0];
    entity->Coordinates = HV_OVERLAY_SCREEN;
    entity->X = state->Allocation.Width > 16 ? 16 : 0;
    entity->Y = state->Allocation.Height > 24 ? 24 : 0;
    entity->Width = state->Allocation.Width - entity->X < 160 ?
        state->Allocation.Width - entity->X : 160;
    entity->Height = state->Allocation.Height - entity->Y < 80 ?
        state->Allocation.Height - entity->Y : 80;
    entity->Color = UINT32_C(0xffff0000);
    entity->FillColor = UINT32_C(0x50000000);
    entity->LabelLength = (uint32_t)(sizeof(label) / sizeof(label[0]));
    for (index = 0; index < entity->LabelLength; ++index)
        entity->Label[index] = (uint8_t)label[index];
}

static uint32_t HvGpuBootAllocateStatePages(HV_GPU_NATIVE_STATE **stateOut,
                                            EFI_PHYSICAL_ADDRESS *physicalOut)
{
    EFI_PHYSICAL_ADDRESS physical = 0;
    uint64_t pageCount64;
    EFI_STATUS status;
    HV_GPU_NATIVE_STATE *state;
    if (stateOut == NULL || physicalOut == NULL ||
        !HvGpuBootAlignUp(sizeof(HV_GPU_NATIVE_STATE),
                          HV_GPU_BOOT_PAGE_SIZE, &pageCount64)) return 0;
    pageCount64 /= HV_GPU_BOOT_PAGE_SIZE;
    if (pageCount64 == 0 || pageCount64 > UINT32_MAX) return 0;
    status = HvGpuBootAllocate((UINTN)pageCount64, &physical);
    if (EFI_ERROR(status)) return 0;
    state = (HV_GPU_NATIVE_STATE *)(uintptr_t)physical;
    HvGpuBootZero(state, (size_t)(pageCount64 * HV_GPU_BOOT_PAGE_SIZE));
    state->Marker = HV_GPU_NATIVE_MARKER;
    state->SelfPhysical = physical;
    state->SelfPages = (uint32_t)pageCount64;
    *stateOut = state;
    *physicalOut = physical;
    return 1;
}

static uint32_t HvGpuBootAllocatePagesInto(uint32_t pages,
                                           uint64_t *physicalOut)
{
    EFI_PHYSICAL_ADDRESS physical = 0;
    EFI_STATUS status;
    if (physicalOut == NULL || pages == 0) return 0;
    status = HvGpuBootAllocate((UINTN)pages, &physical);
    if (EFI_ERROR(status)) return 0;
    HvGpuBootZero((void *)(uintptr_t)physical,
                  (size_t)((uint64_t)pages * HV_GPU_BOOT_PAGE_SIZE));
    *physicalOut = physical;
    return 1;
}

static uint32_t HvGpuBootDiscover(EFI_SYSTEM_TABLE *systemTable,
                                 uint64_t ecamBase,
                                 uint64_t ecamBytes,
                                 HV_GPU_NATIVE_STATE *state)
{
    EFI_HANDLE *gopHandles = NULL;
    UINTN gopCount = 0;
    UINTN index;
    HV_GPU_BOOT_BDF nvidiaBdfs[8];
    uint32_t nvidiaCount = 0;
    uint32_t intelCount = 0;
    HV_EFI_GOP_PROTOCOL *intelGop = NULL;
    uint8_t config[256];
    HV_GPU_ADAPTER adapter;
    HV_GPU_BOOT_BAR bar0;
    HV_GPU_BOOT_BAR bar2;
    uint64_t bar0Raw;
    uint64_t bar2Raw;
    uint16_t command;
    uint16_t vendor;
    uint16_t device;
    uint32_t classCode;
    uint32_t headerType;
    uint32_t ggc;
    uint64_t ggttPhysical;
    uint64_t ggttBytes;
    uint32_t hostBridgeIdentity;
    uint8_t hostBridgeClass;
    uint32_t width;
    uint32_t height;
    uint32_t pipe;
    uint64_t stride64;
    uint64_t declaredBytes;
    uint64_t pinnedBytes;
    uint64_t entryCount;
    uint64_t pageCount;
    uint64_t ggttBase;
    EFI_STATUS status;

    if (systemTable == NULL || systemTable->BootServices == NULL ||
        state == NULL || ecamBase == 0 ||
        ecamBytes < HV_GPU_BOOT_DISPLAY_ECAM_OFFSET + UINT64_C(0x100) ||
        ecamBase >= HV_GPU_GUEST_FILTER_PHYS_LIMIT ||
        ecamBytes > HV_GPU_GUEST_FILTER_PHYS_LIMIT - ecamBase ||
        systemTable->BootServices->LocateHandleBuffer == NULL ||
        systemTable->BootServices->HandleProtocol == NULL ||
        systemTable->BootServices->FreePool == NULL) return 0;
    hostBridgeIdentity = *(volatile uint32_t *)(uintptr_t)ecamBase;
    hostBridgeClass = *(volatile uint8_t *)(uintptr_t)(ecamBase + 0x0bu);
    if ((hostBridgeIdentity & UINT32_C(0xffff)) != UINT32_C(0x8086) ||
        hostBridgeClass != UINT8_C(0x06)) return 0;
    HvGpuBootZero(nvidiaBdfs, sizeof(nvidiaBdfs));
    status = systemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &gHvGpuGopGuid, NULL, &gopCount, &gopHandles);
    if (EFI_ERROR(status) || gopHandles == NULL || gopCount == 0) return 0;

    for (index = 0; index < gopCount; ++index) {
        EFI_HANDLE controller = NULL;
        HV_EFI_PCI_IO_PROTOCOL *pci = NULL;
        HV_EFI_GOP_PROTOCOL *gop = NULL;
        HV_GPU_BOOT_BDF bdf;
        uint16_t deviceVendor;
        uint16_t deviceId;
        if (EFI_ERROR(systemTable->BootServices->HandleProtocol(
                gopHandles[index], &gHvGpuGopGuid, (VOID **)&gop)) ||
            gop == NULL || !HvGpuBootPciFromHandle(
                gopHandles[index], &controller, &pci) ||
            !HvGpuBootGetBdf(pci, &bdf) ||
            !HvGpuBootReadPciIdentity(pci, &deviceVendor, &deviceId)) continue;
        if (deviceVendor == UINT16_C(0x8086) && bdf.Segment == 0 &&
            bdf.Bus == 0 && bdf.Device == 2 && bdf.Function == 0) {
            ++intelCount;
            intelGop = gop;
            gHvGpuIntelController = controller;
            gHvGpuIntelGopHandle = gopHandles[index];
            gHvGpuIntelPciIo = pci;
            (void)deviceId;
        } else if (deviceVendor == HV_GPU_BOOT_NVIDIA_VENDOR &&
                   nvidiaCount < sizeof(nvidiaBdfs) / sizeof(nvidiaBdfs[0])) {
            nvidiaBdfs[nvidiaCount++] = bdf;
        }
    }
    (void)systemTable->BootServices->FreePool(gopHandles);
    if (intelCount != 1 || intelGop == NULL || gHvGpuIntelPciIo == NULL ||
        !HvGpuBootFindConsoleNvidia(systemTable, nvidiaBdfs, nvidiaCount) ||
        !HvGpuBootReadGopMode(intelGop, &width, &height) ||
        !HvGpuBootFindPipes(gHvGpuIntelPciIo, width, height, &pipe) ||
        !HvGpuBootOtherPlanesDisabled(gHvGpuIntelPciIo, pipe)) return 0;

    if (!HvGpuBootReadPciConfig(gHvGpuIntelPciIo, 0, sizeof(config), config) ||
        HvGpuIntelParsePci(config, sizeof(config), &adapter) != HV_GPU_OK)
        return 0;
    vendor = HvGpuBootReadLe16(config + HV_GPU_BOOT_PCI_VENDOR_OFFSET);
    device = HvGpuBootReadLe16(config + 2);
    command = HvGpuBootReadLe16(config + HV_GPU_BOOT_PCI_COMMAND_OFFSET);
    classCode = config[HV_GPU_BOOT_PCI_CLASS_OFFSET];
    headerType = config[0x0e] & 0x7fu;
    if (vendor != UINT16_C(0x8086) ||
        device != HV_GPU_INTEL_UHD770_ID ||
        classCode != HV_GPU_BOOT_CLASS_DISPLAY ||
        headerType != HV_GPU_BOOT_HEADER_NORMAL ||
        (command & HV_GPU_BOOT_PCI_COMMAND_MEMORY) == 0 ||
        (command & HV_GPU_BOOT_PCI_COMMAND_IO) != 0 ||
        !HvGpuBootReadBarBase(config, HV_GPU_BOOT_PCI_BAR0_OFFSET, &bar0Raw) ||
        !HvGpuBootReadBarBase(config, HV_GPU_BOOT_PCI_BAR2_OFFSET, &bar2Raw) ||
        !HvGpuBootParseBarResource(gHvGpuIntelPciIo, 0, bar0Raw, &bar0) ||
        !HvGpuBootParseBarResource(gHvGpuIntelPciIo, 2, bar2Raw, &bar2) ||
        bar0.Base != adapter.MmioPhysical ||
        bar0.Bytes < HV_GPU_BOOT_GGTT_APERTURE_OFFSET +
                         HV_GPU_BOOT_GGTT_APERTURE_MIN_BYTES ||
        !HvGpuBootReadStolenGgtt(ecamBase, &ggc, &ggttPhysical,
                                 &ggttBytes) ||
        bar0.Bytes < HV_GPU_BOOT_GGTT_APERTURE_OFFSET + ggttBytes) return 0;

    stride64 = ((uint64_t)width * 4u + UINT64_C(63)) & ~UINT64_C(63);
    if (stride64 == 0 || stride64 > UINT32_MAX ||
        stride64 > UINT64_MAX / height) return 0;
    declaredBytes = stride64 * height;
    if (declaredBytes == 0 || declaredBytes > HV_GPU_MAX_BYTES ||
        !HvGpuBootAlignUp(declaredBytes, HV_GPU_BOOT_PAGE_SIZE,
                          &pinnedBytes) || pinnedBytes > HV_GPU_MAX_BYTES)
        return 0;
    pageCount = pinnedBytes / HV_GPU_BOOT_PAGE_SIZE;
    entryCount = ggttBytes / 8;
    if (pageCount == 0 || pageCount > HV_GPU_MAX_PAGES ||
        pageCount >= entryCount ||
        (uint64_t)(entryCount - pageCount) * HV_GPU_BOOT_PAGE_SIZE >
            UINT32_MAX) return 0;
    ggttBase = (entryCount - pageCount) * HV_GPU_BOOT_PAGE_SIZE;

    state->Adapter = adapter;
    state->Adapter.MmioPhysical = bar0.Base;
    state->Filter.Size = (uint32_t)sizeof(state->Filter);
    state->Filter.Segment = 0;
    state->Filter.Bus = 0;
    state->Filter.Device = 2;
    state->Filter.Function = 0;
    state->Filter.EcamPage = ecamBase + HV_GPU_BOOT_DISPLAY_ECAM_OFFSET;
    state->Filter.Bar0.Base = bar0.Base;
    state->Filter.Bar0.Bytes = bar0.Bytes;
    state->Filter.Bar2.Base = bar2.Base;
    state->Filter.Bar2.Bytes = bar2.Bytes;
    if (!HvGpuGuestFilterValidatePolicy(&state->Filter)) return 0;
    state->Allocation.PhysicalBase = 0;
    state->Allocation.PinnedBytes = pinnedBytes;
    state->Allocation.GgttBase = (uint32_t)ggttBase;
    state->Allocation.Pipe = pipe;
    state->Allocation.Width = width;
    state->Allocation.Height = height;
    state->Allocation.StrideBytes = (uint32_t)stride64;
    state->GgttPhysical = ggttPhysical;
    state->GgttBytes = ggttBytes;
    state->FramebufferBytes = pinnedBytes;
    /* Set only after the explicit output-confirmation build acknowledgement
     * and successful Intel GOP plus active-pipe validation. Firmware has no
     * reliable generic way to prove that a connector has a physical sink. */
    state->PhysicalOutputVerified = 1;
    (void)ggc;
    return 1;
}

static uint32_t HvGpuBootReadCommand(uint16_t *command)
{
    uint8_t bytes[2];
    if (command == NULL || !HvGpuBootReadPciConfig(
            gHvGpuIntelPciIo, HV_GPU_BOOT_PCI_COMMAND_OFFSET,
            sizeof(bytes), bytes)) return 0;
    *command = HvGpuBootReadLe16(bytes);
    return 1;
}

static uint32_t HvGpuBootWriteCommand(uint16_t command)
{
    uint16_t value = command;
    return gHvGpuIntelPciIo != NULL &&
        gHvGpuIntelPciIo->Pci.Write != NULL &&
        gHvGpuIntelPciIo->Pci.Write(gHvGpuIntelPciIo,
            HvEfiPciIoWidthUint16, HV_GPU_BOOT_PCI_COMMAND_OFFSET, 1,
            &value) == EFI_SUCCESS;
}

/* Do not enable PCI bus mastering until both the actual VT-d hardware state
 * and the prepared guest CPU filters prove that the Intel function is fenced.
 * Table auditing alone is insufficient: the live RTADDR and GSTS registers
 * on every DRHD unit must agree with those audited tables. */
static uint32_t HvGpuBootProveIsolation(
    const HV_EFI_CONTEXT *context,
    const HV_GPU_NATIVE_STATE *state)
{
    uint32_t index;
    uint32_t installed = 0;
    if (context == NULL || state == NULL ||
        context->DmaIsolationMode != HV_EFI_DMA_ISOLATION_VTD_MEDIATED ||
        context->DmaIsolationState != HV_EFI_IOMMU_RUNTIME_ENABLED ||
        context->PhysicalIommuStatePhysical == 0 ||
        context->PhysicalIommuTablePhysical == 0 ||
        context->PhysicalIommuTablePhysical != state->LegacyTablesPhysical ||
        context->PhysicalIommuTablePages != state->LegacyTablePages ||
        state->UnitCount == 0 || state->UnitCount > HV_GPU_NATIVE_MAX_UNITS ||
        state->CpuCount == 0 ||
        state->CpuCount > HV_EFI_MAX_SUPPORTED_PROCESSORS ||
        state->CpuCount != context->EnabledProcessorCount ||
        state->OwnerCpu != context->BspProcessorNumber ||
        state->CurrentCpu != context->BspProcessorNumber ||
        state->LegacyTablesPhysical == 0 || state->LegacyTablePages == 0 ||
        state->DmaTablesPhysical == 0 ||
        (state->DmaTablesPhysical & (HV_GPU_BOOT_PAGE_SIZE - 1)) != 0 ||
        state->DmaPlan.Segment != 0 || state->DmaPlan.Bus != 0 ||
        state->DmaPlan.Device != 2 || state->DmaPlan.Function != 0 ||
        state->DmaTables.RootPhysical != state->DmaTablesPhysical ||
        state->DmaTables.Segment != 0 || state->DmaTables.Bus != 0 ||
        state->DmaTables.Device != 2 || state->DmaTables.Function != 0 ||
        state->DmaTables.DomainId != state->DmaPlan.DomainId ||
        state->DmaTables.LegacyDomainId != state->DmaPlan.LegacyDomainId ||
        state->DmaPlan.FramebufferBytes != state->FramebufferBytes ||
        state->DmaPlan.TranslationBytes != state->GgttBytes ||
        HvEfiIommuAuditRootDeviceDomain(
            (const uint64_t *)(uintptr_t)state->LegacyTablesPhysical,
            state->LegacyTablePages, state->LegacyTablesPhysical,
            &state->LegacyTables,
            (const uint64_t *)(uintptr_t)state->DmaTablesPhysical,
            HV_GPU_NATIVE_DMA_PAGES, state->DmaTablesPhysical,
            &state->DmaPlan, &state->DmaTables) !=
                HV_EFI_IOMMU_RUNTIME_OK) return 0;

    for (index = 0; index < HV_EFI_MAX_SUPPORTED_PROCESSORS; ++index) {
        const HV_GPU_NATIVE_CPU *cpu = &state->Cpus[index];
        const HV_EFI_CONTEXT *cpuContext;
        if (cpu->Installed == 0) continue;
        if (cpu->Installed != 1 || cpu->ContextPhysical == 0) return 0;
        cpuContext = (const HV_EFI_CONTEXT *)(uintptr_t)cpu->ContextPhysical;
        if (cpuContext->CurrentProcessorNumber != index ||
            cpuContext->GpuStatePhysical != state->SelfPhysical ||
            cpuContext->EptPhysical == 0 ||
            cpuContext->ShadowEptPhysical == 0 ||
            cpuContext->EptHierarchyReady == 0 ||
            (cpuContext->SecondaryControls &
                HV_EFI_VM_SECONDARY_ENABLE_EPT) == 0 ||
            (cpuContext->CpuControls &
                (HV_EFI_VM_CPU_USE_IO_BITMAPS |
                 HV_EFI_VM_CPU_USE_MSR_BITMAPS)) !=
                (HV_EFI_VM_CPU_USE_IO_BITMAPS |
                 HV_EFI_VM_CPU_USE_MSR_BITMAPS)) return 0;
        ++installed;
    }
    if (installed != state->CpuCount) return 0;

    for (index = 0; index < state->UnitCount; ++index) {
        const uint64_t base = state->UnitBases[index];
        volatile uint64_t *rootRegister;
        volatile uint32_t *statusRegister;
        uint64_t root;
        uint64_t capability;
        uint32_t status;
        if (base == 0 || (base & (HV_GPU_BOOT_PAGE_SIZE - 1)) != 0 ||
            base >= HV_GPU_GUEST_FILTER_PHYS_LIMIT ||
            HV_GPU_GUEST_FILTER_PHYS_LIMIT - base <
                HV_GPU_BOOT_VTD_RTADDR_OFFSET + sizeof(uint64_t)) return 0;
        rootRegister = (volatile uint64_t *)(uintptr_t)(base +
            HV_GPU_BOOT_VTD_RTADDR_OFFSET);
        statusRegister = (volatile uint32_t *)(uintptr_t)(base +
            HV_GPU_BOOT_VTD_GSTS_OFFSET);
        root = *rootRegister;
        capability = *(volatile uint64_t *)(uintptr_t)(base + 8);
        status = *statusRegister;
        if (!HvEfiIommuDomainIdSupported(capability,
                state->DmaPlan.DomainId) ||
            root != state->LegacyTablesPhysical ||
            (status & (HV_GPU_BOOT_VTD_GSTS_RTPS |
                       HV_GPU_BOOT_VTD_GSTS_TES)) !=
                (HV_GPU_BOOT_VTD_GSTS_RTPS | HV_GPU_BOOT_VTD_GSTS_TES))
            return 0;
    }
    return 1;
}

static EFI_STATUS HvGpuBootQuiesceBusMaster(void)
{
    uint16_t command;
    uint16_t desired;
    uint16_t readback;
    if (!HvGpuBootReadCommand(&command) ||
        (command & HV_GPU_BOOT_PCI_COMMAND_IO) != 0 ||
        (command & HV_GPU_BOOT_PCI_COMMAND_MEMORY) == 0)
        return EFI_UNSUPPORTED;
    gHvGpuOriginalPciCommand = command;
    gHvGpuPciCommandCaptured = 1;
    /* Keep cleanup armed before the write because an EFI protocol error does
     * not prove the configuration write failed to reach the device. */
    gHvGpuPciBmeQuiesced =
        (command & HV_GPU_BOOT_PCI_COMMAND_BUSMASTER) != 0;
    desired = command & (uint16_t)~HV_GPU_BOOT_PCI_COMMAND_BUSMASTER;
    gHvGpuPciCommandNeedsRestore = desired != command;
    if (desired != command && !HvGpuBootWriteCommand(desired))
        return EFI_DEVICE_ERROR;
    if (!HvGpuBootReadCommand(&readback) || readback != desired ||
        (readback & HV_GPU_BOOT_PCI_COMMAND_BUSMASTER) != 0) {
        gHvGpuPciCommandNeedsRestore = 1;
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}

static uint32_t HvGpuBootRestorePciCommandBeforeInstall(
    const HV_EFI_CONTEXT *context)
{
    uint16_t readback;
    if (gHvGpuPciCommandCaptured == 0 ||
        gHvGpuPciCommandNeedsRestore == 0)
        return 1;
    /* The caller must fail-stop after VT-d installation. Never reopen DMA
     * after that boundary, even when the caller reaches generic cleanup. */
    if (context == NULL ||
        (context->DmaIsolationState != HV_EFI_IOMMU_RUNTIME_RESET &&
         context->DmaIsolationState != HV_EFI_IOMMU_RUNTIME_ADMITTED))
        return 0;
    if (!HvGpuBootWriteCommand(gHvGpuOriginalPciCommand) ||
        !HvGpuBootReadCommand(&readback) ||
        readback != gHvGpuOriginalPciCommand) return 0;
    gHvGpuPciBmeQuiesced = 0;
    gHvGpuPciCommandCaptured = 0;
    gHvGpuPciCommandNeedsRestore = 0;
    return 1;
}

static uint32_t HvGpuBootEnableBusMaster(HV_EFI_CONTEXT *context,
                                          HV_GPU_NATIVE_STATE *state)
{
    uint16_t command;
    uint16_t desired;
    uint16_t readback;
    if (!HvGpuBootProveIsolation(context, state) ||
        !HvGpuBootReadCommand(&command) ||
        (command & HV_GPU_BOOT_PCI_COMMAND_IO) != 0 ||
        (command & HV_GPU_BOOT_PCI_COMMAND_MEMORY) == 0 ||
        (command & HV_GPU_BOOT_PCI_COMMAND_BUSMASTER) != 0) return 0;
    desired = command | HV_GPU_BOOT_PCI_COMMAND_BUSMASTER |
        HV_GPU_BOOT_PCI_COMMAND_INTERRUPT_DISABLE;
    if (desired != command && !HvGpuBootWriteCommand(desired)) return 0;
    if (!HvGpuBootReadCommand(&readback) || readback != desired ||
        (readback & HV_GPU_BOOT_PCI_COMMAND_IO) != 0 ||
        (readback & (HV_GPU_BOOT_PCI_COMMAND_MEMORY |
                     HV_GPU_BOOT_PCI_COMMAND_BUSMASTER)) !=
            (HV_GPU_BOOT_PCI_COMMAND_MEMORY |
             HV_GPU_BOOT_PCI_COMMAND_BUSMASTER)) return 0;
    return 1;
}

static uint32_t HvGpuBootReadPlaneRegs(uint32_t pipe,
                                       uint32_t *base,
                                       HV_GPU_BOOT_PLANE_SNAPSHOT *plane,
                                       uint32_t *liveSurface)
{
    uint64_t regBase;
    if (plane == NULL || liveSurface == NULL || pipe > 3) return 0;
    regBase = (uint64_t)*base + (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE;
    return HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, regBase,
                              &plane->Control) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + HV_GPU_BOOT_PLANE_STRIDE_OFFSET, &plane->Stride) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + HV_GPU_BOOT_PLANE_SIZE_OFFSET, &plane->Size) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + HV_GPU_BOOT_PLANE_SURFACE_OFFSET, &plane->Surface) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + HV_GPU_BOOT_PLANE_LIVE_OFFSET, liveSurface) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + UINT32_C(0x24), &plane->Offset) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + HV_GPU_BOOT_PLANE_POSITION_OFFSET, &plane->Position) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + HV_GPU_BOOT_PLANE_COLOR_OFFSET,
            &plane->ColorControl) &&
        HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            regBase + HV_GPU_BOOT_PLANE_AUX_DISTANCE_OFFSET,
            &plane->AuxDistance);
}

static uint32_t HvGpuBootReadGgttPte(uint32_t index, uint64_t *pte)
{
    return HvGpuBootReadBar64(gHvGpuIntelPciIo, 0,
        HV_GPU_BOOT_GGTT_APERTURE_OFFSET + (uint64_t)index * 8, pte);
}

static uint32_t HvGpuBootWriteGgttPte(uint32_t index, uint64_t pte)
{
    return HvGpuBootWriteBar64(gHvGpuIntelPciIo, 0,
        HV_GPU_BOOT_GGTT_APERTURE_OFFSET + (uint64_t)index * 8, pte);
}

static uint32_t HvGpuBootSaveSecondaryPlanes(uint32_t pipe)
{
    const uint32_t bases[2] = {
        HV_GPU_BOOT_PLANE2_A, HV_GPU_BOOT_PLANE3_A
    };
    uint32_t *controls[2] = {
        &gHvGpuPreviousPlane2Control,
        &gHvGpuPreviousPlane3Control
    };
    uint32_t *surfaces[2] = {
        &gHvGpuPreviousPlane2Surface,
        &gHvGpuPreviousPlane3Surface
    };
    uint32_t index;
    for (index = 0; index < 2; ++index) {
        const uint64_t base = (uint64_t)bases[index] +
            (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE;
        if (!HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, base, controls[index]) ||
            !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
                base + HV_GPU_BOOT_PLANE_SURFACE_OFFSET, surfaces[index]))
            return 0;
    }
    if (!HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            HV_GPU_BOOT_CURSOR_A + (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE,
            &gHvGpuPreviousCursorControl)) return 0;
    return 1;
}

static uint32_t HvGpuBootDisableSecondaryPlanes(uint32_t pipe)
{
    const uint32_t bases[2] = {
        HV_GPU_BOOT_PLANE2_A, HV_GPU_BOOT_PLANE3_A
    };
    const uint32_t oldControls[2] = {
        gHvGpuPreviousPlane2Control,
        gHvGpuPreviousPlane3Control
    };
    const uint32_t oldSurfaces[2] = {
        gHvGpuPreviousPlane2Surface,
        gHvGpuPreviousPlane3Surface
    };
    uint32_t index;
    for (index = 0; index < 2; ++index) {
        if ((oldControls[index] & HV_GPU_BOOT_PLANE_ENABLE) != 0) {
            uint32_t control = oldControls[index] & ~HV_GPU_BOOT_PLANE_ENABLE;
            uint32_t readback;
            if (!HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
                    (uint64_t)bases[index] +
                        (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE,
                    control) ||
                !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
                    (uint64_t)bases[index] +
                        (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE +
                        HV_GPU_BOOT_PLANE_SURFACE_OFFSET,
                    oldSurfaces[index]) ||
                !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
                    (uint64_t)bases[index] +
                        (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE,
                    &readback) ||
                (readback & HV_GPU_BOOT_PLANE_ENABLE) != 0) return 0;
        }
    }
    if ((gHvGpuPreviousCursorControl & HV_GPU_BOOT_PLANE_ENABLE) != 0 &&
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            HV_GPU_BOOT_CURSOR_A + (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE,
            gHvGpuPreviousCursorControl & ~HV_GPU_BOOT_PLANE_ENABLE)) return 0;
    if ((gHvGpuPreviousCursorControl & HV_GPU_BOOT_PLANE_ENABLE) != 0) {
        uint32_t readback;
        if (!HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
                HV_GPU_BOOT_CURSOR_A + (uint64_t)pipe *
                    HV_GPU_BOOT_PIPE_STRIDE, &readback) ||
            (readback & HV_GPU_BOOT_PLANE_ENABLE) != 0) return 0;
    }
    return 1;
}

static uint32_t HvGpuBootDisablePrimaryPlane(uint32_t pipe)
{
    const uint64_t controlOffset = HV_GPU_BOOT_PLANE1_A +
        (uint64_t)pipe * HV_GPU_BOOT_PIPE_STRIDE;
    uint32_t control;
    uint32_t readback;
    if (pipe > 3 ||
        !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, controlOffset, &control) ||
        (control & HV_GPU_BOOT_PLANE_ENABLE) == 0 ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0, controlOffset,
            control & ~HV_GPU_BOOT_PLANE_ENABLE) ||
        !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, controlOffset, &readback) ||
        (readback & HV_GPU_BOOT_PLANE_ENABLE) != 0) return 0;
    return 1;
}

static uint32_t HvGpuBootRestorePlaneBeforeSurface(HV_GPU_NATIVE_STATE *state)
{
    uint32_t planeBase = HV_GPU_BOOT_PLANE1_A;
    uint32_t success = 1;
    uint32_t index;
    const uint32_t secondaryBases[2] = {
        HV_GPU_BOOT_PLANE2_A, HV_GPU_BOOT_PLANE3_A
    };
    const uint32_t secondaryControls[2] = {
        gHvGpuPreviousPlane2Control,
        gHvGpuPreviousPlane3Control
    };
    const uint32_t secondarySurfaces[2] = {
        gHvGpuPreviousPlane2Surface,
        gHvGpuPreviousPlane3Surface
    };
    uint64_t planeBaseOffset = (uint64_t)planeBase +
        (uint64_t)state->Allocation.Pipe * HV_GPU_BOOT_PIPE_STRIDE;
    if (gHvGpuArmSnapshotValid == 0) return 1;
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0, planeBaseOffset,
        gHvGpuPreviousPlane1.Control & ~HV_GPU_BOOT_PLANE_ENABLE);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        planeBaseOffset + HV_GPU_BOOT_PLANE_STRIDE_OFFSET,
        gHvGpuPreviousPlane1.Stride);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        planeBaseOffset + HV_GPU_BOOT_PLANE_SIZE_OFFSET,
        gHvGpuPreviousPlane1.Size);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        planeBaseOffset + UINT32_C(0x24), gHvGpuPreviousPlane1.Offset);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        planeBaseOffset + HV_GPU_BOOT_PLANE_POSITION_OFFSET,
        gHvGpuPreviousPlane1.Position);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        planeBaseOffset + HV_GPU_BOOT_PLANE_COLOR_OFFSET,
        gHvGpuPreviousPlane1.ColorControl);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        planeBaseOffset + HV_GPU_BOOT_PLANE_AUX_DISTANCE_OFFSET,
        gHvGpuPreviousPlane1.AuxDistance);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        planeBaseOffset + HV_GPU_BOOT_PLANE_SURFACE_OFFSET,
        gHvGpuPreviousPlane1.Surface);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0, planeBaseOffset,
                                   gHvGpuPreviousPlane1.Control);
    for (index = 0; index < 2; ++index) {
        if ((secondaryControls[index] & HV_GPU_BOOT_PLANE_ENABLE) != 0) {
            success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
                (uint64_t)secondaryBases[index] +
                    (uint64_t)state->Allocation.Pipe *
                        HV_GPU_BOOT_PIPE_STRIDE +
                    HV_GPU_BOOT_PLANE_SURFACE_OFFSET,
                secondarySurfaces[index]);
            success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
                (uint64_t)secondaryBases[index] +
                    (uint64_t)state->Allocation.Pipe *
                        HV_GPU_BOOT_PIPE_STRIDE,
                secondaryControls[index]);
        }
    }
    if ((gHvGpuPreviousCursorControl & HV_GPU_BOOT_PLANE_ENABLE) != 0)
        success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            HV_GPU_BOOT_CURSOR_A + (uint64_t)state->Allocation.Pipe *
                HV_GPU_BOOT_PIPE_STRIDE,
            gHvGpuPreviousCursorControl);
    success &= HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
        HV_GPU_BOOT_VGACNTRL, gHvGpuPreviousVgaControl);
    return success;
}

static uint32_t HvGpuBootRollbackGgtt(HV_GPU_NATIVE_STATE *state)
{
    uint32_t count = gHvGpuBootPteWritten;
    uint32_t first = state->Allocation.GgttBase /
        (uint32_t)HV_GPU_BOOT_PAGE_SIZE;
    uint32_t index;
    uint32_t success = 1;
    for (index = 0; index < count; ++index)
        success &= HvGpuBootWriteGgttPte(first + index, 0);
    for (index = 0; index < count; ++index) {
        uint64_t value = UINT64_MAX;
        success &= HvGpuBootReadGgttPte(first + index, &value) && value == 0;
    }
    if (success) {
        gHvGpuBootPteWritten = 0;
        state->Reserved = 0;
    }
    return success;
}

static void HvGpuBootFreeState(HV_GPU_NATIVE_STATE *state,
                               HV_EFI_CONTEXT *context)
{
    EFI_PHYSICAL_ADDRESS self = state->SelfPhysical;
    UINTN selfPages = (UINTN)state->SelfPages;
    uint64_t framebuffer = state->FramebufferPhysical;
    uint64_t framebufferBytes = state->FramebufferBytes;
    uint64_t absent = state->AbsentConfigPhysical;
    uint64_t dma = state->DmaTablesPhysical;
    if (context != NULL) context->GpuStatePhysical = 0;
    state->Marker = 0;
    if (framebuffer != 0 && framebufferBytes != 0)
        (void)gHvGpuBootServices->FreePages(framebuffer,
            (UINTN)(framebufferBytes / HV_GPU_BOOT_PAGE_SIZE));
    if (absent != 0)
        (void)gHvGpuBootServices->FreePages(absent, 1);
    if (dma != 0)
        (void)gHvGpuBootServices->FreePages(dma,
            HV_GPU_NATIVE_DMA_PAGES);
    if (self != 0 && selfPages != 0)
        (void)gHvGpuBootServices->FreePages(self, selfPages);
}

EFI_STATUS HvGpuBootPrepare(HV_EFI_CONTEXT *context,
                            EFI_SYSTEM_TABLE *systemTable,
                            uint64_t ecamBase,
                            uint64_t ecamBytes)
{
    HV_GPU_NATIVE_STATE *state = NULL;
    EFI_PHYSICAL_ADDRESS statePhysical = 0;
    EFI_PHYSICAL_ADDRESS framebufferPhysical = 0;
    uint64_t absentPhysical = 0;
    uint64_t dmaPhysical = 0;
    uint64_t framebufferBytes;
    uint32_t framebufferPages;
    HV_GPU_EXTENT extent;
    uint8_t *memory;
    uint64_t sessionId = 0;
    EFI_STATUS status;

    if (context == NULL || systemTable == NULL ||
        systemTable->BootServices == NULL) return EFI_INVALID_PARAMETER;
    if (context->GpuStatePhysical != 0 || gHvGpuBootState != NULL)
        return EFI_ALREADY_STARTED;
    /* This build-time acknowledgement records the operator's manual proof
     * that an Intel connector has an attached diagnostic display. GOP and an
     * active pipe prove firmware mode only; they cannot detect a physical sink. */
    if (HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED == 0) {
        context->GpuStatePhysical = 0;
        return EFI_UNSUPPORTED;
    }
    gHvGpuBootServices = systemTable->BootServices;
    if (!HvGpuBootAllocateStatePages(&state, &statePhysical))
        return EFI_OUT_OF_RESOURCES;
    gHvGpuBootState = state;
    context->GpuStatePhysical = statePhysical;

    if (!HvGpuBootDiscover(systemTable, ecamBase, ecamBytes, state)) {
        status = EFI_UNSUPPORTED;
        goto fail;
    }
    framebufferBytes = state->FramebufferBytes;
    framebufferPages = (uint32_t)(framebufferBytes / HV_GPU_BOOT_PAGE_SIZE);
    status = HvGpuBootAllocate(framebufferPages, &framebufferPhysical);
    if (EFI_ERROR(status) ||
        framebufferPhysical < HV_GPU_BOOT_MIN_FRAMEBUFFER) {
        status = EFI_OUT_OF_RESOURCES;
        goto fail;
    }
    state->FramebufferPhysical = framebufferPhysical;
    state->Allocation.PhysicalBase = framebufferPhysical;
    memory = (uint8_t *)(uintptr_t)framebufferPhysical;
    HvGpuBootZero(memory, (size_t)framebufferBytes);

    if (!HvGpuBootAllocatePagesInto(1, &absentPhysical)) {
        status = EFI_OUT_OF_RESOURCES;
        goto fail;
    }
    state->AbsentConfigPhysical = absentPhysical;
    if (!HvGpuBootAllocatePagesInto(HV_GPU_NATIVE_DMA_PAGES, &dmaPhysical)) {
        status = EFI_OUT_OF_RESOURCES;
        goto fail;
    }
    state->DmaTablesPhysical = dmaPhysical;
    HvGpuBootFill((void *)(uintptr_t)absentPhysical, 0xff,
                  (size_t)HV_GPU_BOOT_PAGE_SIZE);

    if (systemTable->BootServices->GetNextMonotonicCount == NULL ||
        systemTable->BootServices->GetNextMonotonicCount(&sessionId) !=
            EFI_SUCCESS ||
        (sessionId == 0 &&
         (systemTable->BootServices->GetNextMonotonicCount(&sessionId) !=
              EFI_SUCCESS || sessionId == 0))) {
        status = EFI_UNSUPPORTED;
        goto fail;
    }
    state->SessionId = sessionId;
    if (context->EnabledProcessorCount == 0 ||
        context->EnabledProcessorCount > HV_EFI_MAX_SUPPORTED_PROCESSORS ||
        context->BspProcessorNumber >= context->ProcessorCount) {
        status = EFI_UNSUPPORTED;
        goto fail;
    }
    state->CurrentCpu = context->BspProcessorNumber;
    state->OwnerCpu = context->BspProcessorNumber;
    state->CpuCount = context->EnabledProcessorCount;
    state->Bound = 0;
    state->Lost = 0;
    state->Reserved = 0;
    state->PhysicalOutputVerified = 1;
    HvGpuBootInitializeEntities(state);
    if (state->Entities.Entities[0].Width < 2 ||
        state->Entities.Entities[0].Height < 2 ||
        HvGpuValidateExtent(&state->Allocation, &extent) != HV_GPU_OK) {
        status = EFI_UNSUPPORTED;
        goto fail;
    }
    /* The VT-d installer snapshots PCI commands and restores them when it
     * completes. Clear BME only after discovery/allocation validation so that
     * installer records the Intel function quiesced. */
    status = HvGpuBootQuiesceBusMaster();
    if (EFI_ERROR(status)) goto fail;
    return EFI_SUCCESS;

fail:
    if (state != NULL && state->Reserved == 0)
        HvGpuBootReleaseBeforeLaunch(context);
    return status;
}

static uint32_t HvGpuBootCurrentPipeStillValid(HV_GPU_NATIVE_STATE *state)
{
    uint32_t pipeConf;
    uint32_t pipeSrc;
    uint32_t pipe;
    uint32_t activeCount = 0;
    uint32_t liveSurface;
    HV_GPU_BOOT_PLANE_SNAPSHOT plane;
    uint32_t planeBase = HV_GPU_BOOT_PLANE1_A;
    for (pipe = 0; pipe < 4; ++pipe) {
        if (!HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
                HV_GPU_BOOT_PIPECONF_A + (uint64_t)pipe *
                    HV_GPU_BOOT_PIPE_STRIDE, &pipeConf)) return 0;
        if ((pipeConf & HV_GPU_BOOT_PIPE_ENABLE) != 0) ++activeCount;
    }
    if (activeCount != 1 ||
        !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            HV_GPU_BOOT_PIPECONF_A +
                (uint64_t)state->Allocation.Pipe * HV_GPU_BOOT_PIPE_STRIDE,
            &pipeConf) || (pipeConf & HV_GPU_BOOT_PIPE_ENABLE) == 0 ||
        !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0,
            HV_GPU_BOOT_PIPESRC_A +
                (uint64_t)state->Allocation.Pipe * HV_GPU_BOOT_PIPE_STRIDE,
            &pipeSrc) ||
        ((pipeSrc >> 16) & HV_GPU_BOOT_PIPESRC_DIMENSION_MASK) + 1u !=
            state->Allocation.Width ||
        (pipeSrc & HV_GPU_BOOT_PIPESRC_DIMENSION_MASK) + 1u !=
            state->Allocation.Height ||
        !HvGpuBootOtherPlanesDisabled(gHvGpuIntelPciIo,
                                      state->Allocation.Pipe) ||
        !HvGpuBootReadPlaneRegs(state->Allocation.Pipe, &planeBase, &plane,
                                &liveSurface) ||
        (plane.Control & HV_GPU_BOOT_PLANE_ENABLE) == 0 ||
        plane.Size != (((state->Allocation.Height - 1u) << 16) |
                       (state->Allocation.Width - 1u)) ||
        plane.Surface != liveSurface ||
        plane.Stride == 0 ||
        (uint64_t)plane.Stride * 64u <
            (uint64_t)state->Allocation.Width * 4u ||
        plane.Offset != 0 || plane.Position != 0) return 0;
    return 1;
}

static uint32_t HvGpuBootVerifyPlaneAndGgtt(HV_GPU_NATIVE_STATE *state)
{
    HV_GPU_PLANE plane;
    uint32_t base = HV_GPU_BOOT_PLANE1_A;
    uint32_t live;
    uint64_t pte;
    uint64_t physical;
    uint32_t index;
    uint32_t pageCount = (uint32_t)(state->Allocation.PinnedBytes /
                                    HV_GPU_BOOT_PAGE_SIZE);
    HV_GPU_BOOT_PLANE_SNAPSHOT values;
    if (!HvGpuBootReadPlaneRegs(state->Allocation.Pipe, &base, &values, &live))
        return 0;
    plane.Control = values.Control;
    plane.Stride = values.Stride;
    plane.Size = values.Size;
    plane.Surface = values.Surface;
    plane.LiveSurface = live;
    plane.Offset = values.Offset;
    plane.Position = values.Position;
    plane.ColorControl = values.ColorControl;
    plane.AuxDistance = values.AuxDistance;
    if (HvGpuIntelValidatePlane(&plane, &state->Allocation) != HV_GPU_OK)
        return 0;
    for (index = 0; index < pageCount; ++index) {
        const uint32_t ggttIndex = state->Allocation.GgttBase /
            (uint32_t)HV_GPU_BOOT_PAGE_SIZE + index;
        if (!HvGpuBootReadGgttPte(ggttIndex, &pte) ||
            HvGpuIntelGgttPhysical(pte, &physical) != HV_GPU_OK ||
            physical != state->Allocation.PhysicalBase +
                (uint64_t)index * HV_GPU_BOOT_PAGE_SIZE) return 0;
    }
    return 1;
}

static uint32_t HvGpuBootDisableLegacyVga(HV_GPU_NATIVE_STATE *state)
{
    uint32_t vga;
    uint16_t command;
    if (!HvGpuBootReadCommand(&command) ||
        (command & HV_GPU_BOOT_PCI_COMMAND_IO) != 0 ||
        (command & HV_GPU_BOOT_PCI_COMMAND_MEMORY) == 0 ||
        !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, HV_GPU_BOOT_VGACNTRL, &vga))
        return 0;
    if ((vga & HV_GPU_BOOT_VGA_DISABLE) == 0 &&
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0, HV_GPU_BOOT_VGACNTRL,
                             vga | HV_GPU_BOOT_VGA_DISABLE)) return 0;
    if (!HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, HV_GPU_BOOT_VGACNTRL, &vga) ||
        (vga & HV_GPU_BOOT_VGA_DISABLE) == 0) return 0;
    (void)state;
    return 1;
}

static uint32_t HvGpuBootWaitLiveSurface(HV_GPU_NATIVE_STATE *state)
{
    uint32_t poll;
    uint32_t live;
    const uint32_t liveOffset = HV_GPU_BOOT_PLANE1_A +
        state->Allocation.Pipe * HV_GPU_BOOT_PIPE_STRIDE +
        HV_GPU_BOOT_PLANE_LIVE_OFFSET;
    for (poll = 0; poll < HV_GPU_BOOT_MAX_POLL_COUNT; ++poll) {
        if (!HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, liveOffset, &live))
            return 0;
        if (live == state->Allocation.GgttBase) return 1;
        if (gHvGpuBootServices != NULL &&
            gHvGpuBootServices->Stall != NULL)
            (void)gHvGpuBootServices->Stall(HV_GPU_BOOT_POLL_US);
    }
    return 0;
}

static EFI_STATUS HvGpuBootFailArm(HV_GPU_NATIVE_STATE *state)
{
    if (state != NULL) {
        /* IOMMU enforcement is already live at this stage. Do not hand the
         * old plane or bus-master state back to firmware on an error. */
        state->Reserved = 1;
        state->Lost = 1;
    }
    gHvGpuBootArmStarted = 1;
    return EFI_UNSUPPORTED;
}

EFI_STATUS HvGpuBootArm(HV_EFI_CONTEXT *context)
{
    HV_GPU_NATIVE_STATE *state;
    uint16_t command;
    uint32_t ggc;
    uint64_t ggttPhysical;
    uint64_t ggttBytes;
    uint32_t pageCount;
    uint32_t firstIndex;
    uint32_t index;
    uint32_t planeBase;
    uint32_t planeBaseLocal;
    uint64_t planeRegisterBase;
    uint32_t control;
    uint32_t liveSurface;
    EFI_STATUS status;

    if (context == NULL) return EFI_INVALID_PARAMETER;
    if (context->GpuStatePhysical == 0) return EFI_SUCCESS;
    state = (HV_GPU_NATIVE_STATE *)(uintptr_t)context->GpuStatePhysical;
    if (state->Marker != HV_GPU_NATIVE_MARKER ||
        state != gHvGpuBootState || state->Bound != 0 || state->Lost != 0 ||
        state->Reserved != 0 || state->PhysicalOutputVerified != 1 ||
        gHvGpuIntelPciIo == NULL ||
        gHvGpuIntelController == NULL || gHvGpuIntelGopHandle == NULL ||
        gHvGpuBootServices == NULL ||
        gHvGpuBootServices->DisconnectController == NULL ||
        gHvGpuBootServices->ConnectController == NULL) return EFI_UNSUPPORTED;
    /* From this point onward the VT-d domain is live. Errors fail-stop at the
     * caller and keep the reserved pages pinned; no firmware hand-back is
     * attempted. Quiesce DMA before touching the active plane. */
    state->Reserved = 1;
    gHvGpuBootArmStarted = 1;
    if (gHvGpuPciCommandCaptured != 1 ||
        ((gHvGpuOriginalPciCommand & HV_GPU_BOOT_PCI_COMMAND_BUSMASTER) != 0 &&
         gHvGpuPciBmeQuiesced == 0) ||
        !HvGpuBootReadCommand(&command) ||
        (command & HV_GPU_BOOT_PCI_COMMAND_IO) != 0 ||
        (command & HV_GPU_BOOT_PCI_COMMAND_MEMORY) == 0 ||
        (command & HV_GPU_BOOT_PCI_COMMAND_BUSMASTER) != 0)
        return HvGpuBootFailArm(state);
    if (!HvGpuBootReadStolenGgtt(
            state->Filter.EcamPage - HV_GPU_BOOT_DISPLAY_ECAM_OFFSET, &ggc,
            &ggttPhysical, &ggttBytes) ||
        ggttPhysical != state->GgttPhysical || ggttBytes != state->GgttBytes ||
        !HvGpuBootCurrentPipeStillValid(state))
        return HvGpuBootFailArm(state);
    pageCount = (uint32_t)(state->Allocation.PinnedBytes /
                           HV_GPU_BOOT_PAGE_SIZE);
    firstIndex = state->Allocation.GgttBase /
        (uint32_t)HV_GPU_BOOT_PAGE_SIZE;
    for (index = 0; index < pageCount; ++index) {
        uint64_t oldPte;
        if (!HvGpuBootReadGgttPte(firstIndex + index, &oldPte) ||
            oldPte != 0) return HvGpuBootFailArm(state);
    }

    status = gHvGpuBootServices->DisconnectController(
        gHvGpuIntelController, NULL, NULL);
    if (EFI_ERROR(status)) return HvGpuBootFailArm(state);
    gHvGpuBootGopDisconnected = 1;
    planeBaseLocal = HV_GPU_BOOT_PLANE1_A;
    if (!HvGpuBootCurrentPipeStillValid(state) ||
        !HvGpuBootReadPlaneRegs(state->Allocation.Pipe,
            &planeBaseLocal, &gHvGpuPreviousPlane1, &liveSurface) ||
        !HvGpuBootSaveSecondaryPlanes(state->Allocation.Pipe) ||
        !HvGpuBootReadBar32(gHvGpuIntelPciIo, 0, HV_GPU_BOOT_VGACNTRL,
                            &gHvGpuPreviousVgaControl))
        return HvGpuBootFailArm(state);
    gHvGpuArmSnapshotValid = 1;
    if (!HvGpuBootDisableLegacyVga(state) ||
        !HvGpuBootDisableSecondaryPlanes(state->Allocation.Pipe) ||
        !HvGpuBootDisablePrimaryPlane(state->Allocation.Pipe))
        return HvGpuBootFailArm(state);

    for (index = 0; index < pageCount; ++index) {
        uint64_t oldPte;
        if (!HvGpuBootReadGgttPte(firstIndex + index, &oldPte) ||
            oldPte != 0) return HvGpuBootFailArm(state);
    }
    for (index = 0; index < pageCount; ++index) {
        const uint64_t physical = state->Allocation.PhysicalBase +
            (uint64_t)index * HV_GPU_BOOT_PAGE_SIZE;
        const uint64_t pte = physical | UINT64_C(1);
        if (!HvGpuBootWriteGgttPte(firstIndex + index, pte))
            return HvGpuBootFailArm(state);
        ++gHvGpuBootPteWritten;
    }
    for (index = 0; index < pageCount; ++index) {
        uint64_t pte;
        uint64_t physical;
        if (!HvGpuBootReadGgttPte(firstIndex + index, &pte) ||
            HvGpuIntelGgttPhysical(pte, &physical) != HV_GPU_OK ||
            physical != state->Allocation.PhysicalBase +
                (uint64_t)index * HV_GPU_BOOT_PAGE_SIZE)
            return HvGpuBootFailArm(state);
    }

    planeBase = HV_GPU_BOOT_PLANE1_A;
    planeRegisterBase = (uint64_t)planeBase +
        (uint64_t)state->Allocation.Pipe * HV_GPU_BOOT_PIPE_STRIDE;
    control = HV_GPU_INTEL_XRGB8888;
    if (!HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0, planeRegisterBase,
                             control) ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            planeRegisterBase + HV_GPU_BOOT_PLANE_STRIDE_OFFSET,
            state->Allocation.StrideBytes / 64u) ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            planeRegisterBase + HV_GPU_BOOT_PLANE_SIZE_OFFSET,
            ((state->Allocation.Height - 1u) << 16) |
                (state->Allocation.Width - 1u)) ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            planeRegisterBase + UINT32_C(0x24), 0) ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            planeRegisterBase + HV_GPU_BOOT_PLANE_POSITION_OFFSET, 0) ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            planeRegisterBase + HV_GPU_BOOT_PLANE_COLOR_OFFSET,
            HV_GPU_INTEL_COLOR_GAMMA_DISABLE) ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            planeRegisterBase + HV_GPU_BOOT_PLANE_AUX_DISTANCE_OFFSET, 0) ||
        !HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0, planeRegisterBase,
                             control | HV_GPU_BOOT_PLANE_ENABLE))
        return HvGpuBootFailArm(state);

    /* BME is still clear, so enabling the plane cannot fetch from the old
     * surface. Program PLANE_SURF only after the reserved GGTT entries exist. */
    if (!HvGpuBootWriteBar32(gHvGpuIntelPciIo, 0,
            planeRegisterBase + HV_GPU_BOOT_PLANE_SURFACE_OFFSET,
            state->Allocation.GgttBase) ||
        !HvGpuBootEnableBusMaster(context, state) ||
        !HvGpuBootWaitLiveSurface(state) ||
        !HvGpuBootVerifyPlaneAndGgtt(state))
        return HvGpuBootFailArm(state);
    state->Bound = 1;
    state->Lost = 0;
    return EFI_SUCCESS;
}

void HvGpuBootReleaseBeforeLaunch(HV_EFI_CONTEXT *context)
{
    HV_GPU_NATIVE_STATE *state;
    if (context == NULL || context->GpuStatePhysical == 0 ||
        gHvGpuBootServices == NULL) return;
    state = (HV_GPU_NATIVE_STATE *)(uintptr_t)context->GpuStatePhysical;
    if (state->Marker != HV_GPU_NATIVE_MARKER || state != gHvGpuBootState ||
        state->Reserved != 0 || gHvGpuBootArmStarted != 0) return;
    if (gHvGpuBootPteWritten != 0 && !HvGpuBootRollbackGgtt(state)) return;
    if (gHvGpuArmSnapshotValid != 0 &&
        !HvGpuBootRestorePlaneBeforeSurface(state)) return;
    if (!HvGpuBootRestorePciCommandBeforeInstall(context)) {
        /* Keep the state and pages pinned. The caller is already taking a
         * boot-failure path; it must not proceed as though ownership was
         * cleanly restored. */
        state->Reserved = 1;
        state->Lost = 1;
        gHvGpuBootArmStarted = 1;
        return;
    }
    if (gHvGpuBootGopDisconnected != 0 &&
        gHvGpuBootServices->ConnectController != NULL) {
        if (!EFI_ERROR(gHvGpuBootServices->ConnectController(
                gHvGpuIntelController, NULL, NULL, TRUE)))
            gHvGpuBootGopDisconnected = 0;
    }
    gHvGpuPciCommandCaptured = 0;
    gHvGpuPciBmeQuiesced = 0;
    gHvGpuPciCommandNeedsRestore = 0;
    gHvGpuOriginalPciCommand = 0;
    HvGpuBootFreeState(state, context);
    gHvGpuBootState = NULL;
    gHvGpuIntelController = NULL;
    gHvGpuIntelGopHandle = NULL;
    gHvGpuIntelPciIo = NULL;
    gHvGpuBootGopDisconnected = 0;
    gHvGpuBootArmStarted = 0;
    gHvGpuBootPteWritten = 0;
    gHvGpuArmSnapshotValid = 0;
}

#endif
