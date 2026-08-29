#pragma once
#include "DelayMemory.h"
#include "BiquadFilters.h"
#include "MagnitudeResponseFitter.h"
#include "AcousticMetrics.h"
#include "Saturator.h"
#include "OutputLimiter.h"
#include "OutputEQ.h"
#include <JuceHeader.h>
#include "DSPParams.h"
#include "DSPConstants.h"
#include <array>
#include <cmath>

#define AMBIENCE_USE_STAGE2_ABSORPTION 1

namespace FDNReverb {

    enum class ReverbTopology { Room, Hall, Plate, Spring, Goldfoil };

    // ═══════════════════════════════════════════════════════════════════════════
    //  BandlimitedNoiseLFO: 帯域制限ノイズ + 1次 IIR LPF
    // ═══════════════════════════════════════════════════════════════════════════
    struct BandlimitedNoiseLFO {
        uint32_t state{ 12345u };
        float    smoothed{ 0.0f };
        float    rateMultiplier{ 1.0f };

        inline float nextNoise() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float>(state) * 2.3283064365386963e-10f * 2.0f - 1.0f;
        }

        inline float tick(float lpfCoeff) noexcept {
            smoothed += (nextNoise() - smoothed) * lpfCoeff;
            return smoothed;
        }
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  ChorusLFO: ウェーブテーブル正弦波（コーラス的ピッチモジュレーション）
    //  ★ パラボラ近似を廃止 → 1024点LUT+線形補間 (THD < -96dB)
    // ═══════════════════════════════════════════════════════════════════════════
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

    class UniversalEngine {
    public:
        UniversalEngine();
        void prepare(double sampleRate, int maxBlockSize);
        void reset();
        void setParams(const DSPParams& p);
        void processBlock(const float* inL, const float* inR,
            float* outL, float* outR, int numSamples) noexcept;

        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept { return effectiveRT60; }
        float getD50() const noexcept { return acousticMetrics.getD50(); }
        float getC50() const noexcept { return acousticMetrics.getC50(); }
        float getC80() const noexcept { return acousticMetrics.getC80(); }
        float getEDT() const noexcept { return theoreticalEDT; }
        const AcousticMetrics& getAcousticMetrics() const noexcept { return acousticMetrics; }

        int   getERTapCount() const noexcept { return currentERTapCount; }
        float getERTapDelaySamples(int index) const noexcept {
            return (index >= 0 && index < currentERTapCount) ? currentERDelaySamples[index] : 0.0f;
        }
        float getERTapGain(int index) const noexcept {
            return (index >= 0 && index < currentERTapCount) ? currentERGains[index] : 0.0f;
        }
        double getSampleRate() const noexcept { return fs; }
        bool   isERBypassed()  const noexcept { return bypassER; }

    private:
        void updateTopologyAndRouting();
        void calculatePrimePowerDelays();
        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;

        // ═══ FDN ループ内マイクロサチュレーション ═══
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
        ReverbTopology  currentTopology{ ReverbTopology::Room };

        static constexpr int FDN_ORDER = 16;
        static constexpr int SERIAL_APF_STAGES = 3;

        // ★ パラメータ平滑化: updateTopologyAndRouting() の呼出頻度を制限
        int topologyUpdateCounter{ 0 };
        static constexpr int TOPOLOGY_UPDATE_INTERVAL = 64;
        bool topologyUpdatePending{ false };

        // ★ PreDelay ディレイライン (最大500ms) - L/R独立
        LinearDelayLine                              preDelayLineL;
        LinearDelayLine                              preDelayLineR;
        float                                        preDelaySamples{ 0.0f };

        LinearDelayLine                              erDelay;
        std::array<float, 16>                        erTaps;
        std::array<LinearDelayLine, 4>               inputDiffusers;
        std::array<LinearDelayLine, FDN_ORDER>        fdnDelays;  // ★ Hermite 3次補間
        std::array<std::array<LinearDelayLine, SERIAL_APF_STAGES>, FDN_ORDER> nestedAllpassDelays;

        int                            currentERTapCount{ 0 };
        std::array<float, MAX_ER_TAPS> currentERDelaySamples;
        std::array<float, MAX_ER_TAPS> currentERGains;

        OutputLimiter outputLimiter;
        OutputEQ      outputEQ;

        float duckingEnvelope{ 0.0f };
        float duckingAttackCoeff{ 0.0f };
        float duckingReleaseCoeff{ 0.0f };

#if AMBIENCE_USE_STAGE2_ABSORPTION
        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2;
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> currentAbsorptionCoeffsS2;
#else
        std::array<BiquadState, FDN_ORDER> absorptionFilters;
        std::array<BiquadCoeffs, FDN_ORDER> currentAbsorptionCoeffs;
#endif

        std::array<BandlimitedNoiseLFO, FDN_ORDER> lfos;
        std::array<ChorusLFO, FDN_ORDER>           chorusLFOs;
        std::array<float, FDN_ORDER>               fdnBaseDelaySamples;
        std::array<float, FDN_ORDER>               fbVec;

        float apfGain{ 0.618f };
        bool  bypassER{ false };
        bool  bypassInputDiffusers{ false };
        float lateMixScale{ 1.0f };
        float lateMakeupGainLinear{ 1.0f };

        float diffusionSensitivity{ 1.0f };

        float microSatBlend{ 1.0f };
        float modDepthScale{ 1.0f };
        float smoothedModAmount{ 0.0f };
        float smoothedModRate{ 1.0f };

        // ★ DCブロッカー: FDNループ内のDC蓄積を阻止
        std::array<float, FDN_ORDER> dcX1;
        std::array<float, FDN_ORDER> dcY1;
        float dcBlockerCoeff{ 0.999f };

        // ★ Soft-kneeコンプレッション: FDNフィードバックループ内
        std::array<float, FDN_ORDER> fdnRmsEnv;
        float rmsCoeff{ 0.002f };

        std::array<float, NUM_BANDS> effectiveRT60;
        float theoreticalEDT{ 0.0f };

        AcousticMetrics acousticMetrics;
        Saturator saturatorL;
        Saturator saturatorR;
    };

} // namespace FDNReverb