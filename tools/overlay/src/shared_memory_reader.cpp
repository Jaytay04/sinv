#include "shared_memory_reader.h"

#include <algorithm>
#include <cstring>

namespace hypersinv::overlay {
namespace {

std::uint64_t ReadSequence(const volatile std::uint64_t* value) noexcept {
    // The executable is x64-only. Aligned 64-bit loads are atomic on x64; the
    // barriers prevent the compiler/CPU from moving the payload copy across
    // the sequence observations. No interlocked write is used on a read-only
    // mapping.
    MemoryBarrier();
    const std::uint64_t result = *value;
    MemoryBarrier();
    return result;
}

} // namespace

SharedMemoryReader::~SharedMemoryReader() {
    Close();
}

bool SharedMemoryReader::Open(const std::wstring& mappingName) noexcept {
    Close();
    if (mappingName.empty()) {
        return false;
    }

    mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName.c_str());
    if (mapping_ == nullptr) {
        return false;
    }

    view_ = static_cast<const SharedOverlayBuffer*>(
        MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(SharedOverlayBuffer)));
    if (view_ == nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }
    return true;
}

void SharedMemoryReader::Close() noexcept {
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
}

bool SharedMemoryReader::IsOpen() const noexcept {
    return view_ != nullptr;
}

SnapshotReadStatus SharedMemoryReader::Read(OverlaySnapshot& snapshot) const noexcept {
    if (view_ == nullptr) {
        return SnapshotReadStatus::NotOpen;
    }

    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        const std::uint64_t before = ReadSequence(&view_->Header.Sequence);
        if ((before & 1u) != 0) {
            YieldProcessor();
            continue;
        }

        SharedOverlayHeader header{};
        std::memcpy(&header, &view_->Header, sizeof(header));
        if (!IsHeaderValid(header)) {
            return SnapshotReadStatus::InvalidData;
        }

        OverlaySnapshot candidate{};
        std::memcpy(&candidate.Header, &header, sizeof(header));
        const std::size_t count = header.EntityCount;
        std::memcpy(candidate.Entries,
                    view_->Entries,
                    count * sizeof(EntityData));
        MemoryBarrier();

        const std::uint64_t after = ReadSequence(&view_->Header.Sequence);
        if (before != after || (after & 1u) != 0) {
            YieldProcessor();
            continue;
        }

        candidate.Header.Sequence = after;
        for (std::size_t i = 0; i < count; ++i) {
            candidate.Entries[i].name[31] = L'\0';
        }
        snapshot = candidate;
        return SnapshotReadStatus::Success;
    }

    return SnapshotReadStatus::Busy;
}

} // namespace hypersinv::overlay
