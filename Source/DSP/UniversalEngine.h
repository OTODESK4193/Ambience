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
#include "SaturatorADAAOS2.h"
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
    //  [phase3/mod-source 本番形] 平滑ランダム変調源 (Smoothed Random / Spin-Wander 型)
    //  試作 (proto-tree) の軽量化版。設計の要点は試作と同一:
    //  決定的 xorshift32 → 1 次 LP × 2 段で平滑化し、正弦 mix と RMS を揃えて
    //  正規化、軟飽和で ±1.15 級に制限する (Wander 相当の歩幅制限)。
    //  ストリームは「チャンネルペア (i, i+8) × APF 段」の 8×3 = 24 本。
    //  ペア相手は同一ストリームの反転を使い、ピッチ偏移の一次成分を相殺する。
    //  軽量化 (試作比):
    //   - 軟飽和は libm tanh から代数シグモイド x/√(1+x²) へ置換
    //     (奇対称・単調・原点傾き 1・|出力| < 1。sqrt/除算は IEEE 正確で、
    //     プラットフォーム間の決定性も tanh より良い)。
    //   - tick は制御レート (16 サンプル毎) に落とし、区間内は線形補間する。
    //     源の帯域は ≤0.5 Hz 級であり、極を保存する係数換算
    //     aStep = 1-(1-a)^16 と合わせ、軌跡の統計 (std/zcr/微分 std) は
    //     毎サンプル版と同格になる (probe_opt.cpp で校正・実測)。
    //   - 正規化は制御時間でも同形の解析式で squash 前 std を RAND_PRE_STD
    //     (= 0.60、probe_opt 校正値) に揃える。squash 後 std は実測 ≈0.47 で、
    //     毎サンプル tanh 版の実測 0.44〜0.49 の範囲内に収まる。
    //  AMBIENCE_MODSRC_PACKAGE=0 でビルドするとパッケージ全体が無効化され、
    //  エンジンは現行 (muse/dev 3f6627db) とビット一致する (検証済み)。
    // ═══════════════════════════════════════════════════════════════════════════
#ifndef AMBIENCE_MODSRC_PACKAGE
#define AMBIENCE_MODSRC_PACKAGE 1
#endif

#if AMBIENCE_MODSRC_PACKAGE
    struct RandModStream {
        uint32_t rngState{ 1 };
        float lp1{ 0.0f };
        float lp2{ 0.0f };
        float coeff{ 0.0f }; // 制御ステップあたりの 1-pole 係数 (ブロック毎に再計算)
        float gain{ 0.0f };  // 正規化ゲイン (同上)

        // 制御ステップ 1 回分 (16 サンプル相当) 進め、軟飽和後の値を返す。
        inline float tickControl() noexcept {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            const float u = static_cast<float>(static_cast<int32_t>(rngState))
                          * (1.0f / 2147483648.0f); // [-1, +1)
            lp1 += coeff * (u - lp1);
            lp2 += coeff * (lp1 - lp2);
            const float x = gain * lp2 * (1.0f / 1.15f);
            // Wander bound: 代数シグモイドで ±1.15 に軟飽和 (tanh の安価な近似)
            return 1.15f * x / std::sqrt(1.0f + x * x);
        }
    };
#endif

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
        void applySdnDerivedParams();
        void updateSdnGeometry();
        // [phase3/small-fixes] reset() 完全初期化: prepare() 終端状態の捕捉/復元
        void captureResetSnapshot();
        void restoreResetSnapshot();
#if AMBIENCE_MODSRC_PACKAGE
        void resetRandMod(); // [phase3/mod-source] ランダム源の決定論的再シード
#endif
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
        bool sdnParamsUpdatePending{ false };
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
        // phase3/goldfoil-stereo: per-ear feed meshes (Goldfoil only).
        // gfMeshL is driven by the L input alone, gfMeshR by R alone;
        // their pickup vectors source the structural FDN feed for the
        // matching ear's channels so the feed stops being a shared
        // cross-path under SPLIT8. The shared plateMesh still produces
        // the ER output exactly as before.
        SDNTopology2DMesh                            gfMeshL;
        SDNTopology2DMesh                            gfMeshR;
        SDNTopologySpring1D                          springChain;
        SDNTopologyInchindown                        inchindownEngine;
        // [tank-stereo] Per-ear tank: a second, independent Inchindown
        // tank instance for the R ear. The L tank (inchindownEngine) is
        // driven by the L input alone and the R tank by the R input alone
        // (same pattern as gfMeshL/gfMeshR), so the tank's late energy
        // stops being a shared monaural cross-path under SPLIT8. Identical
        // parameters on both instances: a centred source drives both tanks
        // exactly as the legacy single tank, so centred renders stay
        // bit-identical to the legacy design.
        SDNTopologyInchindown                        inchindownEngineR;

        std::array<LinearDelayLine, INPUT_DIFFUSER_STAGES> inputDiffusersM;
        std::array<LinearDelayLine, INPUT_DIFFUSER_STAGES> inputDiffusersS;
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
#if AMBIENCE_MODSRC_PACKAGE
        // [phase3/mod-source 本番形] 平滑ランダム変調源の状態。
        // 既定構成 = ランダム源 + APF 第 1〜3 段 + 深さ倍率 MODSRC_DMULT (試聴候補)。
        static constexpr int   RAND_PAIRS = FDN_ORDER / 2;
        static constexpr int   RAND_CTRL_INTERVAL = 16; // 制御レート間隔 (サンプル)
        static constexpr float RAND_PRE_STD = 0.60f;    // squash 前 std 目標 (probe_opt 校正)
        static constexpr float MODSRC_DMULT = 0.5f;     // 深さ倍率 (APF 経路のみ)
        std::array<RandModStream, RAND_PAIRS * SERIAL_APF_STAGES> randStreams{};
        std::array<float, RAND_PAIRS>              randPairRates{}; // ペアレート (両 ch の幾何平均)
        std::array<std::array<float, SERIAL_APF_STAGES>, RAND_PAIRS> randValFrom{};  // 補間始点
        std::array<std::array<float, SERIAL_APF_STAGES>, RAND_PAIRS> randValTo{};    // 補間終点
        std::array<std::array<float, SERIAL_APF_STAGES>, RAND_PAIRS> randPairVals{}; // 当サンプルの補間値
        int randCtrlCounter{ 0 };
