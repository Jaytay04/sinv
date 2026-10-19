#include "hwid_spoofer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>

struct LegacyConfig {
    BOOL SpoofDiskSerial;
    WCHAR FakeDiskSerial[64];
    BOOL SpoofMacAddress;
    BYTE FakeMacAddress[6];
    BOOL SpoofSMBIOS;
    WCHAR FakeSMBIOSSerial[64];
    WCHAR FakeSMBIOSManufacturer[64];
    WCHAR FakeSMBIOSProduct[64];
    BOOL SpoofGPU;
    WCHAR FakeGPUSerial[64];
    BOOL SpoofMotherboard;
    WCHAR FakeMotherboardSerial[64];
    WCHAR FakeMotherboardManufacturer[64];
    WCHAR FakeMotherboardProduct[64];
    BOOL SpoofCPU;
    WCHAR FakeCPUSerial[64];
};

static_assert(offsetof(HWID_SPOOF_CONFIG, SpoofDiskSerial) == offsetof(LegacyConfig, SpoofDiskSerial), "Existing config offset changed: SpoofDiskSerial");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeDiskSerial) == offsetof(LegacyConfig, FakeDiskSerial), "Existing config offset changed: FakeDiskSerial");
static_assert(offsetof(HWID_SPOOF_CONFIG, SpoofMacAddress) == offsetof(LegacyConfig, SpoofMacAddress), "Existing config offset changed: SpoofMacAddress");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeMacAddress) == offsetof(LegacyConfig, FakeMacAddress), "Existing config offset changed: FakeMacAddress");
static_assert(offsetof(HWID_SPOOF_CONFIG, SpoofSMBIOS) == offsetof(LegacyConfig, SpoofSMBIOS), "Existing config offset changed: SpoofSMBIOS");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeSMBIOSSerial) == offsetof(LegacyConfig, FakeSMBIOSSerial), "Existing config offset changed: FakeSMBIOSSerial");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeSMBIOSManufacturer) == offsetof(LegacyConfig, FakeSMBIOSManufacturer), "Existing config offset changed: FakeSMBIOSManufacturer");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeSMBIOSProduct) == offsetof(LegacyConfig, FakeSMBIOSProduct), "Existing config offset changed: FakeSMBIOSProduct");
static_assert(offsetof(HWID_SPOOF_CONFIG, SpoofGPU) == offsetof(LegacyConfig, SpoofGPU), "Existing config offset changed: SpoofGPU");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeGPUSerial) == offsetof(LegacyConfig, FakeGPUSerial), "Existing config offset changed: FakeGPUSerial");
static_assert(offsetof(HWID_SPOOF_CONFIG, SpoofMotherboard) == offsetof(LegacyConfig, SpoofMotherboard), "Existing config offset changed: SpoofMotherboard");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeMotherboardSerial) == offsetof(LegacyConfig, FakeMotherboardSerial), "Existing config offset changed: FakeMotherboardSerial");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeMotherboardManufacturer) == offsetof(LegacyConfig, FakeMotherboardManufacturer), "Existing config offset changed: FakeMotherboardManufacturer");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeMotherboardProduct) == offsetof(LegacyConfig, FakeMotherboardProduct), "Existing config offset changed: FakeMotherboardProduct");
static_assert(offsetof(HWID_SPOOF_CONFIG, SpoofCPU) == offsetof(LegacyConfig, SpoofCPU), "Existing config offset changed: SpoofCPU");
static_assert(offsetof(HWID_SPOOF_CONFIG, FakeCPUSerial) == offsetof(LegacyConfig, FakeCPUSerial), "Existing config offset changed: FakeCPUSerial");
static_assert(offsetof(HWID_SPOOF_CONFIG, SpoofSMBIOSUUID) == sizeof(LegacyConfig), "UUID must be appended after the legacy prefix");
static_assert(sizeof(((HWID_SPOOF_CONFIG*)nullptr)->FakeSMBIOSUUID) == 37 * sizeof(WCHAR), "UUID text must fit canonical form and terminator");

static void Require(bool condition, const char* reason) {
    if (!condition) {
        std::cerr << reason << "\n";
        std::exit(1);
    }
}

int main() {
    constexpr std::uint64_t guard = 0xD13EC7A5C0FFEE42ull;
    struct GuardedConfig {
        std::uint64_t before;
        HWID_SPOOF_CONFIG value;
        std::uint64_t after;
    } guarded{guard, {}, guard};
    HwidSpooferGenerateRandomConfig(nullptr);
    HwidSpooferGenerateRandomConfig(&guarded.value);
    Require(guarded.before == guard && guarded.after == guard, "Configuration generation wrote outside the struct");
    Require(!guarded.value.SpoofCPU && !guarded.value.SpoofGPU, "Unavailable serials must remain disabled");
    Require(!guarded.value.SpoofSMBIOSUUID, "Observed UUID must not auto-enable replacement UUID");
    const auto zero = [](WCHAR value) { return value == 0; };
    Require(std::all_of(std::begin(guarded.value.FakeCPUSerial), std::end(guarded.value.FakeCPUSerial), zero), "CPU serial must be zero");
    Require(std::all_of(std::begin(guarded.value.FakeGPUSerial), std::end(guarded.value.FakeGPUSerial), zero), "GPU serial must be zero");
    Require(std::all_of(std::begin(guarded.value.FakeSMBIOSUUID), std::end(guarded.value.FakeSMBIOSUUID), zero), "UUID must be zero until explicitly supplied");
    Require(guarded.value.FakeDiskSerial[63] == 0 && guarded.value.FakeSMBIOSSerial[63] == 0 && guarded.value.FakeMotherboardSerial[63] == 0, "Serial buffer terminators must remain in bounds");
    std::cout << "HWID config append/default contract passed\n";
    return 0;
}
