#include "hvr1_overlay_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace {

using hypersinv::overlay::AnalysisResult;
using hypersinv::overlay::CoordinateSpace;
using hypersinv::overlay::EntityData;

struct EntityBuilder {
    std::string Id;
    EntityData Entry{};
    bool HasPosition = false;
    bool HasName = false;
};

std::string BoundedName(const char value[HV_EFI_ANALYSIS_IDENTIFIER_BYTES]) {
    std::size_t length = 0;
    while (length < HV_EFI_ANALYSIS_IDENTIFIER_BYTES && value[length] != '\0') {
        ++length;
    }
    return std::string(value, length);
}

bool IsFinitePositive(float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

void CopyWideName(const std::wstring& value, wchar_t output[32]) noexcept {
    std::memset(output, 0, sizeof(wchar_t) * 32);
    const std::size_t count = (std::min)(value.size(), std::size_t{31});
    if (count != 0) {
        std::memcpy(output, value.data(), count * sizeof(wchar_t));
    }
}

std::wstring WidenAscii(const std::string& value) {
    std::wstring result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        result += character >= 0x20u && character <= 0x7eu
            ? static_cast<wchar_t>(character)
            : L'_';
    }
    return result;
}

bool ParseEntityField(const std::string& name,
                      std::uint32_t type,
                      std::string& id,
                      std::string& property) {
    constexpr char prefix[] = "entity.";
    if (name.compare(0, sizeof(prefix) - 1, prefix) == 0) {
        const std::size_t separator = name.rfind('.');
        if (separator == std::string::npos ||
            separator <= sizeof(prefix) - 1 || separator + 1 >= name.size()) {
            return false;
        }
        id = name.substr(sizeof(prefix) - 1,
                         separator - (sizeof(prefix) - 1));
        property = name.substr(separator + 1);
        return !id.empty() && id.size() <= 20;
    }
    if (type == HV_EFI_ANALYSIS_FIELD_VECTOR3 && !name.empty()) {
        id = name;
        property = "position";
        return true;
    }
    return false;
}

EntityBuilder* FindOrAdd(EntityBuilder* builders,
                         std::uint32_t& count,
                         const std::string& id) noexcept {
    for (std::uint32_t index = 0; index < count; ++index) {
        if (builders[index].Id == id) {
            return &builders[index];
        }
    }
    if (count == hypersinv::overlay::kMaximumEntities) {
        return nullptr;
    }
    EntityBuilder& builder = builders[count++];
    try {
        builder.Id = id;
    } catch (...) {
        --count;
        return nullptr;
    }
    builder.Entry.coordinateSpace = static_cast<std::uint32_t>(
        CoordinateSpace::World);
    builder.Entry.isVisible = 1;
    return &builder;
}

bool IsStringField(std::uint32_t type) noexcept {
    return type == HV_EFI_ANALYSIS_FIELD_ASCII_Z ||
           type == HV_EFI_ANALYSIS_FIELD_UTF8_Z ||
           type == HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED8 ||
           type == HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED16 ||
           type == HV_EFI_ANALYSIS_FIELD_LENGTH_PREFIXED32;
}

} // namespace