#endif
        std::array<float, FDN_ORDER>               fdnBaseDelaySamples{};
        std::array<float, FDN_ORDER>               currentFdnDelaySamples{};
        // ★ FDN 読み出し引数キャッシュ: FDN ベース遅延はパラメータ変更時のみ変化し
        //   定常時は currentFdnDelaySamples == fdnBaseDelaySamples が厳密に成立する。
        //   目標値に対する readInt / Hermite の引数を事前計算して使い回し、毎サンプルの
        //   round/clamp/変換を省く。引数の値は従来経路の計算と完全同一（ビット完全）。
        //   fdnReadCacheTarget が目標値と一致しないチャンネルはキャッシュ無効。
        std::array<float, FDN_ORDER>               fdnReadCacheTarget{};
        std::array<int,   FDN_ORDER>               fdnReadCacheId{};    // int時: readInt 引数 / frac時: 整数部 id
        std::array<float, FDN_ORDER>               fdnReadCacheFrac{};  // frac時: 小数部 frac
        std::array<char,  FDN_ORDER>               fdnReadCacheIsInt{}; // 1 = readInt 経路
        float                                      delaySmoothCoeff{ 0.0f };
        std::array<float, FDN_ORDER>               fbVec{};

        // Goldfoil structural feed (phase3/goldfoil/STRUCTURAL.md): state of
        // the 1-pole lowpass (fc = 1 kHz, coefficient set in prepare()) that
        // conditions the per-channel mesh pickup vector before it is wired
        // into the FDN delay-line inputs. Goldfoil only; all other
        // topologies never touch this state.
        std::array<float, FDN_ORDER>               gfMeshFeedLpState{};
        float                                      gfMeshFeedLpCoeff{ 0.0f };
        // Second feed-shaping stage (phase3/goldfoil/FEED8K.md): 6th-order
        // Butterworth lowpass at 2.2 kHz (3 RBJ biquad sections, coefficients
        // set in prepare()) cascaded after the 1 kHz 1-pole above. The mesh
        // pickup is spectrally bright; without this stage its 3-6 kHz energy
        // inflates the tail's upper-mid band (and, through analysis-filter
        // skirt leakage, the measured 8 kHz decay). The stage passes the
        // <= 3 kHz zero-filling band essentially intact.
        std::array<std::array<float, 5>, 3>        gfMeshFeedBqCoeffs{}; // per section: b0,b1,b2,a1,a2
        std::array<std::array<float, FDN_ORDER>, 3> gfMeshFeedBqX1{}, gfMeshFeedBqX2{};
        std::array<std::array<float, FDN_ORDER>, 3> gfMeshFeedBqY1{}, gfMeshFeedBqY2{};
        // Third feed-shaping stage (phase3/ringing): two RBJ peaking cuts
        // (1750 Hz / -8 dB and 2600 Hz / -10 dB, Q = 2.5, coefficients set in
        // prepare()) cascaded after the Butterworth stage. The mesh pickup
        // carries a dense modal cluster at 1.7-2.9 kHz that the feed injects
        // into the FDN at +7..+12 dB over the main-path excitation, where it
        // rings as an audible metallic tone in the tail. The cuts trim only
        // the feed's share of that cluster's excitation; loop poles, the
        // <= 1.5 kHz zero-filling band and the HF shaping are untouched.
        std::array<std::array<float, 5>, 2>        gfMeshFeedPkCoeffs{}; // per section: b0,b1,b2,a1,a2
        std::array<std::array<float, FDN_ORDER>, 2> gfMeshFeedPkX1{}, gfMeshFeedPkX2{};
        std::array<std::array<float, FDN_ORDER>, 2> gfMeshFeedPkY1{}, gfMeshFeedPkY2{};

        float apfGain{ 0.618f };
        bool  bypassER{ false };
        bool  bypassInputDiffusers{ false };
        bool  inputDiffusersValid{ true }; // false if any diffuser delay line got nullptr at prepare
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
        // [ER law unification] per-path trims anchored at the preset default
        // erLevel so both ER paths follow the same erLevel^2 law (see ER_PRO.md)
        float erCoreTrim{ 1.0f };
        float erIsmTrim{ 1.0f };
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
        std::array<float, INPUT_DIFFUSER_STAGES> cachedDiffuserDelaySmpM{};
        std::array<float, INPUT_DIFFUSER_STAGES> cachedDiffuserDelaySmpS{};
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
        float currentDesignRT60_1k{ 1.5f }; // effectiveRT60[5], captured in updateAbsorptionFilters

        AcousticMetrics acousticMetrics;
        SaturatorADAAOS2 saturatorL;
        SaturatorADAAOS2 saturatorR;
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

        // ── [phase3/small-fixes] reset() 完全初期化スナップショット ──
        // prepare() 終端 (updateTopologyAndRouting + reset 完了直後) の状態を
        // 捕捉し、reset() で復元する。対象は「reset()/panicReset() が戻さず、
        // かつ次の setParams/遷移で決定論的に再導出される前に音へ影響する」
        // 状態の全て: パラメータ変更検出の基準 (activeParams)、平滑値の
        // 現在値/目標値、吸収係数バンク、FDN 基準遅延 (currentFdnDelaySamples
        // のスナップ先)、SDN 基準遅延、ISM のパラメータオブジェクトと
        // 変更検出キャッシュ、サチュレータの amount/モード。
        bool           resetSnapshotValid{ false };
        DSPParams      snapActiveParams{};
        DSPParams      snapPendingParams{};
        ReverbTopology snapCurrentTopology{ ReverbTopology::Room };
        ReverbTopology snapPendingTopology{ ReverbTopology::Room };
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> snapCoeffsA{};
        std::array<std::array<BiquadCoeffs, ABSO_STAGES_S2>, FDN_ORDER> snapCoeffsB{};
        std::array<float, NUM_BANDS> snapEffectiveRT60{};
        std::array<float, NUM_BANDS> snapTargetRT60{};
        float          snapTheoreticalEDT{ 0.0f };
        float          snapCurrentRT60Mid{ 1.5f };
        float          snapCurrentDesignRT60_1k{ 1.5f };
        float          snapModDepthScale{ 1.0f };
        float          snapLateMakeupGainLinear{ 1.0f };
        float          snapApfGain{ 0.618f };
        float          snapDiffusionSensitivity{ 1.0f };
        bool           snapBypassER{ false };
        bool           snapBypassInputDiffusers{ false };
        float          snapPreDelaySamples{ 0.0f };
        std::array<float, FDN_ORDER> snapFdnBaseDelaySamples{};
        std::array<std::array<float, SERIAL_APF_STAGES>, FDN_ORDER> snapCachedApfBaseDelaySmp{};
        std::array<std::array<int, SERIAL_APF_STAGES>, FDN_ORDER> snapCachedApfBaseDelayInt{};
        int            snapCurrentERTapCount{ 0 };
        std::array<float, MAX_ER_TAPS> snapCurrentERDelaySamples{};
        std::array<float, MAX_ER_TAPS> snapCurrentERGains{};
        float          snapSmPreDelayCur{ 0.0f }, snapSmPreDelayTgt{ 0.0f };
        float          snapSmRoomSizeCur{ 1.0f }, snapSmRoomSizeTgt{ 1.0f };
        float          snapSmDiffuserCur{ 0.0f }, snapSmDiffuserTgt{ 0.0f };
        float          snapSmApfCur{ 0.0f },      snapSmApfTgt{ 0.0f };
        float          snapSmWidthCur{ 1.0f },    snapSmWidthTgt{ 1.0f };
        float          snapSmMakeupCur{ 0.0f },   snapSmMakeupTgt{ 1.0f };

        // ── [phase3/small-fixes 試作] makeup 増加の保留・解放 ──
        // Decay 下方向ドラッグでは rt60Mid の低下で makeup が最大 +6 dB
        // 上昇する。長いテイルがループ内に残っている間に makeup だけが
        // 先に上がると帯域包絡が静的端点を超える (DECAY_KNOB 残存項)。
        // テイルが鳴っている状態でのフェード経由の再設計では増加分を
        // 保留し、再設計が止んで hold 時間が経ってから解放する。
        bool  makeupHoldActive{ false };
        float makeupHeldTarget{ 1.0f };
        int   makeupHoldSamplesLeft{ 0 };
        int   makeupHoldSamplesTotal{ 14400 }; // prepare で fs × 0.300 に設定
        bool  absorptionDesignFromFade{ false }; // processBlock の保留対象経路か
        bool  tailWasActive{ false }; // reset 以降に有意な入力を処理したか
        std::array<float, 8> snapSdnBaseDelaySamples{};
        std::array<uint32_t, 6> snapSdnRngStates{};
        EarlyReflectionsISM::StateSnapshot snapIsmState{};
        SaturatorADAAOS2::ResetState snapSatL{};
        SaturatorADAAOS2::ResetState snapSatR{};
    };

} // namespace FDNReverb