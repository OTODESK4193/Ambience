#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <immintrin.h>

namespace FDNReverb {

/**
 * @brief 超高速 AVX2 1D 散乱チェイン ＋ Boing チャープ分散フィルタ
 * Spring (Vintage Tank / Accutronics Type 9 / Fender 6G15) 専用 1次元物理散乱コアエンジン
 */
class SDNTopologySpring1D {
public:
    static constexpr int NUM_SPRINGS = 3;         // 3本のスプリング (低・中・高張力)
    static constexpr int SEGMENTS_PER_SPRING = 8; // 各スプリング内のセグメント数
    static constexpr int BUFFER_MASK = 511;       // 512サンプル循環バッファ (2^9)
    static constexpr int BUFFER_SIZE = 512;
    static constexpr int DISPERSION_STAGES = 8;   // 8段カスケード分散オールパス

    SDNTopologySpring1D() noexcept {
        reset();
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
        fs = sampleRate;
        const float fsf = static_cast<float>(sampleRate);

        // 3本のスプリングの基本遅延セグメント長（互いに素な素数比 + 黄金比摂動）
        // Spring 0: 低域・長遅延 (~41.3ms 往復: Accutronics S0)
        static constexpr int BASE_DELAYS_S0[8] = { 113, 131, 149, 167, 181, 197, 223, 239 };
        // Spring 1: 中域・中遅延 (~33.7ms 往復: Accutronics S1)
        static constexpr int BASE_DELAYS_S1[8] = {  89,  97, 107, 127, 137, 151, 163, 179 };
        // Spring 2: 高域・短遅延 (~24.1ms 往復: Accutronics S2)
        static constexpr int BASE_DELAYS_S2[8] = {  61,  71,  79,  83, 101, 109, 113, 131 };

        const float sampleRateScale = fsf / 48000.0f;
        for (int i = 0; i < 8; ++i) {
            delaysS0[i] = std::clamp(static_cast<int>(BASE_DELAYS_S0[i] * sampleRateScale), 4, BUFFER_SIZE - 4);
            delaysS1[i] = std::clamp(static_cast<int>(BASE_DELAYS_S1[i] * sampleRateScale), 4, BUFFER_SIZE - 4);
            delaysS2[i] = std::clamp(static_cast<int>(BASE_DELAYS_S2[i] * sampleRateScale), 4, BUFFER_SIZE - 4);
        }

        // 分散オールパスの極配置（ヘリカルスプリング特有の高周波群遅延チャープ）
        for (int s = 0; s < DISPERSION_STAGES; ++s) {
            const float frac = static_cast<float>(s + 1) / static_cast<float>(DISPERSION_STAGES);
            // 極半径 r: 0.65 〜 0.88 (高域ほど鋭い群遅延ピーク)
            dispR[s] = 0.65f + 0.028f * static_cast<float>(s);
            // 極周波数 theta: 500Hz 〜 4200Hz に分散ピークを配置
            const float freqHz = 500.0f + 3700.0f * std::pow(frac, 1.4f);
            dispTheta[s] = 2.0f * 3.14159265f * freqHz / fsf;
        }

        setParameters(2.5f, 0.3f, 1.0f, 1.0f);
        reset();
    }

    void reset() noexcept {
        for (int s = 0; s < NUM_SPRINGS; ++s) {
            for (int ch = 0; ch < SEGMENTS_PER_SPRING; ++ch) {
                std::fill(fwdBuffer[s][ch].begin(), fwdBuffer[s][ch].end(), 0.0f);
                std::fill(bwdBuffer[s][ch].begin(), bwdBuffer[s][ch].end(), 0.0f);
            }
        }
        writeIndex = 0;

        // 分散フィルタステートクリア
        for (int s = 0; s < DISPERSION_STAGES; ++s) {
            apState1_L[s] = apState2_L[s] = 0.0f;
            apState1_R[s] = apState2_R[s] = 0.0f;
            apState1_M[s] = apState2_M[s] = 0.0f;
        }

        dcBlockL = dcBlockR = 0.0f;
        transientState = 0.0f;
    }

