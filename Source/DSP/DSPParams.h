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

        // ── OutEQ 拡張 ──
        int   loEQType{ 0 };    // 0: Off, 1: Cut, 2: Shelf (デフォルト 0 = Off)
        int   hiEQType{ 0 };    // 0: Off, 1: Cut, 2: Shelf (デフォルト 0 = Off)
        float loGainDB{ 0.0f }; // -12 ~ +12 dB
        float hiGainDB{ 0.0f }; // -12 ~ +12 dB

        // ── PRO Tab 物理音響パラメータ ──
        float scattering{ 0.5f };       // 0.0 ~ 1.0 (デフォルト 0.5)
        float erCrossoverMs{ 40.0f };   // 10.0 ~ 100.0 ms (デフォルト 40.0ms)
        float lateDensity{ 0.7f };     // 0.0 ~ 1.0 (デフォルト 0.7)
        float asymmetry{ 0.3f };       // 0.0 ~ 1.0 (デフォルト 0.3)
        float clarityDB{ 0.0f };       // -6.0 ~ +6.0 dB (デフォルト 0.0dB)
        float airAbsorbScale{ 1.0f };  // 0.2 ~ 2.5 x (デフォルト 1.0x)

        // ── 排他的タブ状態 ──
        bool rt60Tab{ false };
        bool proTab{ false };

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
                && hiCutHz == o.hiCutHz
                && loEQType == o.loEQType
                && hiEQType == o.hiEQType
                && loGainDB == o.loGainDB
                && hiGainDB == o.hiGainDB
                && scattering == o.scattering
                && erCrossoverMs == o.erCrossoverMs
                && lateDensity == o.lateDensity
                && asymmetry == o.asymmetry
                && clarityDB == o.clarityDB
                && airAbsorbScale == o.airAbsorbScale
                && rt60Tab == o.rt60Tab
                && proTab == o.proTab;
        }
        bool operator!=(const DSPParams& o) const noexcept { return !(*this == o); }
    };

} // namespace FDNReverb