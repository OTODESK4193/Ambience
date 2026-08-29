#include "MagnitudeResponseFitter.h"
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>
#include <complex>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  バンド Q 値（オクターブバンド: Q ≈ √2 / (2^(1/2) - 2^(-1/2)) ≈ 1.414）
    // ─────────────────────────────────────────────────────────────────────────────
    static const std::array<float, NUM_BANDS> kBandQs = {
        1.7f,  // 31.25 Hz (端: Q 上昇)
        1.414f, // 62.5 Hz
        1.414f, // 125 Hz
        1.414f, // 250 Hz
        1.414f, // 500 Hz
        1.414f, // 1 kHz
        1.414f, // 2 kHz
        1.414f, // 4 kHz
        1.414f, // 8 kHz
        1.7f   // 16 kHz (端: Q 上昇)
    };

    const std::array<float, NUM_BANDS>& MagnitudeResponseFitter::getBandQs() noexcept {
        return kBandQs;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 1 ヘルパー（既存）
    // ─────────────────────────────────────────────────────────────────────────────

    float MagnitudeResponseFitter::t60ToLoopGain(float t60Seconds, int delaySamples, double sampleRate) noexcept {
        float t60Safe = std::max(0.01f, t60Seconds);
        float exponent = -3.0f * static_cast<float>(delaySamples) / (static_cast<float>(sampleRate) * t60Safe);
        return std::pow(10.0f, exponent);
    }

    float MagnitudeResponseFitter::computeJotPole(float gDC, float alphaRatio) noexcept {
        float alphaSafe = juce::jlimit(0.05f, 20.0f, alphaRatio);
        float gDCSafe = juce::jlimit(1e-6f, 0.99999f, gDC);
        constexpr float kLn10Over4 = 0.5756462732485f;
        float log10g = std::log10(gDCSafe);
        float alphaSqInv = 1.0f / (alphaSafe * alphaSafe);
        float pole = kLn10Over4 * log10g * (1.0f - alphaSqInv);
        return juce::jlimit(-0.98f, 0.98f, pole);
    }

    BiquadCoeffs MagnitudeResponseFitter::orthogonalizedFirstOrderToBiquad(float gain, float pole) noexcept {
        BiquadCoeffs c;
        c.b0 = gain * (1.0f - pole);
        c.b1 = 0.0f;
        c.b2 = 0.0f;
        c.a1 = -pole;
        c.a2 = 0.0f;
        return c;
    }

    float MagnitudeResponseFitter::getT60AtDC(const std::array<float, NUM_BANDS>& rt60) noexcept {
        return (rt60[0] + rt60[1]) * 0.5f;
    }

    float MagnitudeResponseFitter::getT60AtNyquist(const std::array<float, NUM_BANDS>& rt60, double sampleRate) noexcept {
        if (sampleRate <= 50000.0) {
            return rt60[9];
        }
        else {
            return (rt60[8] + rt60[9]) * 0.5f;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 1 メイン設計関数（既存）
    // ─────────────────────────────────────────────────────────────────────────────

    MagnitudeResponseFitter::DesignResult MagnitudeResponseFitter::design(
        int delaySamples,
        double sampleRate,
        const std::array<float, NUM_BANDS>& rt60,
        float hfDamping,
        float lfAbsorption)
    {
        DesignResult result;

        float t60DC = std::max(0.01f, getT60AtDC(rt60));
        float t60Nyq = std::max(0.01f, getT60AtNyquist(rt60, sampleRate));

        float gDC = t60ToLoopGain(t60DC, delaySamples, sampleRate);
        float gNyq = t60ToLoopGain(t60Nyq, delaySamples, sampleRate);

        float alpha = t60Nyq / t60DC;
        float pole = computeJotPole(gDC, alpha);

        result.coeffs[0] = orthogonalizedFirstOrderToBiquad(gDC, pole);

        float lfShelfDB = -lfAbsorption * 3.0f;
        result.coeffs[1] = FilterDesign::lowShelf(150.0f, lfShelfDB, sampleRate);

        float hfShelfDB = -hfDamping * 6.0f;
        result.coeffs[2] = FilterDesign::highShelf(4000.0f, hfShelfDB, sampleRate);

        result.dcGain = gDC;
        result.nyquistGain = gNyq;
        result.pole = pole;

        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2 ヘルパー: シンメトリックBiquad ピークフィルタ
    // ─────────────────────────────────────────────────────────────────────────────
    BiquadCoeffs MagnitudeResponseFitter::designSymmetricPeakBiquad(
        float fcHz, float gainDB, float Q, double sampleRate) noexcept
    {
        float fcSafe = juce::jlimit(10.0f, static_cast<float>(sampleRate) * 0.49f, fcHz);

        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = 2.0f * juce::MathConstants<float>::pi * fcSafe / static_cast<float>(sampleRate);
        float cosW0 = std::cos(w0);
        float sinW0 = std::sin(w0);
        float alpha = sinW0 / (2.0f * std::max(0.1f, Q));

        float a0 = 1.0f + alpha / A;

        BiquadCoeffs c;
        c.b0 = (1.0f + alpha * A) / a0;
        c.b1 = -2.0f * cosW0 / a0;
        c.b2 = (1.0f - alpha * A) / a0;
        c.a1 = -2.0f * cosW0 / a0;
        c.a2 = (1.0f - alpha / A) / a0;
        return c;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2 ヘルパー: Biquad 振幅応答 (dB) 計算
    // ─────────────────────────────────────────────────────────────────────────────
    float MagnitudeResponseFitter::biquadMagnitudeDB(
        const BiquadCoeffs& c, float fEval, double sampleRate) noexcept
    {
        double w = 2.0 * juce::MathConstants<double>::pi * fEval / sampleRate;
        double cosW = std::cos(w);
        double sinW = std::sin(w);
        double cos2W = std::cos(2.0 * w);
        double sin2W = std::sin(2.0 * w);

        double bRe = c.b0 + c.b1 * cosW + c.b2 * cos2W;
        double bIm = -c.b1 * sinW - c.b2 * sin2W;

        double aRe = 1.0 + c.a1 * cosW + c.a2 * cos2W;
        double aIm = -c.a1 * sinW - c.a2 * sin2W;

        double bMag2 = bRe * bRe + bIm * bIm;
        double aMag2 = aRe * aRe + aIm * aIm;

        double mag2 = bMag2 / std::max(1e-30, aMag2);

        return static_cast<float>(10.0 * std::log10(std::max(1e-30, mag2)));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2 ヘルパー: 10x10 LDLT 分解ソルバ
    // ─────────────────────────────────────────────────────────────────────────────
    void MagnitudeResponseFitter::solveLDLT10(
        const std::array<std::array<double, NUM_BANDS>, NUM_BANDS>& A,
        const std::array<double, NUM_BANDS>& b,
        std::array<double, NUM_BANDS>& x) noexcept
    {
        constexpr int N = NUM_BANDS;
        double L[N][N] = { 0 };
        double D[N] = { 0 };

        for (int i = 0; i < N; ++i) L[i][i] = 1.0;

        for (int j = 0; j < N; ++j) {
            double sum = A[j][j];
            for (int k = 0; k < j; ++k) {
                sum -= L[j][k] * L[j][k] * D[k];
            }
            D[j] = sum;

            if (std::abs(D[j]) < 1e-12) {
                D[j] = (D[j] < 0.0 ? -1e-12 : 1e-12);
            }

            for (int i = j + 1; i < N; ++i) {
                double s = A[i][j];
                for (int k = 0; k < j; ++k) {
                    s -= L[i][k] * L[j][k] * D[k];
                }
                L[i][j] = s / D[j];
            }
        }

        double z[N];
        for (int i = 0; i < N; ++i) {
            double s = b[i];
            for (int k = 0; k < i; ++k) s -= L[i][k] * z[k];
            z[i] = s;
        }

        double y[N];
        for (int i = 0; i < N; ++i) y[i] = z[i] / D[i];

        for (int i = N - 1; i >= 0; --i) {
            double s = y[i];
            for (int k = i + 1; k < N; ++k) s -= L[k][i] * x[k];
            x[i] = s;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2 ヘルパー: Biquad 係数への線形ゲイン吸収
    // ─────────────────────────────────────────────────────────────────────────────
    //  H(z) = (b0 + b1·z^{-1} + b2·z^{-2}) / (1 + a1·z^{-1} + a2·z^{-2})
    //
    //  全周波数で振幅を linearGain 倍するには、分子 (b0, b1, b2) のみを linearGain 倍
    //  すればよい。これは数学的に独立 DC スカラー適用と完全に等価。
    BiquadCoeffs MagnitudeResponseFitter::absorbGainIntoBiquad(
        const BiquadCoeffs& c, float linearGain) noexcept
    {
        BiquadCoeffs result = c;
        result.b0 *= linearGain;
        result.b1 *= linearGain;
        result.b2 *= linearGain;
        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2: Interaction Matrix の事前計算
    // ─────────────────────────────────────────────────────────────────────────────
    void MagnitudeResponseFitter::precomputeInteractionMatrix(double sampleRate) {
        if (cacheValid && std::abs(cachedSampleRate - sampleRate) < 0.5) {
            return;
        }

        constexpr int N = NUM_BANDS;
        constexpr float kProbeGainDB = 1.0f;

        for (int j = 0; j < N; ++j) {
            BiquadCoeffs c = designSymmetricPeakBiquad(
                BAND_FREQ[j], kProbeGainDB, kBandQs[j], sampleRate);

            for (int i = 0; i < N; ++i) {
                float dB = biquadMagnitudeDB(c, BAND_FREQ[i], sampleRate);
                cachedB[i][j] = static_cast<double>(dB);
            }
        }

        const std::array<double, NUM_BANDS> weights = {
            0.5,   // 31.25 Hz
            0.7,   // 62.5 Hz
            0.85,  // 125 Hz
            1.0,   // 250 Hz
            1.0,   // 500 Hz
            1.0,   // 1 kHz
            1.0,   // 2 kHz
            1.0,   // 4 kHz
            0.85,  // 8 kHz
            0.6    // 16 kHz
        };
        for (int i = 0; i < N; ++i) cachedW[i] = weights[i];

        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                double s = 0.0;
                for (int k = 0; k < N; ++k) {
                    s += cachedB[k][i] * cachedW[k] * cachedB[k][j];
                }
                cachedBtWB[i][j] = s;
            }
        }

        constexpr double kRidge = 1e-4;
        for (int i = 0; i < N; ++i) cachedBtWB[i][i] += kRidge;

        cachedSampleRate = sampleRate;
        cacheValid = true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Stage 2c: メイン設計関数（修正版）
    // ─────────────────────────────────────────────────────────────────────────────
    //  各遅延線につき:
    //    1. 各バンドの目標 dB を計算 (T60 dB スケール)
    //         t[i] = -60 · m / (fs · T60[i])
    //    2. ユーザー LF/HF 補正を目標 dB に直接加算
    //    3. 目標 dB を 0 以下にクランプ → ループゲイン ≤ 1 を保証
    //    4. 中域基準ゲイン midGain を抽出 (band 4 = 500Hz)
    //         midGain = 10^(midDb/20)
    //    5. 残差 dB ベクトルを WLS でフィット
    //         g_cmd = (B^T·W·B)^(-1)·B^T·W·t_residual
    //    6. 各 g_cmd[j] dB を Biquad 係数に変換
    //    7. band 0 の係数に midGain を吸収
    //         → 独立した DC スカラー適用が不要になる
    MagnitudeResponseFitter::DesignResultStage2 MagnitudeResponseFitter::designStage2(
        int delaySamples,
        double sampleRate,
        const std::array<float, NUM_BANDS>& rt60,
        float hfDamping,
        float lfAbsorption)
    {
        precomputeInteractionMatrix(sampleRate);

        DesignResultStage2 result;

        constexpr int N = NUM_BANDS;
        const float fs = static_cast<float>(sampleRate);
        const float m = static_cast<float>(delaySamples);

        // ── Step 1: 各バンドのループ1周ゲインを dB 単位で目標化 ──
        std::array<float, NUM_BANDS> targetDb;
        for (int i = 0; i < N; ++i) {
            float t60Safe = std::max(0.01f, rt60[i]);
            targetDb[i] = -60.0f * m / (fs * t60Safe);
        }

        // ── Step 2: ユーザー LF/HF 補正を目標 dB に加算 ──
        // LF Absorption: 低域帯 (31Hz, 62Hz, 125Hz) を追加減衰
        //   lfAbsorption=0 → 補正なし、=1 → 各帯 -3dB の追加減衰
        targetDb[0] += -lfAbsorption * 3.0f;
        targetDb[1] += -lfAbsorption * 2.5f;
        targetDb[2] += -lfAbsorption * 1.5f;
        // HF Damping: 高域帯 (4kHz, 8kHz, 16kHz) を追加減衰
        //   hfDamping=0 → 補正なし、=1 → 各帯 -6dB の追加減衰
        targetDb[7] += -hfDamping * 3.0f;
        targetDb[8] += -hfDamping * 5.0f;
        targetDb[9] += -hfDamping * 6.0f;

        // ── Step 3: 目標 dB を 0 以下にクランプ ──
        // ループゲイン ≤ 1 を数学的に保証する安全機構
        for (int i = 0; i < N; ++i) {
            targetDb[i] = std::min(targetDb[i], 0.0f);
            // 過剰な減衰は数値精度に悪影響なので下限も設ける (-60dB/loop)
            targetDb[i] = std::max(targetDb[i], -60.0f);
            result.targetDb[i] = targetDb[i];
        }

        // ── Step 4: 中域基準ゲイン midGain を抽出 (band 4 = 500Hz) ──
        float midDb = targetDb[4];
        float midGainLinear = std::pow(10.0f, midDb / 20.0f);
        result.midGainAbsorbed = midGainLinear;

        // 残差 dB: 中域からの偏差 (GEQ がフィットすべき周波数特性)
        std::array<double, NUM_BANDS> residualDb;
        for (int i = 0; i < N; ++i) {
            residualDb[i] = static_cast<double>(targetDb[i] - midDb);
        }

        // ── Step 5: WLS による GEQ 係数の決定 ──
        std::array<double, NUM_BANDS> rhs;
        for (int j = 0; j < N; ++j) {
            double s = 0.0;
            for (int k = 0; k < N; ++k) {
                s += cachedB[k][j] * cachedW[k] * residualDb[k];
            }
            rhs[j] = s;
        }

        std::array<double, NUM_BANDS> gCmd;
        solveLDLT10(cachedBtWB, rhs, gCmd);

        // ── Step 6: 各 g_cmd[j] dB を Biquad 係数に変換 ──
        // 安全範囲にクランプ (±18 dB 以内)
        for (int j = 0; j < N; ++j) {
            float gDb = static_cast<float>(juce::jlimit(-18.0, 18.0, gCmd[j]));
            result.commandDb[j] = gDb;
            result.geqStages[j] = designSymmetricPeakBiquad(
                BAND_FREQ[j], gDb, kBandQs[j], sampleRate);
        }

        // ── Step 7: band 0 の係数に midGain を吸収 ──
        // これにより独立 DC スカラー適用が不要になり、
        // フィルタカスケード全体のループゲインが厳密に WLS の解に従う。
        result.geqStages[0] = absorbGainIntoBiquad(result.geqStages[0], midGainLinear);

        return result;
    }

} // namespace FDNReverb