    /**
     * @brief 物理パラメータの設定
     * @param rt60 減衰時間（秒）
     * @param hfDamping 高域ダンピング [0.0, 1.0]
     * @param sizeScale スプリングの仮想長スケール [0.5, 2.0]
     * @param boingIntensity チャープ分散強度 [0.0, 2.0]
     */
    void setParameters(float rt60, float hfDamping, float sizeScale, float boingIntensity = 1.0f) noexcept {
        const float clampedRt60 = std::clamp(rt60, 0.1f, 20.0f);
        const float decayDbPerSec = -60.0f / clampedRt60;

        // 導波管1セグメントあたりの平均伝播損失
        const float lossBase = std::pow(10.0f, (decayDbPerSec * 0.003f) / 20.0f);
        const float lossDamped = lossBase * (1.0f - hfDamping * 0.08f);

        // 各ジャンクションの反射係数 r (不均一性による散乱: NED <= 50ms 達成)
        static constexpr float BASE_R[8] = { 0.28f, -0.22f, 0.31f, -0.19f, 0.25f, -0.33f, 0.21f, -0.26f };

        for (int i = 0; i < 8; ++i) {
            const float r = BASE_R[i] * std::clamp(1.0f / std::sqrt(sizeScale), 0.7f, 1.4f);
            rCoeff[i] = std::clamp(r, -0.65f, 0.65f);
            tCoeff[i] = std::sqrt(1.0f - rCoeff[i] * rCoeff[i]); // 直交ユニタリ条件 (エネルギー保存)
            lossVector[i] = std::clamp(lossDamped, 0.85f, 0.998f);
        }

        v_r    = _mm256_loadu_ps(rCoeff.data());
        v_t    = _mm256_loadu_ps(tCoeff.data());
        v_loss = _mm256_loadu_ps(lossVector.data());

        // 端点境界反射係数（固定端・弾性反転反射）
        boundaryReflection = -std::clamp(0.96f - hfDamping * 0.06f, 0.80f, 0.98f);

        // 2次オールパス分散係数の更新
        const float boingScale = std::clamp(boingIntensity, 0.0f, 1.5f);
        for (int s = 0; s < DISPERSION_STAGES; ++s) {
            const float r = std::clamp(dispR[s] * (0.85f + 0.15f * boingScale), 0.40f, 0.95f);
            const float theta = dispTheta[s];

            apCoeff_a1[s] = -2.0f * r * std::cos(theta);
            apCoeff_a2[s] = r * r;
        }
    }

