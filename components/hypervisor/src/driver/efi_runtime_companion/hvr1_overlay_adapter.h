#ifndef HVR1_OVERLAY_ADAPTER_H
#define HVR1_OVERLAY_ADAPTER_H

#include "data_transform.h"
#include "overlay_protocol.h"
#include "runtime_analysis_abi.h"

#include <cstdint>

struct HVR1_OVERLAY_SOURCE {
    std::uint64_t Generation;
    std::uint64_t Sequence;
    std::uint64_t ProcessId;
    std::uint32_t FieldCount;
    std::uint32_t ResultCount;
    HV_EFI_ANALYSIS_EXTRACTED_FIELD Fields[HV_EFI_ANALYSIS_MAX_FIELDS];
    HV_EFI_ANALYSIS_SCAN_RESULT Results[HV_EFI_ANALYSIS_MAX_RESULTS];
};

struct HVR1_OVERLAY_ADAPTER_CONFIG {
    hypersinv::overlay::TransformConfig NameTransform{};
    hypersinv::overlay::TextEncoding NameEncoding =
        hypersinv::overlay::TextEncoding::Ascii;
    float DefaultWidth = 80.0f;
    float DefaultHeight = 40.0f;
    std::uint32_t DefaultColor = 0xff33cc66u;
};

bool Hvr1BuildOverlayFrame(
    const HVR1_OVERLAY_SOURCE& source,
    std::uint64_t targetWindow,
    const HVR1_OVERLAY_ADAPTER_CONFIG& config,
    hypersinv::overlay::AnalysisResult& frame);

#endif /* HVR1_OVERLAY_ADAPTER_H */
