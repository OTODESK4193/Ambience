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
#include <array>
#include <cmath>

#define AMBIENCE_USE_STAGE2_ABSORPTION 1

namespace FDNReverb {

    enum class ReverbTopology { Room, Hall, Plate, Spring, Goldfoil };

    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    //  BandlimitedNoiseLFO: 逋ｽ濶ｲ繝弱う繧ｺ + 1谺｡ IIR LPF
    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
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

    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    //  ChorusLFO: 繧ｦ繧ｧ繝ｼ繝悶ユ繝ｼ繝悶Ν豁｣蠑ｦ豕｢・医さ繝ｼ繝ｩ繧ｹ蝙九ヴ繝・メ繝｢繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ・・
    //  笘・繝代Λ繝懊Λ霑台ｼｼ繧貞ｻ・ｭ｢ 竊・1024轤ｹLUT+邱壼ｽ｢陬憺俣 (THD < -96dB)
    //    繝代Λ繝懊Λ縺ｯ鬆らせ縺ｫ莠碁嚴蠕ｮ蛻・ｸ埼｣邯壽ｧ縺後≠繧翫：M螟芽ｪｿ繝弱う繧ｺ・磯≡螻樒噪髻ｿ縺搾ｼ峨・蜴溷屏縲・
    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    struct ChorusLFO {
        float phase{ 0.0f };
        float phaseInc{ 0.0f };
        float rateScale{ 1.0f };   // 繝√Ε繝ｳ繝阪Ν蝗ｺ譛峨・繝ｬ繝ｼ繝井ｿよ焚・磯ｻ・≡豈泌・蟶・ｼ・

        static constexpr int TABLE_SIZE = 1024;
        static inline float sineTable[TABLE_SIZE + 1];  // +1: 陬憺俣繧ｬ繝ｼ繝峨・繧､繝ｳ繝・
        static inline bool  tableInitialized = false;

        static void initTable() noexcept {
            if (tableInitialized) return;
            constexpr float twoPi = 6.28318530718f;
            for (int i = 0; i <= TABLE_SIZE; ++i)
                sineTable[i] = std::sin(twoPi * static_cast<float>(i)
                                        / static_cast<float>(TABLE_SIZE));
            tableInitialized = true;
        }

        // 笘・繧ｦ繧ｧ繝ｼ繝悶ユ繝ｼ繝悶Ν + 邱壼ｽ｢陬憺俣: CPU繧ｳ繧ｹ繝医・繝代Λ繝懊Λ縺ｨ蜷檎ｭ峨ゝHD縺ｯ邏・00蛟肴隼蝟・
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

        // 笏笏笏 FDN 繝ｫ繝ｼ繝怜・繝槭う繧ｯ繝ｭ繧ｵ繝√Η繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ 笏笏笏
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
        static constexpr int SERIAL_APF_STAGES = 3;  // 笘・繧ｷ繝ｪ繧｢繝ｫAllpass繝√ぉ繝ｼ繝ｳ谿ｵ謨ｰ

        // 笘・PreDelay 繝・ぅ繝ｬ繧､繝ｩ繧､繝ｳ (譛螟ｧ500ms)
        LinearDelayLine                              preDelayLine;
        float                                        preDelaySamples{ 0.0f };

        LinearDelayLine                              erDelay;
        std::array<float, 16>                        erTaps;
        std::array<LinearDelayLine, 4>               inputDiffusers;
        std::array<LinearDelayLine, FDN_ORDER>        fdnDelays;  // 笘・Thiran allpass陬憺俣
        std::array<std::array<LinearDelayLine, SERIAL_APF_STAGES>, FDN_ORDER> nestedAllpassDelays;

        int                            currentERTapCount{ 0 };
        std::array<float, MAX_ER_TAPS> currentERDelaySamples;
        std::array<float, MAX_ER_TAPS> currentERGains;

        OutputLimiter outputLimiter;
        OutputEQ      outputEQ;        // 笘・Phase 5 霑ｽ蜉

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
        std::array<ChorusLFO, FDN_ORDER>           chorusLFOs;  // 笘・繧ｳ繝ｼ繝ｩ繧ｹ蝙九ヴ繝・メ繝｢繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ
        std::array<float, FDN_ORDER>               fdnBaseDelaySamples;
        std::array<float, FDN_ORDER>               fbVec;

        float apfGain{ 0.618f };
        bool  bypassER{ false };
        bool  bypassInputDiffusers{ false };  // 笘・繝・ヵ繧ｩ繝ｫ繝医ｒ false 縺ｫ
        float lateMixScale{ 1.0f };
        float lateMakeupGainLinear{ 1.0f };

        // 笘・Phase 5 霑ｽ蜉: 繧｢繝ｫ繧ｴ繝ｪ繧ｺ繝蛻･ Diffusion 諢溷ｺｦ
        float diffusionSensitivity{ 1.0f };

        // 笘・驥大ｱ樣浹蟇ｾ遲・ DecayTime 萓晏ｭ倥・繝代Λ繝｡繝ｼ繧ｿ
        float microSatBlend{ 1.0f };   // FDN繝ｫ繝ｼ繝怜・繝槭う繧ｯ繝ｭ繧ｵ繝√Η繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ縺ｮ驕ｩ逕ｨ驥・(0=繝舌う繝代せ, 1=繝輔Ν)
        float modDepthScale{ 1.0f };
        float smoothedModAmount{ 0.0f };   // 繝｢繧ｸ繝･繝ｬ繝ｼ繧ｷ繝ｧ繝ｳ豺ｱ縺輔・繧ｹ繧ｱ繝ｼ繝ｪ繝ｳ繧ｰ (髟ｷ縺Дecay縺ｧ蠅怜刈)

        // 笘・DC繝悶Ο繝・き繝ｼ: FDN繝ｫ繝ｼ繝怜・縺ｮDC闢・ｩ阪ｒ髦ｲ豁｢
        std::array<float, FDN_ORDER> dcX1;
        std::array<float, FDN_ORDER> dcY1;
        float dcBlockerCoeff{ 0.999f };

        // 笘・Soft-knee繧ｳ繝ｳ繝励Ξ繝・す繝ｧ繝ｳ: FDN繝輔ぅ繝ｼ繝峨ヰ繝・け繝ｫ繝ｼ繝怜・
        std::array<float, FDN_ORDER> fdnRmsEnv;
        float rmsCoeff{ 0.002f };

        std::array<float, NUM_BANDS> effectiveRT60;
        float theoreticalEDT{ 0.0f };

        AcousticMetrics acousticMetrics;
        Saturator saturatorL;
        Saturator saturatorR;
    };

} // namespace FDNReverb