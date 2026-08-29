#pragma once
#include "../Source/DSP/DelayMemory.h"
#include "../Source/DSP/BiquadFilters.h"
#include "../Source/DSP/MagnitudeResponseFitter.h"
#include "../Source/DSP/AcousticMetrics.h"
#include "../Source/DSP/Saturator.h"
#include "../Source/DSP/OutputLimiter.h"
#include "../Source/DSP/OutputEQ.h"
#include "../Source/DSP/DSPParams.h"
#include "../Source/DSP/DSPConstants.h"
#include <JuceHeader.h>
#include <array>
#include <cmath>

namespace FDNReverb::V121 {

    struct ChorusLFO {
        float phase{ 0.0f };
        float phaseInc{ 0.0f };
        float rateScale{ 1.0f };

        static constexpr int TABLE_SIZE = 1024;
        static inline float sineTable[TABLE_SIZE + 1];
        static inline bool  tableInitialized = false;

        static void initTable() noexcept {
            if (tableInitialized) return;
            constexpr float twoPi = 6.28318530718f;
            for (int i = 0; i <= TABLE_SIZE; ++i)
                sineTable[i] = std::sin(twoPi * static_cast<float>(i)
                                        / static_cast<float>(TABLE_SIZE));
            tableInitialized = true;
        }

        inline float tick() noexcept {
            phase += phaseInc;
            if (phase >= 1.0f) phase -= 1.0f;
            const float idx = phase * static_cast<float>(TABLE_SIZE);
            const int   i0  = static_cast<int>(idx);
            const float frac = idx - static_cast<float>(i0);
            return sineTable[i0] + frac * (sineTable[i0 + 1] - sineTable[i0]);
        }
    };

    class UniversalEngineUpdate {
    public:
        UniversalEngineUpdate();
        void prepare(double sampleRate, int maxBlockSize);
        void reset();
        void setParams(const DSPParams& p);
        void processBlock(const float* inL, const float* inR,
            float* outL, float* outR, int numSamples) noexcept;

    private:
        void updateTopologyAndRouting();
        void calculatePrimePowerDelays();
        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;

        inline static float processMicroSaturation(float x) noexcept {
            constexpr float kInScale = 0.15f;
            constexpr float kOutScale = 1.0f / kInScale;
            const float xs = x * kInScale;
            if (xs > 3.0f) return  kOutScale;
            if (xs < -3.0f) return -kOutScale;
            const float xsq = xs * xs;
            return (xs * (27.0f + xsq) / (27.0f + 9.0f * xsq)) * kOutScale;
        }

        DelayMemoryPool memoryPool;
        double          fs{ 48000.0 };
        DSPParams       activeParams;

        static constexpr int FDN_ORDER = 16;
        static constexpr int SERIAL_APF_STAGES = 3;

        LinearDelayLine                              preDelayLineL;
        LinearDelayLine                              preDelayLineR;
        float                                        preDelaySamples{ 0.0f };

        LinearDelayLine                              erDelay;
        std::array<LinearDelayLine, 4>               inputDiffusers;
        std::array<LinearDelayLine, FDN_ORDER>        fdnDelays;
        std::array<std::array<LinearDelayLine, SERIAL_APF_STAGES>, FDN_ORDER> nestedAllpassDelays;

        int                            currentERTapCount{ 0 };
        std::array<float, MAX_ER_TAPS> currentERDelaySamples;
        std::array<float, MAX_ER_TAPS> currentERGains;

        OutputLimiter outputLimiter;
        OutputEQ      outputEQ;

        float duckingEnvelope{ 0.0f };
        float duckingAttackCoeff{ 0.0f };
        float duckingReleaseCoeff{ 0.0f };

        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2;
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> currentAbsorptionCoeffsS2;

        std::array<ChorusLFO, FDN_ORDER>           chorusLFOs;
        std::array<float, FDN_ORDER>               fdnBaseDelaySamples;
        std::array<float, FDN_ORDER>               fbVec;

        float apfGain{ 0.618f };
        bool  bypassER{ false };
        bool  bypassInputDiffusers{ false };
        float diffusionSensitivity{ 1.0f };
        float microSatBlend{ 1.0f };
        float modDepthScale{ 1.0f };
        float smoothedModAmount{ 0.0f };

        std::array<float, FDN_ORDER> dcX1;
        std::array<float, FDN_ORDER> dcY1;
        float dcBlockerCoeff{ 0.999f };

        std::array<float, FDN_ORDER> fdnRmsEnv;
        float rmsCoeff{ 0.002f };

        std::array<float, NUM_BANDS> effectiveRT60;
        float theoreticalEDT{ 0.0f };

        AcousticMetrics acousticMetrics;
        Saturator saturatorL;
        Saturator saturatorR;
    };

} // namespace FDNReverb::V121
