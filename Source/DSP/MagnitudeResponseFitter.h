#pragma once
#include "DSPConstants.h"
#include "BiquadFilters.h"
#include "../AlgorithmPresets.h"
#include <array>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  MagnitudeResponseFitter
    // ─────────────────────────────────────────────────────────────────────────────
    //  10バンドのRT60ターゲットに対して、各遅延線の吸収フィルタを設計する。
    //
    //  設計モード:
    //    Stage 1 (Jot 1次直交化):
    //      Jot–Chaigne (AES Preprint 3030, 1991) の直交化1次フィルタ。
    //      DC と Nyquist の 2 点だけで設計する軽量版。
    //
    //    Stage 2c (Välimäki–Liski 累積バイカッドGEQ):
    //      Välimäki & Liski (IEEE SPL 2017) の Interaction Matrix + WLS による
    //      10バンド厳密フィッティング。
    //
    //      安全性保証:
    //        - 目標 dB を 0 以下にクランプ → ループゲイン ≤ 1 を保証
    //        - midGain を band 0 の b0/b1/b2 に吸収 → 二重適用なし
    //        - LF/HF ユーザー補正は GEQ 目標 dB に加算 → 独立段なし
    //
    //  重要:
    //    - dB スケールでなく T60 dB スケール (-60·m / (fs·T60)) でフィッティング
    //      Schlecht–Habets (DAFx-17) の「2 kHz で T60 が無限大に発散する罠」を回避
    //    - 設計はオフライン（メッセージスレッド）で行い、結果を Biquad 係数として
    //      オーディオスレッドに渡す
    // ─────────────────────────────────────────────────────────────────────────────
    class MagnitudeResponseFitter {
    public:
        enum class DesignMode {
            Stage1_Jot1stOrder,      // Jot 直交化1次 (DC/Nyquist 2点フィット)
            Stage2_BiquadGEQ          // Välimäki–Liski 累積バイカッドGEQ (10点厳密フィット)
        };

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 1 用設計結果（既存互換）
        // ─────────────────────────────────────────────────────────────────────────
        // ABSO_STAGES = 3 段の Biquad に格納される:
        //   coeffs[0] = ミッドゲイン（Jot 1次の直交化フィルタを Biquad 形式で）
        //   coeffs[1] = 低域補正（Low Shelf, LF Absorption ユーザー操作分）
        //   coeffs[2] = 高域補正（High Shelf, HF Damping ユーザー操作分）
        struct DesignResult {
            std::array<BiquadCoeffs, ABSO_STAGES> coeffs;
            float dcGain{ 1.0f };
            float nyquistGain{ 1.0f };
            float pole{ 0.0f };
        };

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 2c 用設計結果（修正版）
        // ─────────────────────────────────────────────────────────────────────────
        // 10 段の GEQ のみで構成される。
        //   geqStages[0]   = band 0 (31.25Hz)、midGain を係数に吸収済み
        //   geqStages[1..9] = band 1〜9 (62.5Hz〜16kHz)、純粋な GEQ
        //
        // フィルタ実行順序: geqStages[0] → geqStages[1] → ... → geqStages[9]
        // 独立した midGain 乗算は不要 (band 0 に吸収済み)。
        struct DesignResultStage2 {
            std::array<BiquadCoeffs, NUM_BANDS> geqStages;  // 10段 GEQ

            // デバッグ・可視化用
            std::array<float, NUM_BANDS> targetDb;          // 各バンドの目標 dB (クランプ後)
            std::array<float, NUM_BANDS> commandDb;         // WLS で求めた実コマンド dB
            float midGainAbsorbed{ 1.0f };                  // band 0 に吸収された midGain
        };

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 1 設計関数（既存互換）
        // ─────────────────────────────────────────────────────────────────────────
        static DesignResult design(
            int delaySamples,
            double sampleRate,
            const std::array<float, NUM_BANDS>& rt60,
            float hfDamping,
            float lfAbsorption);

        // ─────────────────────────────────────────────────────────────────────────
        //  Stage 2c 設計関数（インスタンス個別実行で完全スレッドセーフ）
        // ─────────────────────────────────────────────────────────────────────────
        DesignResultStage2 designStage2(
            int delaySamples,
            double sampleRate,
            const std::array<float, NUM_BANDS>& rt60,
            float hfDamping,
            float lfAbsorption);

        // ─────────────────────────────────────────────────────────────────────────
        //  Interaction Matrix の事前計算（prepare()時にインスタンスごとに呼ぶ）
        // ─────────────────────────────────────────────────────────────────────────
        void precomputeInteractionMatrix(double sampleRate);
        double getCachedSampleRate() const noexcept { return cachedSampleRate; }

    private:
        // ── Stage 1 ヘルパー ──
        static float t60ToLoopGain(float t60Seconds, int delaySamples, double sampleRate) noexcept;
        static float computeJotPole(float gDC, float alphaRatio) noexcept;
        static BiquadCoeffs orthogonalizedFirstOrderToBiquad(float gain, float pole) noexcept;
        static float getT60AtDC(const std::array<float, NUM_BANDS>& rt60) noexcept;
        static float getT60AtNyquist(const std::array<float, NUM_BANDS>& rt60, double sampleRate) noexcept;

        // ── Stage 2 ヘルパー ──
        static BiquadCoeffs designSymmetricPeakBiquad(
            float fcHz, float gainDB, float Q, double sampleRate) noexcept;
        static const std::array<float, NUM_BANDS>& getBandFreqs() noexcept { return BAND_FREQ; }
        static const std::array<float, NUM_BANDS>& getBandQs() noexcept;
        static float biquadMagnitudeDB(const BiquadCoeffs& c, float fEval, double sampleRate) noexcept;
        static void solveLDLT10(
            const std::array<std::array<double, NUM_BANDS>, NUM_BANDS>& A,
            const std::array<double, NUM_BANDS>& b,
            std::array<double, NUM_BANDS>& x) noexcept;

        // Biquad 係数全体に DC ゲインを乗算 (b0, b1, b2 を gain 倍)
        // これにより独立 DC スカラー適用と数学的に等価なフィルタになる
        static BiquadCoeffs absorbGainIntoBiquad(const BiquadCoeffs& c, float linearGain) noexcept;

        // ── Stage 2 インスタンス固有キャッシュ（マルチインスタンス完全分離） ──
        std::array<std::array<double, NUM_BANDS>, NUM_BANDS> cachedB{};
        std::array<std::array<double, NUM_BANDS>, NUM_BANDS> cachedBtWB{};
        std::array<double, NUM_BANDS> cachedW{};
        double cachedSampleRate{ 0.0 };
        bool cacheValid{ false };
    };

} // namespace FDNReverb