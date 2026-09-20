#pragma once

#include "overlay_protocol.h"

#include <windows.h>

#include <string>

namespace hypersinv::overlay {

enum class SnapshotReadStatus {
    Success,
    NotOpen,
    Busy,
    InvalidData,
};

class SharedMemoryReader final {
public:
    SharedMemoryReader() = default;
    ~SharedMemoryReader();

    SharedMemoryReader(const SharedMemoryReader&) = delete;
    SharedMemoryReader& operator=(const SharedMemoryReader&) = delete;

    bool Open(const std::wstring& mappingName) noexcept;
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    SnapshotReadStatus Read(OverlaySnapshot& snapshot) const noexcept;

private:
    HANDLE mapping_ = nullptr;
    const SharedOverlayBuffer* view_ = nullptr;
};

} // namespace hypersinv::overlay
