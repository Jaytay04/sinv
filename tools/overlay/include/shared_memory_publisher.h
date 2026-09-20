#pragma once

#include "overlay_protocol.h"

#include <windows.h>

#include <cstdint>
#include <string>

namespace hypersinv::overlay {

enum class PublishStatus {
    Success,
    RateLimited,
    Busy,
    InvalidFrame,
    NotOpen,
};

enum class PublisherAccess {
    CurrentUserOnly,
    AuthenticatedUsersRead,
};

class SharedMemoryPublisher final {
public:
    SharedMemoryPublisher() = default;
    ~SharedMemoryPublisher();

    SharedMemoryPublisher(const SharedMemoryPublisher&) = delete;
    SharedMemoryPublisher& operator=(const SharedMemoryPublisher&) = delete;

    bool Open(
        const std::wstring& mappingName,
        std::uint32_t maximumHz = 60,
        PublisherAccess access = PublisherAccess::CurrentUserOnly);
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    PublishStatus Publish(const AnalysisResult& frame) noexcept;

private:
    HANDLE mapping_ = nullptr;
    HANDLE writerMutex_ = nullptr;
    AnalysisResult* view_ = nullptr;
    LARGE_INTEGER frequency_{};
    std::int64_t lastPublishQpc_ = 0;
    std::int64_t minimumIntervalQpc_ = 0;
};

} // namespace hypersinv::overlay
