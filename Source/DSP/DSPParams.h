#pragma once
#include <array>

namespace FDNReverb {

    struct DSPParams {
        int   algorithmIndex{ 0 };
        float decayScale{ 1.0f };
        float roomSizeScale{ 1.0f };
        float hfDamping{ 0.0f };
        float lfAbsorption{ 0.0f };
        float diffusion{ 0.70f };
        float preDelayMs{ 10.0f };
        float modAmount{ 0.25f };
        float modRate{ 0.5f };
        float stereoWidth{ 0.95f };
        float erLevel{ 0.6f };
        float lateLevel{ 1.0f };
        float wetDB{ -4.0f };
        float dryDB{ 0.0f };
        float saturation{ 0.0f };
        int   satTypeIdx{ 0 };
        float duckingAmount{ 0.0f };
        float duckingAttackMs{ 10.0f };
        float duckingRelMs{ 200.0f };
        float duckingThreshDB{ -20.0f };
        bool  erSolo{ false };
        bool  proMode{ false };
        float tiltLow{ 1.0f };
        float tiltMid{ 1.0f };
        float tiltHigh{ 1.0f };
        std::array<float, 10> rtBands{ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f } };
        float loCutHz{ 20.0f };
        float hiCutHz{ 20000.0f };

        bool operator==(const DSPParams& o) const noexcept {
            return algorithmIndex == o.algorithmIndex
                && decayScale == o.decayScale
                && roomSizeScale == o.roomSizeScale
                && hfDamping == o.hfDamping
                && lfAbsorption == o.lfAbsorption
                && diffusion == o.diffusion
                && preDelayMs == o.preDelayMs
                && modAmount == o.modAmount
                && modRate == o.modRate
                && stereoWidth == o.stereoWidth
                && erLevel == o.erLevel
                && lateLevel == o.lateLevel
                && wetDB == o.wetDB
                && dryDB == o.dryDB
                && saturation == o.saturation
                && satTypeIdx == o.satTypeIdx
                && duckingAmount == o.duckingAmount
                && duckingAttackMs == o.duckingAttackMs
                && duckingRelMs == o.duckingRelMs
                && duckingThreshDB == o.duckingThreshDB
                && erSolo == o.erSolo
                && proMode == o.proMode
                && tiltLow == o.tiltLow
                && tiltMid == o.tiltMid
                && tiltHigh == o.tiltHigh
                && rtBands == o.rtBands
                && loCutHz == o.loCutHz
                && hiCutHz == o.hiCutHz;
        }
        bool operator!=(const DSPParams& o) const noexcept { return !(*this == o); }
    };

} // namespace FDNReverb