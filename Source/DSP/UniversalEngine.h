#pragma once

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <memory>
#include <algorithm>
#include <vector>

#include "DSPConstants.h"
#include "DelayMemory.h"
#include "DSPParams.h"
#include "BiquadFilters.h"
#include "MagnitudeResponseFitter.h"
#include "AcousticMetrics.h"
#include "Saturator.h"
#include "OutputLimiter.h"
#include "OutputEQ.h"
#include "SDNEngine.h"
#include "SDNTopology2DMesh.h"
#include "SDNTopologySpring1D.h"
#include "SDNTopologyInchindown.h"
#include "DynamicEQDucker.h"
#include "EarlyReflectionsISM.h"

#define AMBIENCE_USE_STAGE2_ABSORPTION 1

namespace FDNReverb {

    enum class ReverbTopology {
        Room,
        Hall,
        Plate,
        Spring,
        Goldfoil,
        Inchindown
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  非同期デュアル黄金比 LFO (Dual Incommensurate LFO)
    // ═══════════════════════════════════════════════════════════════════════════
    struct DualGoldenLFO {
        float phase1{ 0.0f };
        float phase2{ 0.0f };
        float phaseInc1{ 0.0f };
        float phaseInc2{ 0.0f };
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
            phase1 += phaseInc1;
            if (phase1 >= 1.0f) phase1 -= 1.0f;
            phase2 += phaseInc2;
            if (phase2 >= 1.0f) phase2 -= 1.0f;

            const float idx1 = phase1 * static_cast<float>(TABLE_SIZE);
            const int   i0_1 = static_cast<int>(idx1);
            const float frac1 = idx1 - static_cast<float>(i0_1);
            const float s1 = sineTable[i0_1] + frac1 * (sineTable[i0_1 + 1] - sineTable[i0_1]);

            const float idx2 = phase2 * static_cast<float>(TABLE_SIZE);
            const int   i0_2 = static_cast<int>(idx2);
            const float frac2 = idx2 - static_cast<float>(i0_2);
            const float s2 = sineTable[i0_2] + frac2 * (sineTable[i0_2 + 1] - sineTable[i0_2]);

            // 75% 主周期 + 25% 黄金比副周期 (周期的一致が物理的にゼロ)
            return 0.75f * s1 + 0.25f * s2;
        }
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  SimpleAllpass: 出力段ステレオ・オールパス・ディフューザー
    //  (振幅着色ゼロ・位相直交化によるステレオ相関低減・IACC 最適化)
    // ═══════════════════════════════════════════════════════════════════════════
    template <size_t DelayLen>
    class SimpleAllpass {
    public:
        void reset() noexcept {
            buffer.fill(0.0f);
            writeIdx = 0;
        }
        inline float process(float in, float g) noexcept {
            const float delayed = buffer[writeIdx];
            const float v = in - g * delayed;
            buffer[writeIdx] = v;
            if (++writeIdx >= DelayLen) writeIdx = 0;
            return delayed + g * v;
        }
    private:
        std::array<float, DelayLen> buffer{};
        size_t writeIdx{ 0 };
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  UniversalEngine (V1.2.1 B010)
    // ═══════════════════════════════════════════════════════════════════════════
    class UniversalEngine {
    public:
        UniversalEngine();
        ~UniversalEngine() = default;

        void prepare(double sampleRate, int maxBlockSize);
        void reset();
        void setParams(const DSPParams& p);
        void processBlock(const float* inL, const float* inR,
            float* outL, float* outR, int numSamples) noexcept;

        void panicReset() noexcept;

        std::array<float, NUM_BANDS> getEffectiveRT60() const noexcept {
            return effectiveRT60;
        }
        std::array<float, NUM_BANDS> getTargetRT60() const noexcept {
            return targetRT60;
        }
        float getD50() const noexcept { return acousticMetrics.getD50(); }
        float getC50() const noexcept { return acousticMetrics.getC50(); }
        float getC80() const noexcept { return acousticMetrics.getC80(); }
        float getEDT() const noexcept { return theoreticalEDT; }