bool Hvr1BuildOverlayFrame(const HVR1_OVERLAY_SOURCE& source,
                           std::uint64_t targetWindow,
                           const HVR1_OVERLAY_ADAPTER_CONFIG& config,
                           AnalysisResult& frame) {
    if (source.Generation == 0 ||
        source.FieldCount > HV_EFI_ANALYSIS_MAX_FIELDS ||
        source.ResultCount > HV_EFI_ANALYSIS_MAX_RESULTS ||
        !IsFinitePositive(config.DefaultWidth) ||
        !IsFinitePositive(config.DefaultHeight)) {
        return false;
    }

    try {
    frame = {};
    frame.Header.Magic = hypersinv::overlay::kOverlayMagic;
    frame.Header.Version = hypersinv::overlay::kOverlayVersion;
    frame.Header.HeaderSize = sizeof(hypersinv::overlay::SharedOverlayHeader);
    frame.Header.EntityStride = sizeof(EntityData);
    frame.Header.Capacity = static_cast<std::uint32_t>(
        hypersinv::overlay::kMaximumEntities);
    frame.Header.TargetWindow = targetWindow;
    frame.Header.SourceGeneration = source.Generation;
    frame.Header.SourceSequence = source.Sequence;

    EntityBuilder builders[hypersinv::overlay::kMaximumEntities]{};
    std::uint32_t builderCount = 0;
    for (std::uint32_t index = 0; index < source.FieldCount; ++index) {
        const HV_EFI_ANALYSIS_EXTRACTED_FIELD& field = source.Fields[index];
        if (field.ValueBytes > sizeof(field.Value.Bytes)) {
            return false;
        }
        const std::string name = BoundedName(field.FieldName);
        if ((name == "view_projection" || name == "camera.matrix") &&
            field.Type == HV_EFI_ANALYSIS_FIELD_MATRIX4X4 &&
            field.ValueBytes == sizeof(field.Value.Mat4x4)) {
            std::memcpy(frame.Header.ViewProjection,
                        field.Value.Mat4x4,
                        sizeof(frame.Header.ViewProjection));
            frame.Header.SceneFlags |=
                hypersinv::overlay::kSceneHasViewProjection;
            continue;
        }

        std::string id;
        std::string property;
        if (!ParseEntityField(name, field.Type, id, property)) {
            continue;
        }
        EntityBuilder* builder = FindOrAdd(builders, builderCount, id);
        if (builder == nullptr) {
            return false;
        }

        if (property == "position" &&
            field.Type == HV_EFI_ANALYSIS_FIELD_VECTOR3 &&
            field.ValueBytes == sizeof(field.Value.Vec3)) {
            builder->Entry.worldX = field.Value.Vec3[0];
            builder->Entry.worldY = field.Value.Vec3[1];
            builder->Entry.worldZ = field.Value.Vec3[2];
            builder->HasPosition = std::isfinite(builder->Entry.worldX) &&
                std::isfinite(builder->Entry.worldY) &&
                std::isfinite(builder->Entry.worldZ);
        } else if (property == "name" && IsStringField(field.Type)) {
            std::wstring transformed;
            if (!hypersinv::overlay::TransformToText(
                    field.Value.Bytes,
                    field.ValueBytes,
                    config.NameTransform,
                    config.NameEncoding,
                    transformed)) {
                return false;
            }
            CopyWideName(transformed, builder->Entry.name);
            builder->HasName = !transformed.empty();
        } else if (property == "box" &&
                   field.Type == HV_EFI_ANALYSIS_FIELD_VECTOR4 &&
                   field.ValueBytes == sizeof(field.Value.Vec4)) {
            if (IsFinitePositive(field.Value.Vec4[0]) &&
                IsFinitePositive(field.Value.Vec4[1])) {
                builder->Entry.width = field.Value.Vec4[0];
                builder->Entry.height = field.Value.Vec4[1];
            }
        } else if (property == "color" &&
                   field.Type == HV_EFI_ANALYSIS_FIELD_INT32 &&
                   field.ValueBytes == sizeof(field.Value.I32)) {
            builder->Entry.color = static_cast<std::uint32_t>(field.Value.I32);
        } else if (property == "visible" &&
                   field.Type == HV_EFI_ANALYSIS_FIELD_INT32 &&
                   field.ValueBytes == sizeof(field.Value.I32)) {
            builder->Entry.isVisible = field.Value.I32 != 0 ? 1u : 0u;
        }
    }

    for (std::uint32_t index = 0; index < builderCount; ++index) {
        EntityBuilder& builder = builders[index];
        if (!builder.HasPosition ||
            frame.Header.EntityCount == hypersinv::overlay::kMaximumEntities) {
            continue;
        }
        if (!builder.HasName) {
            CopyWideName(WidenAscii(builder.Id), builder.Entry.name);
        }
        if (!IsFinitePositive(builder.Entry.width)) {
            builder.Entry.width = config.DefaultWidth;
        }
        if (!IsFinitePositive(builder.Entry.height)) {
            builder.Entry.height = config.DefaultHeight;
        }
        if (builder.Entry.color == 0) {
            builder.Entry.color = config.DefaultColor;
        }
        frame.Entries[frame.Header.EntityCount++] = builder.Entry;
    }

    // Scan-only sessions still produce useful diagnostic visualization without
    // pretending that a physical match is a world-space entity.
    if (frame.Header.EntityCount == 0) {
        const std::uint32_t count = (std::min)(
            source.ResultCount,
            static_cast<std::uint32_t>(hypersinv::overlay::kMaximumEntities));
        for (std::uint32_t index = 0; index < count; ++index) {
            const HV_EFI_ANALYSIS_SCAN_RESULT& result = source.Results[index];
            EntityData& entry = frame.Entries[frame.Header.EntityCount++];
            entry.screenX = 20.0f;
            entry.screenY = 30.0f + static_cast<float>(index) * 48.0f;
            entry.width = 320.0f;
            entry.height = 36.0f;
            entry.coordinateSpace = static_cast<std::uint32_t>(
                CoordinateSpace::Screen);
            entry.color = config.DefaultColor;
            entry.isVisible = 1;
            CopyWideName(WidenAscii(BoundedName(result.MatchedSignature)),
                         entry.name);
        }
    }

    return hypersinv::overlay::IsHeaderValid(frame.Header);
    } catch (...) {
        frame = {};
        return false;
    }
}
