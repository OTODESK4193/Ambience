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
            writeIdx = (writeIdx + 1) % DelayLen;
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
        inline void fastWalshHadamardTransform(std::array<float, 16>& v) noexcept;
        inline void applySignFlipping(std::array<float, 16>& v) noexcept;



        DelayMemoryPool memoryPool;
        double          fs{ 48000.0 };
        DSPParams       activeParams;
        ReverbTopology  currentTopology{ ReverbTopology::Room };

        static constexpr int FDN_ORDER = 16;
        static constexpr int SERIAL_APF_STAGES = 3;

        int topologyUpdateCounter{ 0 };
        static constexpr int TOPOLOGY_UPDATE_INTERVAL = 64;
        bool topologyUpdatePending{ false };

        LinearDelayLine                              preDelayLineL;
        LinearDelayLine                              preDelayLineR;
        float                                        preDelaySamples{ 0.0f };

        // ★ 入力段 Bandwidth LPF ＆ 過渡平滑化 (アタックの過剰入力を防ぎコムフィルタリングを防止)
        float inLpfStateL{ 0.0f };
        float inLpfStateR{ 0.0f };
        float inBandwidthCoeff{ 0.0f };
        float inputTransientEnvFast{ 0.0f };
        float inputTransientEnvSlow{ 0.0f };

        LinearDelayLine                              erDelay;
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
        std::array<float, MAX_ER_TAPS> currentERPanL;
        std::array<float, MAX_ER_TAPS> currentERPanR;
        std::array<float, MAX_ER_TAPS> currentERLpfCoeff;
        std::array<float, MAX_ER_TAPS> erLpfState;

        OutputLimiter outputLimiter;
        OutputEQ      outputEQ;

        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2_A{};
        std::array<std::array<BiquadState, ABSO_STAGES_S2>, FDN_ORDER> absorptionFiltersS2_B{};
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> absorptionCoeffsS2_A{};
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> absorptionCoeffsS2_B{};
        
        float absoCrossfadePos{ 1.0f };
        float absoCrossfadeInc{ 0.0f };
        bool useAbsoStateA{ true };

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
        std::vector<float> ismSeedBuffer;

        MagnitudeResponseFitter fitter;
        bool isPreparedFlag{ false };

        // ★ 出力段ステレオ・オールパス・ディフューザー (音色着色ゼロ・素数ディレイ)
        SimpleAllpass<37>  outApL1;
        SimpleAllpass<61>  outApL2;
        SimpleAllpass<71>  outApR1;
        SimpleAllpass<103> outApR2;
    };

} // namespace FDNReverb