        double getSampleRate() const noexcept { return fs; }
        bool   isERBypassed()  const noexcept { return bypassER; }
        int    getERTapCount() const noexcept { return currentERTapCount; }
        float  getERTapDelaySamples(int idx) const noexcept {
            return (idx >= 0 && idx < currentERTapCount) ? currentERDelaySamples[idx] : 0.0f;
        }
        float  getERTapGain(int idx) const noexcept {
            return (idx >= 0 && idx < currentERTapCount) ? currentERGains[idx] : 0.0f;
        }
        bool   isPrepared() const noexcept { return isPreparedFlag; }
        const OutputEQ& getOutputEQ() const noexcept { return outputEQ; }
        float getDuckingReductionDB() const noexcept { return dynamicDucker.getCurrentReductionDB(); }

    private:
        void updateTopologyAndRouting();
        void calculatePrimePowerDelays();
        void updateAbsorptionFilters(bool targetIsB);
        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;



        DelayMemoryPool memoryPool;
        double          fs{ 48000.0 };
        DSPParams       activeParams;
        ReverbTopology  currentTopology{ ReverbTopology::Room };

        static constexpr int FDN_ORDER = 16;
        static constexpr int SERIAL_APF_STAGES = 3;

        bool topologyUpdatePending{ false };
        bool absorptionUpdatePending{ false };
        int  samplesSinceLastAbsorptionUpdate{ 0 };
        int  absorptionRateLimitIntervalSamples{ 720 };

