#pragma once

#include <string>
#include <vector>

namespace hwid_spoofer {

struct DeviceIdentifier {
    std::string field;
    std::string device;
    std::string value;
    std::string source;
    std::string note;
    bool confirmed = false;
};

bool CollectDeviceIdentifiers(
    std::vector<DeviceIdentifier>& identifiers,
    std::wstring& error);

namespace detail {
enum class CpuSerialObservation {
    NotExposedByFirmware,
    Present
};

bool IsPlaceholderSerial(const std::wstring& value);
CpuSerialObservation ClassifyCpuSerialObservation(const std::wstring& value);
bool IsValidMacAddress(const std::wstring& value);
std::string MapWmiPropertyToField(
    const std::wstring& class_name,
    const std::wstring& property_name);
}  // namespace detail

}  // namespace hwid_spoofer
