#pragma once
#include "DSPConstants.h"
#include "../AlgorithmPresets.h"
#include <array>

namespace FDNReverb {
    // ─────────────────────────────────────────────────────────────────────────────
    //  Biquad helpers (Direct Form II Transposed — most robust)
    // ─────────────────────────────────────────────────────────────────────────────
    struct BiquadCoeffs {
        float b0{ 1.f }, b1{ 0.f }, b2{ 0.f };
        float            a1{ 0.f }, a2{ 0.f };
    };

    struct BiquadState {
        double s1{ 0.0 }, s2{ 0.0 };  // ★ 倍精度化: 低域フィルタの量子化ノイズ防止
        inline float tick(float x, const BiquadCoeffs& c) noexcept {
            const double xd = static_cast<double>(x);
            const double yd = static_cast<double>(c.b0) * xd + s1;
            s1 = static_cast<double>(c.b1) * xd - static_cast<double>(c.a1) * yd + s2;
            s2 = static_cast<double>(c.b2) * xd - static_cast<double>(c.a2) * yd;
            return static_cast<float>(yd);
        }
        void reset() noexcept { s1 = s2 = 0.0; }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Filter design utilities
    // ─────────────────────────────────────────────────────────────────────────────
    namespace FilterDesign {
        BiquadCoeffs lowShelf(float fcHz, float gainDB, double sampleRate);
        BiquadCoeffs highShelf(float fcHz, float gainDB, double sampleRate);
        BiquadCoeffs peak(float fcHz, float gainDB, float Q, double sampleRate);
        BiquadCoeffs highPass1st(float fcHz, double sampleRate);
        BiquadCoeffs allpass1st(float fcHz, double sampleRate);

        // Design absorption filter cascade for delay lines
        // 注: この関数は内部で MagnitudeResponseFitter を呼び出すラッパーになりました
        std::array<BiquadCoeffs, ABSO_STAGES> designAbsorption(
            int delaySamples, double sampleRate,
            const std::array<float, NUM_BANDS>& rt60,
            float hfDamping, float lfAbsorption);
    }
} // namespace FDNReverb