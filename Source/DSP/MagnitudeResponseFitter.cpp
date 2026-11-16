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
        std::array<float, NUM_BANDS> uniform;
        uniform.fill(static_cast<float>(delaySamples));
        return designStage2PerBand(uniform, sampleRate, rt60, hfDamping, lfAbsorption);
    }

    MagnitudeResponseFitter::DesignResultStage2 MagnitudeResponseFitter::designStage2PerBand(
        const std::array<float, NUM_BANDS>& loopSamplesPerBand,
        double sampleRate,
        const std::array<float, NUM_BANDS>& rt60,
        float hfDamping,
        float lfAbsorption,
        bool octaveCalibrate)
    {
        precomputeInteractionMatrix(sampleRate);

        DesignResultStage2 result;

        constexpr int N = NUM_BANDS;
        const float fs = static_cast<float>(sampleRate);

        // ── Step 1: 音響物理に基づく RT60 連続変形と遅延長 m 比例 dB 目標化 ──
        //   - LF Absorption: 160Hz 基準の低域シェルビング重みによる連続変形
        //   - HF Damping: ISO 9613-1 大気分子吸音 (2kHz 以上で f^2 則) による連続変形
        //   - 遅延線長 m (伝播距離) に正確に比例した目標ループゲイン targetDb[i] を算出
        std::array<float, NUM_BANDS> targetDb;
        std::array<float, NUM_BANDS> targetT60;
        for (int i = 0; i < N; ++i) {
            const float freq = BAND_FREQ[i];

            // 低域吸音重み (160Hz 2次ロールオフ)
            const float lfWeight = 1.0f / (1.0f + std::pow(freq / 160.0f, 2.0f));
            float currentT60 = std::max(0.01f, rt60[i]) * (1.0f - lfAbsorption * 0.8f * lfWeight);
            currentT60 = std::max(0.01f, currentT60);

            // 高域大気分子吸音 (2kHz 以上で f^2 に比例)
            const float hfWeight = std::pow(std::max(0.0f, freq - 2000.0f) / 14000.0f, 2.0f);
            const float invT60 = (1.0f / currentT60) + (hfDamping * hfWeight * 2.0f);
            currentT60 = 1.0f / std::max(1e-4f, invT60);
            targetT60[i] = currentT60;

            // 遅延線長 m に正確に比例した減衰量 (dB)
            // Phase3 PL-3: バンド別の実効ループ長 (APF 群遅延込み) を使う
            float db = -60.0f * loopSamplesPerBand[i] / (fs * currentT60);
            db = std::clamp(db, -60.0f, -0.02f);
            targetDb[i] = db;
            result.targetDb[i] = db;
        }

        // ── Steps 2/5/6/7: targetDb から GEQ 係数を解く (較正反復のため関数化) ──
        // targetAdjust は Passivity Guard (関数末尾) が低域ターゲットを
        // 局所的に深めるための補正量 (bands 0/1 のみ、初期値 0)。
        std::array<double, N> targetAdjust{};
        auto solveFromTargetDb = [&]() {
            // ── Step 2: 中域基準ゲイン midGain を抽出 ──
            // (固定 0.998f は廃止し、関数末尾の Passivity Guard で厳密に保証)
            const float midDb = targetDb[4];
            float midGainLinear = std::pow(10.0f, midDb / 20.0f);
            result.midGainAbsorbed = midGainLinear;

            // 残差 dB: 中域からの偏差 (GEQ がフィットすべき周波数特性)
            std::array<double, NUM_BANDS> residualDb;
            for (int i = 0; i < N; ++i) {
                residualDb[i] = static_cast<double>(targetDb[i]) + targetAdjust[i]
                              - static_cast<double>(midDb);
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

            // ── Step 7: band 0 への midGain 吸収 ──
            // Passivity の保証は設計全体の末尾で行う (関数末尾の
            // Passivity Guard を参照)。ここでは未補正の midGain を
            // そのまま吸収する。
            result.geqStages[0] = absorbGainIntoBiquad(result.geqStages[0], midGainLinear);
        };

        solveFromTargetDb();

        // ── Step 8 (任意): オクターブ較正イテレーション ──
        // バンド中心で厳密に合わせる従来設計では、オクターブ帯域内の
        // ループゲイン形状 (中心で最大損失・両端で損失減) まで制御できず、
        // 帯域 EDC は最も損失の小さい部分帯域に支配されて実測 T30 が
        // 目標より伸びる (phase3/spring8k/SPRING8K.md)。ここでは設計時に
        // 急峻オクターブ測定 (8次 Butterworth) と同じ定義の合成 EDC を
        // 評価し、乖離の大きいバンドの targetDb を反復修正する。
        // 設計時のみの計算で、実行時コストはゼロ。
        if (octaveCalibrate) {
            // 合成 EDC によるバンド T60 推定 (このチャンネルの係数・ループ長)
            auto syntheticBandT60 = [&](int b) -> float {
                const float fc = BAND_FREQ[b];
                const float lo = fc / 1.41421356f;
                const float hi = std::min(fc * 1.41421356f, fs * 0.49f);
                const float f0 = std::sqrt(lo * hi);
                const float bw = hi - lo;
                const float loop = loopSamplesPerBand[b];
                constexpr int K = 25;
                constexpr double dt = 0.002;
                const double tMax = std::min(8.0, 2.5 * static_cast<double>(targetT60[b]) + 1.0);
                const int steps = static_cast<int>(tMax / dt);
                std::array<double, K> wgt{};
                std::array<double, K> rate{};
                double wSum = 0.0;
                for (int k = 0; k < K; ++k) {
                    const double frac = (static_cast<double>(k) + 0.5) / K;
                    const float f = static_cast<float>(lo * std::pow(hi / lo, frac));
                    double cascDb = 0.0;
                    for (int j = 0; j < N; ++j)
                        cascDb += static_cast<double>(biquadMagnitudeDB(result.geqStages[j], f, sampleRate));
                    const double om = (static_cast<double>(f) * f - static_cast<double>(f0) * f0)
                                    / (static_cast<double>(f) * bw);
                    const double mag2 = 1.0 / (1.0 + std::pow(std::abs(om), 16.0));
                    wgt[k] = mag2 * f; // log 等間隔ビン: df ∝ f
                    wSum += wgt[k];
                    rate[k] = -cascDb * static_cast<double>(fs) / loop; // dB/s
                }
                // EDC を構築し -5..-35 dB 区間で最小二乗フィット
                double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
                int cnt = 0;
                double e0 = 0.0;
                for (int k = 0; k < K; ++k) e0 += wgt[k];
                for (int sIdx = 0; sIdx <= steps; ++sIdx) {
                    const double t = sIdx * dt;
                    double e = 0.0;
                    for (int k = 0; k < K; ++k)
                        e += wgt[k] * std::pow(10.0, -rate[k] * t / 10.0);
                    const double edc = 10.0 * std::log10(std::max(e / e0, 1e-30));
                    if (edc <= -5.0 && edc >= -35.0) {
                        sx += t; sy += edc; sxx += t * t; sxy += t * edc; ++cnt;
                    }
                    if (edc < -40.0) break;
                }
                if (cnt < 10 || wSum <= 0.0) return -1.0f;
                const double denom = cnt * sxx - sx * sx;
                if (std::abs(denom) < 1e-12) return -1.0f;
                const double slope = (cnt * sxy - sx * sy) / denom;
                if (slope >= -0.01) return -1.0f;
                return static_cast<float>(-60.0 / slope);
            };

            // 較正対象: 全測定帯域 (125〜8k)。モデル誤差が小さい帯域は
            // デッドバンド (3%) で実質的に動かない。
            constexpr int kCalBands[] = { 2, 3, 4, 5, 6, 7, 8 };
            for (int iter = 0; iter < 5; ++iter) {
                bool anyChange = false;
                for (int b : kCalBands) {
                    const float t60fit = syntheticBandT60(b);
                    if (t60fit <= 0.0f) continue;
                    const float err = t60fit / targetT60[b] - 1.0f;
                    if (std::abs(err) < 0.03f) continue;
                    const float rateTarget = 60.0f / targetT60[b];
                    const float rateFit = 60.0f / t60fit;
                    float dDb = (rateTarget - rateFit) * loopSamplesPerBand[b] / fs;
                    dDb = std::clamp(dDb, -2.0f, 2.0f);
                    targetDb[b] = std::clamp(targetDb[b] - 0.85f * dDb, -60.0f, -0.02f);
                    result.targetDb[b] = targetDb[b];
                    anyChange = true;
                }
                if (!anyChange) break;
                solveFromTargetDb();
            }
        }

        // ── Step 9: Passivity Guard (細密グリッド真値評価 + 低域局所補正) ──
        // WLS フィット後の Biquad カスケード全体の真の最大振幅ゲインを、
        // 細密対数グリッド (1 Hz 〜 Nyquist、48 点/oct) で二重精度評価し、
        // 真の最大値が -0.05 dB 以下になるよう設計を抑え、全帯域で
        // ループゲイン < 1 を厳格に保証する (発振防止 & 長残響の維持)。
        //
        // 旧実装はバンド中心 10 点における相互作用行列近似でのみ評価して
        // いたため、band 0 (31.25 Hz) 以下と帯域間の超過を素通りした。
        // 実測では 192 kHz の Inchindown で最大 +1.23 dB の超過があり、
        // FDN ループの低域モードが自己増殖発振した
        // (phase3/sr-followup-b/SR_FOLLOWUP_B.md)。超過の主因は band 0
        // セクションの低域スカートで、高 SR では同セクションの係数が
        // b ≈ (1, -2, 1) の退化形に近づき、その低域応答は float32 の
        // 丸め残差で決まる (吸収前の係数で評価すると最大で約 0.55 dB
        // ずれるため、評価は常に格納される最終 float32 係数で行う)。
        // グリッド下限を 1 Hz とするのは、低域段の DC ゲインが係数
        // 量子化で 0 dB からずれ、DC〜数 Hz に平坦な超過が生じうるため
        // (192 kHz の Spring で +0.128 dB を実測)。
        //
        // 抑制方法は較正への影響が最も小さいものを選ぶ: 超過が低域
        // (90 Hz 未満) にある場合は、平坦な midGain スケールではなく
        // bands 0/1 のターゲットを局所的に深めて再解釈する。平坦
        // スケールは全帯域の毎パス損失を一様に増やし、高 SR (長い
        // ループ = 毎パス損失が小さい) では較正 RT60 を大きく壊す
        // (192 kHz で T30@1k が Inchindown -33%、Goldfoil -37% に
        // なることを実測)。ターゲット補正は band 0/1 近傍にしか
        // 効かず、125 Hz 以上の較正はほぼ保たれる。低域補正で届かない
        // 残余 (DC 残差等) のみ、最終係数への平坦スケールを最小量
        // 適用する。本ガードは設計時のみの計算で、音声経路の
        // コストは増えない。
        {
            auto cascadePeak = [&]() {
                double mx = -1.0e30;
                double fStar = 0.0;
                constexpr double kGridMinHz = 1.0;
                constexpr int kPointsPerOctave = 48;
                const double gridMaxHz = sampleRate * 0.5;
                const int numPoints = static_cast<int>(
                    std::ceil(std::log2(gridMaxHz / kGridMinHz) * kPointsPerOctave)) + 1;
                for (int gi = 0; gi < numPoints; ++gi) {
                    const double fEval = kGridMinHz
                        * std::pow(2.0, static_cast<double>(gi) / kPointsPerOctave);
                    double totalDb = 0.0;
                    for (int j = 0; j < N; ++j) {
                        totalDb += static_cast<double>(biquadMagnitudeDB(
                            result.geqStages[j], static_cast<float>(fEval), sampleRate));
                    }
                    if (totalDb > mx) { mx = totalDb; fStar = fEval; }
                }
                return std::pair<double, double>(mx, fStar);
            };

            constexpr double kCascadeCapDb = -0.06; // 公称 -0.05 dB − グリッド余裕 0.01 dB
            auto peak = cascadePeak();

            // 低域局所補正: 超過量に応じて bands 0/1 のターゲットを深める
            std::array<BiquadCoeffs, NUM_BANDS> bestStages = result.geqStages;
            std::array<float, NUM_BANDS> bestCommandDb = result.commandDb;
            std::array<double, N> bestAdjust = targetAdjust;
            float bestMidGainAbsorbed = result.midGainAbsorbed;
            double bestMaxDb = peak.first;
            for (int iter = 0; iter < 8 && peak.first > kCascadeCapDb; ++iter) {
                const double excess = peak.first - kCascadeCapDb;
                if (peak.second < 45.0) {
                    targetAdjust[0] -= excess * 1.15;
                    targetAdjust[1] -= excess * 0.35;
                } else if (peak.second < 90.0) {
                    targetAdjust[0] -= excess * 0.50;
                    targetAdjust[1] -= excess * 0.80;
                } else {
                    break; // 低域以外の超過は平坦スケール側で処理
                }
                solveFromTargetDb();
                peak = cascadePeak();
                if (peak.first < bestMaxDb) {
                    bestMaxDb = peak.first;
                    bestStages = result.geqStages;
                    bestCommandDb = result.commandDb;
                    bestAdjust = targetAdjust;
                    bestMidGainAbsorbed = result.midGainAbsorbed;
                }
            }
            result.geqStages = bestStages;
            result.commandDb = bestCommandDb;
            result.midGainAbsorbed = bestMidGainAbsorbed;
            for (int i = 0; i < N; ++i)
                result.targetDb[i] = static_cast<float>(
                    static_cast<double>(targetDb[i]) + bestAdjust[i]);

            // 残余の平坦スケール (最終 float32 係数に対して反復評価)
            if (bestMaxDb > kCascadeCapDb) {
                const BiquadCoeffs guardStage0 = result.geqStages[0];
                float scaleLinear = 1.0f;
                float bestScale = 1.0f;
                double bestScaleMaxDb = bestMaxDb;
                for (int iter = 0; iter < 8; ++iter) {
                    result.geqStages[0] = absorbGainIntoBiquad(guardStage0, scaleLinear);
                    const double maxDb = cascadePeak().first;
                    if (maxDb < bestScaleMaxDb) { bestScaleMaxDb = maxDb; bestScale = scaleLinear; }
                    if (maxDb <= kCascadeCapDb) break;
                    scaleLinear *= static_cast<float>(std::pow(10.0, (kCascadeCapDb - maxDb) / 20.0));
                }
                result.geqStages[0] = absorbGainIntoBiquad(guardStage0, bestScale);
                result.midGainAbsorbed = bestMidGainAbsorbed * bestScale;
            }
        }

        return result;
    }

} // namespace FDNReverb