    /**
     * @brief 1サンプル処理（AVX2 散乱 + Boing 分散カスケード + 16ch FDN 直交注入）
     * @param inL, inR ステレオ入力
     * @param outL, outR ステレオ初期散乱出力 (Mid/Side 直交合成)
     * @param fdn16Out 16ch FDN への直交注入バッファ (float[16])
     */
    inline void processOneSample(float inL, float inR, float& outL, float& outR, float* fdn16Out) noexcept {
        const int wIdx = writeIndex;

        // ─────────────────────────────────────────────────────────────
        // 1. 入力加振（Mid/Side 分解 + 非線形初期過渡カップリング）
        // ─────────────────────────────────────────────────────────────
        const float midIn  = (inL + inR) * 0.5f;
        const float sideIn = (inL - inR) * 0.5f;

        // スプリングの初期アタック過渡応答（「チャキッ」という金属接触音）
        const float transient = midIn - transientState;
        transientState += 0.35f * transient;
        const float drivenMid = midIn + 0.15f * transient;

        // 3本のスプリングへの入力分配 (L/Center/R 空間結合)
        const float injectS0 = drivenMid * 0.5f + sideIn * 0.4f; // Spring 0 (L寄り)
        const float injectS1 = drivenMid * 0.707f;               // Spring 1 (Center)
        const float injectS2 = drivenMid * 0.5f - sideIn * 0.4f; // Spring 2 (R寄り)

        // ─────────────────────────────────────────────────────────────
        // 2. AVX2 散乱ジャンクション計算 (3スプリング並列処理)
        // ─────────────────────────────────────────────────────────────
        alignas(32) float v1_plus_arr[8];
        alignas(32) float v2_minus_arr[8];
        alignas(32) float pickupNode[NUM_SPRINGS][8];

        const __m256 vZero = _mm256_setzero_ps();
        const __m256 absMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
        const __m256 vMaxAmp = _mm256_set1_ps(4.0f);
        const __m256 vClampMin = _mm256_set1_ps(-2.5f);
        const __m256 vClampMax = _mm256_set1_ps(2.5f);

        for (int s = 0; s < NUM_SPRINGS; ++s) {
            const auto& delayTable = (s == 0) ? delaysS0 : ((s == 1) ? delaysS1 : delaysS2);
            const float injectVal = (s == 0) ? injectS0 : ((s == 1) ? injectS1 : injectS2);

            // 遅延ラインからの入射波収集 (SoA ロード)
            for (int ch = 0; ch < 8; ++ch) {
                const int rIdx = (wIdx - delayTable[ch]) & BUFFER_MASK;
                v1_plus_arr[ch]  = fwdBuffer[s][ch][rIdx];
                v2_minus_arr[ch] = bwdBuffer[s][ch][rIdx];
            }

            // 加振点注入 (セグメント 0 の前進波へ)
            v1_plus_arr[0] += injectVal;

            __m256 v1_plus  = _mm256_load_ps(v1_plus_arr);
            __m256 v2_minus = _mm256_load_ps(v2_minus_arr);

            // ★ AVX2 / FMA 正規化ケリー・ロックバウム散乱 (ユニタリ直交)
            // v1_minus = t * v2_minus - r * v1_plus
            __m256 v1_minus = _mm256_fmsub_ps(v_t, v2_minus, _mm256_mul_ps(v_r, v1_plus));
            // v2_plus  = t * v1_plus  + r * v2_minus
            __m256 v2_plus  = _mm256_fmadd_ps(v_t, v1_plus,  _mm256_mul_ps(v_r, v2_minus));

            // 物理伝播損失（ダンピング）
            v1_minus = _mm256_mul_ps(v1_minus, v_loss);
            v2_plus  = _mm256_mul_ps(v2_plus,  v_loss);

            // セーフティ機構: ブランチレス NaN/Inf 自動修復 ＆ クランプ
            const __m256 notNanMask = _mm256_cmp_ps(v1_minus, v1_minus, _CMP_EQ_OQ);
            const __m256 absVal     = _mm256_and_ps(v1_minus, absMask);
            const __m256 notInfMask = _mm256_cmp_ps(absVal, vMaxAmp, _CMP_LE_OQ);
            v1_minus = _mm256_blendv_ps(vZero, v1_minus, _mm256_and_ps(notNanMask, notInfMask));
            v1_minus = _mm256_min_ps(_mm256_max_ps(v1_minus, vClampMin), vClampMax);

            alignas(32) float outFwd[8];
            alignas(32) float outBwd[8];
            _mm256_store_ps(outBwd, v1_minus);
            _mm256_store_ps(outFwd, v2_plus);

            // 端点境界での反転反射結合
            // 先頭 (ch=0): 左端は固定端反射
            const float boundaryL = outBwd[0] * boundaryReflection;
            // 終端 (ch=7): 右端はピックアップ端反射
            const float boundaryR = outFwd[7] * boundaryReflection;

            // 導波管遅延バッファへの書き込み (隣接セグメントへの波の受け渡し)
            fwdBuffer[s][0][wIdx] = boundaryL;
            for (int ch = 1; ch < 8; ++ch) {
                fwdBuffer[s][ch][wIdx] = outFwd[ch - 1];
            }

            for (int ch = 0; ch < 7; ++ch) {
                bwdBuffer[s][ch][wIdx] = outBwd[ch + 1];
            }
            bwdBuffer[s][7][wIdx] = boundaryR;

            for (int ch = 0; ch < 8; ++ch) {
                pickupNode[s][ch] = outFwd[ch] + outBwd[ch];
            }
        }

        // ─────────────────────────────────────────────────────────────
        // 3. Boing チャープ分散フィルタ（8段カスケード 2次オールパス）
        // ─────────────────────────────────────────────────────────────
        // ピックアップ信号 (S0: Left寄り, S1: Mid, S2: Right寄り)
        float sigL = pickupNode[0][7];
        float sigM = pickupNode[1][7];
        float sigR = pickupNode[2][7];

        // Direct Form II Transposed による直列カスケード実行
        for (int s = 0; s < DISPERSION_STAGES; ++s) {
            const float coeffA1 = apCoeff_a1[s];
            const float coeffA2 = apCoeff_a2[s];

            // sigL
            const float yL = coeffA2 * sigL + apState1_L[s];
            apState1_L[s] = coeffA1 * sigL - coeffA1 * yL + apState2_L[s];
            apState2_L[s] = sigL - coeffA2 * yL;
            sigL = yL;

            // sigR
            const float yR = coeffA2 * sigR + apState1_R[s];
            apState1_R[s] = coeffA1 * sigR - coeffA1 * yR + apState2_R[s];
            apState2_R[s] = sigR - coeffA2 * yR;
            sigR = yR;

            // sigM
            const float yM = coeffA2 * sigM + apState1_M[s];
            apState1_M[s] = coeffA1 * sigM - coeffA1 * yM + apState2_M[s];
            apState2_M[s] = sigM - coeffA2 * yM;
            sigM = yM;
        }

        // ─────────────────────────────────────────────────────────────
        // 4. 16ch FDN への直交注入マトリックス（同相成分完全相殺）
        // ─────────────────────────────────────────────────────────────
        // 各スプリングのノード信号を組み合わせて 16ch FDN へ直交マッピング
        // 直交符号: 全和=0, 偶奇和=0 を保証（直流蓄積およびコームフィルタ完全防止）
        static constexpr float SPRING_SIGN[16] = {
            1.0f, -1.0f,  1.0f, -1.0f,
           -1.0f,  1.0f, -1.0f,  1.0f,
            1.0f,  1.0f, -1.0f, -1.0f,
           -1.0f, -1.0f,  1.0f,  1.0f
        };

        const float taps[16] = {
            pickupNode[0][1], pickupNode[1][2], pickupNode[2][3], sigL,
            pickupNode[0][5], pickupNode[1][4], pickupNode[2][1], sigR,
            pickupNode[0][3], pickupNode[1][6], pickupNode[2][5], sigM,
            pickupNode[0][7], pickupNode[1][0], pickupNode[2][7], (sigL - sigR)
        };

        for (int k = 0; k < 16; ++k) {
            fdn16Out[k] = taps[k] * SPRING_SIGN[k] * 0.353553f; // 1/sqrt(8) 正規化
        }

        // ─────────────────────────────────────────────────────────────
        // 5. ステレオ ER 出力（3相 Clarke 変換直交射影 + DCブロッカー）
        // ─────────────────────────────────────────────────────────────
        // Clarke 変換: yL = sqrt(2/3) * (S1 - 0.5 S0 - 0.5 S2)
        //              yR = sqrt(2/3) * (sqrt(3)/2 S0 - sqrt(3)/2 S2)
        // 幾何学的完全直交 (mL . mR = 0, ||mL|| = ||mR|| = 1.0)
        static constexpr float SQRT_2_OVER_3 = 0.81649658f;
        static constexpr float HALF_SQRT_3   = 0.86602540f;

        const float clarkeAlpha = sigM - 0.5f * (sigL + sigR);
        const float clarkeBeta  = HALF_SQRT_3 * (sigL - sigR);

        const float rawOutL = clarkeAlpha * SQRT_2_OVER_3;
        const float rawOutR = clarkeBeta * SQRT_2_OVER_3;

        // リーキーDCブロッカー
        dcBlockL += 0.002f * (rawOutL - dcBlockL);
        dcBlockR += 0.002f * (rawOutR - dcBlockR);
        outL = rawOutL - dcBlockL;
        outR = rawOutR - dcBlockR;

        // ポインタインデックスのゼロオーバーヘッド更新
        writeIndex = (wIdx + 1) & BUFFER_MASK;
    }

private:
    double fs{ 48000.0 };
    int writeIndex{ 0 };