        LinearDelayLine                              preDelayLineL;
        LinearDelayLine                              preDelayLineR;
        float                                        preDelaySamples{ 0.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> preDelaySmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> roomSizeSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> diffuserGainSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> apfGainStageSmoothed;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stereoWidthSmoothed;

        // ★ 入力段 Bandwidth LPF ＆ 過渡平滑化 (アタックの過剰入力を防ぎコムフィルタリングを防止)
        float inLpfStateL{ 0.0f };
        float inLpfStateR{ 0.0f };
        float inBandwidthCoeff{ 0.0f };
        // ★ ER Solo / Send Mode 0〜5ms コムフィルター防止オフセット用バッファ (192kHz でも 10.6ms をカバー)
        alignas(32) std::array<float, 2048>          erOffsetDelayL{};
        alignas(32) std::array<float, 2048>          erOffsetDelayR{};
        size_t                                       erOffsetWriteIdx{ 0 };

        // Legacy ER taps replaced by SDN Core
        SDNShoebox3D                                 sdnEngine;
        SDNTopology2DMesh                            plateMesh;
        SDNTopologySpring1D                          springChain;
        SDNTopologyInchindown                        inchindownEngine;

        std::array<LinearDelayLine, 4>               inputDiffusersM;
        std::array<LinearDelayLine, 4>               inputDiffusersS;
        std::array<LinearDelayLine, FDN_ORDER>        fdnDelays;
        std::array<std::array<LinearDelayLine, SERIAL_APF_STAGES>, FDN_ORDER> nestedAllpassDelays;

        int                            currentERTapCount{ 0 };
        std::array<float, MAX_ER_TAPS> currentERDelaySamples;
        std::array<float, MAX_ER_TAPS> currentERGains;

        OutputLimiter outputLimiter;
        OutputEQ      outputEQ;

        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2_A{};
        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2_B{};
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> absorptionCoeffsS2_A{};
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> absorptionCoeffsS2_B{};
        
        // ★ 常時デュアル駆動・ビジー保護クロスフェード・ステートマシン
        enum class AbsoFadeState {
            IdleAtA,    // Bank A 100% (pos = 0.0)
            FadingToB,  // Bank A -> Bank B 移動中 (pos: 0.0 -> 1.0)
            IdleAtB,    // Bank B 100% (pos = 1.0)
            FadingToA   // Bank B -> Bank A 移動中 (pos: 1.0 -> 0.0)
        };
        AbsoFadeState absoFadeState{ AbsoFadeState::IdleAtA };
        float absoCrossfadePos{ 0.0f }; // 0.0f (100% A) 〜 1.0f (100% B)
        float absoCrossfadeInc{ 0.0f }; // 30ms クロスフェードの 1 サンプル増分

        std::array<DualGoldenLFO, FDN_ORDER>       dualLFOs{};
        std::array<float, FDN_ORDER>               fdnBaseDelaySamples{};
        std::array<float, FDN_ORDER>               currentFdnDelaySamples{};
        float                                      delaySmoothCoeff{ 0.0f };
        std::array<float, FDN_ORDER>               fbVec{};

        float apfGain{ 0.618f };
        bool  bypassER{ false };
        bool  bypassInputDiffusers{ false };
        float lateMixScale{ 1.0f };
        float lateMakeupGainLinear{ 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lateMakeupGainSmoothed;

        // ★ Wet-Only Graceful Mute State Machine (アルゴリズム切り替えノイズ根絶 ＆ Panic連動)
        enum class TransitionState {
            Normal,
            FadingOut,
            MutedAndReinit,
            FadingIn
        };
        TransitionState transitionState{ TransitionState::Normal };
        ReverbTopology  pendingTopology{ ReverbTopology::Room };
        DSPParams       pendingParams{};
        float           transitionGain{ 1.0f };
        int             transitionSamplesTotal{ 384 }; // 8ms at 48kHz
        int             transitionSampleCount{ 0 };

        // ★ Graceful Bypass: ER スムーズゲイン
        float erSmoothedGain{ 1.0f };
        float erSmoothCoeff{ 0.0f };

        float diffusionSensitivity{ 1.0f };
        float modDepthScale{ 1.0f };
        float smoothedModAmount{ 0.0f };
        float smoothedModRate{ 0.5f };

        // ★ FDN ループ内 ユニタリ・エネルギー正規化 AGC パラメータ
        float loopEnergyEnv{ 0.0f };
        float loopAttackCoeff{ 0.0f };
        float loopReleaseCoeff{ 0.0f };

        // ★ ループ不変量事前計算キャッシュ (Hot Loop CPU 最適化)
        std::array<float, FDN_ORDER> cachedFreqModScales{};
        std::array<float, 4> cachedDiffuserDelaySmpM{};
        std::array<float, 4> cachedDiffuserDelaySmpS{};
        std::array<std::array<float, SERIAL_APF_STAGES>, FDN_ORDER> cachedApfBaseDelaySmp{};
        std::array<std::array<int, SERIAL_APF_STAGES>, FDN_ORDER> cachedApfBaseDelayInt{};
        std::array<float, FDN_ORDER> dualLfoIncScale1{};
        std::array<float, FDN_ORDER> dualLfoIncScale2{};

        std::array<float, FDN_ORDER> dcX1{};
        std::array<float, FDN_ORDER> dcY1{};
        float dcBlockerCoeff{ 0.999f };

        std::array<float, NUM_BANDS> effectiveRT60{};
        std::array<float, NUM_BANDS> targetRT60{};
        float theoreticalEDT{ 0.0f };
        float currentRT60Mid{ 1.5f };

        AcousticMetrics acousticMetrics;
        Saturator saturatorL;
        Saturator saturatorR;
        DynamicEQDucker dynamicDucker;
        EarlyReflectionsISM ismEngine;
        std::vector<float> ismBufferL;
        std::vector<float> ismBufferR;

        MagnitudeResponseFitter fitter;
        bool isPreparedFlag{ false };

        // ★ 出力段ステレオ・オールパス・ディフューザー (音色着色ゼロ・素数ディレイ3段カスケード)
        SimpleAllpass<41>  outApL1;
        SimpleAllpass<73>  outApL2;
        SimpleAllpass<109> outApL3;
        SimpleAllpass<53>  outApR1;
        SimpleAllpass<89>  outApR2;
        SimpleAllpass<127> outApR3;
    };

} // namespace FDNReverb