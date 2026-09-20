#include "shared_memory_publisher.h"

#include <sddl.h>

#include <cstddef>
#include <cstring>

namespace hypersinv::overlay {

SharedMemoryPublisher::~SharedMemoryPublisher() {
    Close();
}

bool SharedMemoryPublisher::Open(const std::wstring& mappingName,
                                 std::uint32_t maximumHz,
                                 PublisherAccess access) {
    Close();
    if (mappingName.empty() || maximumHz > 60 ||
        !QueryPerformanceFrequency(&frequency_)) {
        return false;
    }
    std::wstring mutexName = mappingName + L".WriterLock";
    writerMutex_ = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (writerMutex_ == nullptr) {
        return false;
    }
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    SECURITY_ATTRIBUTES securityAttributes{};
    SECURITY_ATTRIBUTES* selectedAttributes = nullptr;
    if (access == PublisherAccess::AuthenticatedUsersRead) {
        // The LocalSystem bridge retains full control while an interactive,
        // authenticated overlay receives read-only mapping access.
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;SY)(A;;GR;;;AU)",
                SDDL_REVISION_1,
                &securityDescriptor,
                nullptr)) {
            Close();
            return false;
        }
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.lpSecurityDescriptor = securityDescriptor;
        selectedAttributes = &securityAttributes;
    }
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                  selectedAttributes,
                                  PAGE_READWRITE,
                                  0,
                                  sizeof(AnalysisResult),
                                  mappingName.c_str());
    if (securityDescriptor != nullptr) {
        LocalFree(securityDescriptor);
    }
    if (mapping_ == nullptr) {
        Close();
        return false;
    }
    view_ = static_cast<AnalysisResult*>(
        MapViewOfFile(mapping_, FILE_MAP_READ | FILE_MAP_WRITE,
                      0, 0, sizeof(AnalysisResult)));
    if (view_ == nullptr) {
        Close();
        return false;
    }
    minimumIntervalQpc_ = maximumHz == 0
        ? 0
        : (frequency_.QuadPart + static_cast<std::int64_t>(maximumHz) - 1) /
            static_cast<std::int64_t>(maximumHz);

    const DWORD wait = WaitForSingleObject(writerMutex_, 1000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        Close();
        return false;
    }
    std::memset(view_, 0, sizeof(*view_));
    view_->Header.Magic = kOverlayMagic;
    view_->Header.Version = kOverlayVersion;
    view_->Header.HeaderSize = sizeof(SharedOverlayHeader);
    view_->Header.EntityStride = sizeof(EntityData);
    view_->Header.Capacity = static_cast<std::uint32_t>(kMaximumEntities);
    view_->Header.ProducerProcessId = GetCurrentProcessId();
    ReleaseMutex(writerMutex_);
    return true;
}

void SharedMemoryPublisher::Close() noexcept {
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (writerMutex_ != nullptr) {
        CloseHandle(writerMutex_);
        writerMutex_ = nullptr;
    }
    frequency_.QuadPart = 0;
    lastPublishQpc_ = 0;
    minimumIntervalQpc_ = 0;
}

bool SharedMemoryPublisher::IsOpen() const noexcept {
    return view_ != nullptr && writerMutex_ != nullptr;
}

PublishStatus SharedMemoryPublisher::Publish(
    const AnalysisResult& frame) noexcept {
    if (!IsOpen()) {
        return PublishStatus::NotOpen;
    }
    if (!IsHeaderValid(frame.Header) ||
        frame.Header.EntityCount > kMaximumEntities) {
        return PublishStatus::InvalidFrame;
    }

    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
        return PublishStatus::InvalidFrame;
    }
    if (minimumIntervalQpc_ != 0 && lastPublishQpc_ != 0 &&
        now.QuadPart - lastPublishQpc_ < minimumIntervalQpc_) {
        return PublishStatus::RateLimited;
    }

    const DWORD wait = WaitForSingleObject(writerMutex_, 5);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        return PublishStatus::Busy;
    }

    volatile LONG64* sequence = reinterpret_cast<volatile LONG64*>(
        &view_->Header.Sequence);
    const LONG64 observed = InterlockedCompareExchange64(sequence, 0, 0);
    const LONG64 stable = (observed & 1) != 0 ? observed + 1 : observed;
    const LONG64 writing = stable + 1;
    const LONG64 published = stable + 2;
    InterlockedExchange64(sequence, writing);
    MemoryBarrier();

    constexpr std::size_t sequenceOffset =
        offsetof(SharedOverlayHeader, Sequence);
    constexpr std::size_t afterSequence = sequenceOffset + sizeof(std::uint64_t);
    std::memcpy(&view_->Header, &frame.Header, sequenceOffset);
    std::memcpy(reinterpret_cast<std::uint8_t*>(&view_->Header) + afterSequence,
                reinterpret_cast<const std::uint8_t*>(&frame.Header) +
                    afterSequence,
                sizeof(SharedOverlayHeader) - afterSequence);
    view_->Header.Magic = kOverlayMagic;
    view_->Header.Version = kOverlayVersion;
    view_->Header.HeaderSize = sizeof(SharedOverlayHeader);
    view_->Header.EntityStride = sizeof(EntityData);
    view_->Header.Capacity = static_cast<std::uint32_t>(kMaximumEntities);
    view_->Header.ProducerProcessId = GetCurrentProcessId();
    view_->Header.ProducerTimestampQpc =
        static_cast<std::uint64_t>(now.QuadPart);
    std::memcpy(view_->Entries,
                frame.Entries,
                frame.Header.EntityCount * sizeof(EntityData));
    if (frame.Header.EntityCount < kMaximumEntities) {
        std::memset(view_->Entries + frame.Header.EntityCount,
                    0,
                    (kMaximumEntities - frame.Header.EntityCount) *
                        sizeof(EntityData));
    }

    MemoryBarrier();
    InterlockedExchange64(sequence, published);
    lastPublishQpc_ = now.QuadPart;
    ReleaseMutex(writerMutex_);
    return PublishStatus::Success;
}

} // namespace hypersinv::overlay