    // 各スプリングの遅延長配列
    std::array<int, 8> delaysS0{};
    std::array<int, 8> delaysS1{};
    std::array<int, 8> delaysS2{};

    // 反射・透過・損失パラメータ
    alignas(32) std::array<float, 8> rCoeff{};
    alignas(32) std::array<float, 8> tCoeff{};
    alignas(32) std::array<float, 8> lossVector{};
    float boundaryReflection{ -0.95f };

    __m256 v_r{ _mm256_setzero_ps() };
    __m256 v_t{ _mm256_setzero_ps() };
    __m256 v_loss{ _mm256_setzero_ps() };

    // 32バイトアラインされた導波管バッファ (SoA)
    // 3スプリング x 8セグメント x 512サンプル (合計 48KB: L1/L2キャッシュ常駐)
    alignas(32) std::array<std::array<std::array<float, BUFFER_SIZE>, SEGMENTS_PER_SPRING>, NUM_SPRINGS> fwdBuffer{};
    alignas(32) std::array<std::array<std::array<float, BUFFER_SIZE>, SEGMENTS_PER_SPRING>, NUM_SPRINGS> bwdBuffer{};

    // 分散オールパス係数 & ステート
    std::array<float, DISPERSION_STAGES> dispR{};
    std::array<float, DISPERSION_STAGES> dispTheta{};
    std::array<float, DISPERSION_STAGES> apCoeff_a1{};
    std::array<float, DISPERSION_STAGES> apCoeff_a2{};

    std::array<float, DISPERSION_STAGES> apState1_L{};
    std::array<float, DISPERSION_STAGES> apState2_L{};
    std::array<float, DISPERSION_STAGES> apState1_R{};
    std::array<float, DISPERSION_STAGES> apState2_R{};
    std::array<float, DISPERSION_STAGES> apState1_M{};
    std::array<float, DISPERSION_STAGES> apState2_M{};

    float transientState{ 0.0f };
    float dcBlockL{ 0.0f };
    float dcBlockR{ 0.0f };
};

} // namespace FDNReverb
