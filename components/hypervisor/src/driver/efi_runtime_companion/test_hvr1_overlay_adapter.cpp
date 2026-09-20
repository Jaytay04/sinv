#include "hvr1_overlay_adapter.h"
#include "shared_memory_publisher.h"
#include "shared_memory_reader.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int Fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

void Name(HV_EFI_ANALYSIS_EXTRACTED_FIELD& field, const char* value) {
    strncpy_s(field.FieldName, value, _TRUNCATE);
}

} // namespace

int main() {
    HVR1_OVERLAY_SOURCE source{};
    source.Generation = 7;
    source.Sequence = 11;
    source.ProcessId = 1234;
    source.FieldCount = 5;

    auto& matrix = source.Fields[0];
    Name(matrix, "view_projection");
    matrix.Type = HV_EFI_ANALYSIS_FIELD_MATRIX4X4;
    matrix.ValueBytes = sizeof(matrix.Value.Mat4x4);
    matrix.Value.Mat4x4[0] = 1.0f;
    matrix.Value.Mat4x4[5] = 1.0f;
    matrix.Value.Mat4x4[10] = 1.0f;
    matrix.Value.Mat4x4[15] = 1.0f;

    auto& position = source.Fields[1];
    Name(position, "entity.alpha.position");
    position.Type = HV_EFI_ANALYSIS_FIELD_VECTOR3;
    position.ValueBytes = sizeof(position.Value.Vec3);

    hypersinv::overlay::RollingXorConfig xorConfig{};
    xorConfig.Key[0] = 0x10;
    xorConfig.Key[1] = 0x20;
    xorConfig.Key[2] = 0x30;
    xorConfig.KeyLength = 3;
    xorConfig.InitialState = 0x12345678u;
    xorConfig.StateStep = 0x01020304u;
    xorConfig.StateRotation = 5;
    constexpr std::array<std::uint8_t, 5> encodedName{
        0x29, 0xce, 0x8c, 0x67, 0x2b}; // rolling-XOR("Alpha")
    auto& name = source.Fields[2];
    Name(name, "entity.alpha.name");
    name.Type = HV_EFI_ANALYSIS_FIELD_ASCII_Z;
    name.ValueBytes = static_cast<std::uint32_t>(encodedName.size());
    std::memcpy(name.Value.Bytes, encodedName.data(), encodedName.size());

    auto& box = source.Fields[3];
    Name(box, "entity.alpha.box");
    box.Type = HV_EFI_ANALYSIS_FIELD_VECTOR4;
    box.ValueBytes = sizeof(box.Value.Vec4);
    box.Value.Vec4[0] = 40.0f;
    box.Value.Vec4[1] = 80.0f;

    auto& color = source.Fields[4];
    Name(color, "entity.alpha.color");
    color.Type = HV_EFI_ANALYSIS_FIELD_INT32;
    color.ValueBytes = sizeof(color.Value.I32);
    color.Value.I32 = static_cast<std::int32_t>(0xff102030u);

    HVR1_OVERLAY_ADAPTER_CONFIG config{};
    config.NameTransform.Kind = hypersinv::overlay::TransformKind::RollingXor;
    config.NameTransform.RollingXor = xorConfig;
    hypersinv::overlay::AnalysisResult frame{};
    if (!Hvr1BuildOverlayFrame(source, 0x1234, config, frame) ||
        frame.Header.EntityCount != 1 ||
        frame.Header.SourceGeneration != 7 ||
        frame.Header.SourceSequence != 11 ||
        frame.Header.TargetWindow != 0x1234 ||
        (frame.Header.SceneFlags &
            hypersinv::overlay::kSceneHasViewProjection) == 0 ||
        std::wstring(frame.Entries[0].name) != L"Alpha" ||
        frame.Entries[0].width != 40.0f ||
        frame.Entries[0].height != 80.0f ||
        frame.Entries[0].color != 0xff102030u ||
        frame.Entries[0].coordinateSpace != static_cast<std::uint32_t>(
            hypersinv::overlay::CoordinateSpace::World)) {
        return Fail("structured HVA1-to-HVO2 transformation");
    }

    float x = 0;
    float y = 0;
    if (!hypersinv::overlay::ProjectWorldToScreen(
            frame.Header.ViewProjection,
            frame.Entries[0].worldX,
            frame.Entries[0].worldY,
            frame.Entries[0].worldZ,
            800,
            600,
            x,
            y) || x != 400.0f || y != 300.0f) {
        return Fail("adapter projection");
    }

    wchar_t mappingName[128]{};
    swprintf_s(mappingName,
               L"Local\\Hvr1OverlayAdapter_%lu_%llu",
               GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
    hypersinv::overlay::SharedMemoryPublisher publisher;
    hypersinv::overlay::SharedMemoryReader reader;
    if (!publisher.Open(
            mappingName,
            0,
            hypersinv::overlay::PublisherAccess::AuthenticatedUsersRead) ||
        !reader.Open(mappingName)) {
        return Fail("publisher/reader bridge initialization");
    }
    const auto publishStarted = std::chrono::steady_clock::now();
    if (publisher.Publish(frame) !=
        hypersinv::overlay::PublishStatus::Success) {
        return Fail("publish transformed HVA1 snapshot");
    }
    hypersinv::overlay::OverlaySnapshot snapshot{};
    while (reader.Read(snapshot) !=
           hypersinv::overlay::SnapshotReadStatus::Success) {
        if (std::chrono::steady_clock::now() - publishStarted >=
            std::chrono::milliseconds(50)) {
            return Fail("HVA1-to-HVO2 read latency");
        }
        YieldProcessor();
    }
    if (snapshot.Header.SourceGeneration != source.Generation ||
        snapshot.Header.EntityCount != 1 ||
        std::wstring(snapshot.Entries[0].name) != L"Alpha" ||
        std::chrono::steady_clock::now() - publishStarted >=
            std::chrono::milliseconds(50)) {
        return Fail("authenticated result publication round trip");
    }

    HVR1_OVERLAY_SOURCE scanOnly{};
    scanOnly.Generation = 8;
    scanOnly.ResultCount = 1;
    strncpy_s(scanOnly.Results[0].MatchedSignature, "image_anchor", _TRUNCATE);
    if (!Hvr1BuildOverlayFrame(scanOnly, 0, config, frame) ||
        frame.Header.EntityCount != 1 ||
        frame.Entries[0].coordinateSpace != static_cast<std::uint32_t>(
            hypersinv::overlay::CoordinateSpace::Screen) ||
        std::wstring(frame.Entries[0].name) != L"image_anchor") {
        return Fail("scan-result fallback transformation");
    }

    std::puts("PASS: authenticated analysis response adapter");
    return 0;
}
