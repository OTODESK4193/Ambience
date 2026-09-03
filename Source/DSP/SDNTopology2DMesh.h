#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <immintrin.h>

namespace FDNReverb {

/**
 * @brief 2次オールパス分散フィルタ (Bending Dispersion Biquad)
 * EMT 140 鋼板の曲げ波異常分散 (vg ∝ √ω) による周波数依存群遅延をシミュレート
 */
class DispersionAllpass2nd {
public:
    void reset() noexcept {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void setCoefficients(float poleRadius, float poleAngle) noexcept {
        // 2次オールパス伝達関数: H(z) = (r^2 - 2 r cos(th) z^-1 + z^-2) / (1 - 2 r cos(th) z^-1 + r^2 z^-2)
        const float r = std::clamp(poleRadius, 0.0f, 0.98f);
        const float coeffA1 = -2.0f * r * std::cos(poleAngle);
        const float coeffA2 = r * r;

        b0 = coeffA2;
        b1 = coeffA1;
        b2 = 1.0f;
        this->a1 = coeffA1;
        this->a2 = coeffA2;
    }

    inline float tick(float x) noexcept {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        
        // 防護
        if (std::isnan(y) || std::isinf(y)) y = 0.0f;
        y = std::clamp(y, -4.0f, 4.0f);

        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

private:
    float b0{ 0.0f }, b1{ 0.0f }, b2{ 1.0f };
    float a1{ 0.0f }, a2{ 0.0f };
    float x1{ 0.0f }, x2{ 0.0f };
    float y1{ 0.0f }, y2{ 0.0f };
};

/**
 * @brief 超高速 AVX2 2D FDTD Waveguide Mesh (16x12 = 192ノード)
 * Plate (EMT 140) / Goldfoil (EMT 240) 専用 2次元物理散乱コアエンジン
 */
class SDNTopology2DMesh {
public:
    static constexpr int GRID_W = 16;
    static constexpr int GRID_H = 12;

    // メモリストライド: 横幅16 + 左右パディング = 32 float (128 bytes, 32Bアライン維持, y << 5)
    static constexpr int ROW_STRIDE = 32;
    static constexpr int HEIGHT_PADDED = GRID_H + 2; // 上下パディング行 (y=0, y=13)
    static constexpr int PLANE_SIZE = ROW_STRIDE * HEIGHT_PADDED; // 448 floats (1,792 bytes)

    // 黄金分割に基づく非対称直交加振点 (A: 正相点, B: 対角反対称・直交加振点)
    static constexpr int INJECT_X_A = 10; // 16 * 0.618 ≈ 10
    static constexpr int INJECT_Y_A = 5;  // 12 * 0.382 ≈ 5
    static constexpr int INJECT_X_B = 7;  // 16 - 10 + 1 = 7 (反対称点)
    static constexpr int INJECT_Y_B = 8;  // 12 - 5 + 1 = 8 (反対称点)

    static constexpr float QUAD_APF_COEFF = -0.965f;
    float quadApfState{ 0.0f };

    SDNTopology2DMesh() noexcept {
        reset();
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
        fs = sampleRate;
        for (int i = 0; i < 4; ++i) {
            dispersionL[i].reset();
            dispersionR[i].reset();
            // 低周波ほど遅延する曲げ波分散極 (r=0.6〜0.75, theta=0.1〜0.4 rad)
            dispersionL[i].setCoefficients(0.65f + 0.05f * static_cast<float>(i), 0.15f * static_cast<float>(i + 1));
            dispersionR[i].setCoefficients(0.63f + 0.05f * static_cast<float>(i), 0.17f * static_cast<float>(i + 1));
        }
        setParameters(1.5f, 0.2f, 1.0f, true);
        reset();
    }

    void reset() noexcept {
        for (int p = 0; p < 3; ++p) {
            std::fill(planes[p].begin(), planes[p].end(), 0.0f);
        }
        planeIdx = 0;
        for (int i = 0; i < 4; ++i) {
            dispersionL[i].reset();
            dispersionR[i].reset();
        }
        dcBlockL = dcBlockR = 0.0f;
        quadApfState = 0.0f;
    }

    /**
     * @brief 物理パラメータの設定
     * @param rt60 減衰時間（秒）
     * @param hfDamping 高域ダンピング [0.0, 1.0]
     * @param sizeScale プレートサイズスケーリング [0.5, 2.0]
     * @param isPlate true: EMT 140 (鋼板・分散あり), false: EMT 240 (金箔・張力無分散)
     */
    void setParameters(float rt60, float hfDamping, float sizeScale, bool isPlate) noexcept {
        enableDispersion = isPlate;

        const float clampedRt60 = std::clamp(rt60, 0.05f, 30.0f);
        const float sigma = 3.0f * std::log(10.0f) / clampedRt60; // -60dB減衰速度
        
        const float dt = static_cast<float>(1.0 / fs);
        // 内部ダンピング係数 alpha
        const float alpha = std::clamp(sigma * dt * (1.0f + hfDamping * 1.5f), 1e-6f, 0.05f);

        // CFL安全条件: lambda_safe = 0.700f (理論上限 1/sqrt(2) = 0.7071f に対する1%安全マージン)
        // サイズスケーリング: 仮想波速を 1/sqrt(sizeScale) で微小調整
        const float safeScale = std::clamp(sizeScale, 0.5f, 2.0f);
        const float lambda = std::clamp(0.700f / std::sqrt(safeScale), 0.35f, 0.7070f);
        const float lambdaSq = lambda * lambda;

        const float denom = 1.0f / (1.0f + alpha);
        c_center   = (2.0f - 4.0f * lambdaSq) * denom;
        c_prev     = -(1.0f - alpha) * denom;
        c_neighbor = lambdaSq * denom;

        v_c_center   = _mm256_set1_ps(c_center);
        v_c_prev     = _mm256_set1_ps(c_prev);
        v_c_neighbor = _mm256_set1_ps(c_neighbor);

        // 境界反射係数 (高域ダンピング連動)
        boundaryLoss = std::clamp(0.995f - hfDamping * 0.05f, 0.90f, 0.998f);
    }

    /**
     * @brief 1サンプル処理（AVX2 5点ステンシル + NaN/Inf自動修復 + 16ch FDN抽出）
     * @param inL, inR ステレオ入力
     * @param outL, outR ステレオ初期散乱出力
     * @param fdn16Out 16ch FDNへのダイレクト注入バッファ (float[16])
     */
    inline void processOneSample(float inL, float inR, float& outL, float& outR, float* fdn16Out) noexcept {
        float* u_next = planes[planeIdx].data();
        const int prevIdx = (planeIdx == 0) ? 2 : planeIdx - 1;
        const int pastIdx = (planeIdx == 2) ? 0 : planeIdx + 1;
        const float* u_curr = planes[prevIdx].data();
        const float* u_past = planes[pastIdx].data();

        // ─────────────────────────────────────────────────────────────
        // 1. AVX2 2D FDTD 5点ステンシル計算 (行 1 〜 12)
        // ─────────────────────────────────────────────────────────────
        const __m256 vMaxAmp = _mm256_set1_ps(4.0f);
        const __m256 absMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
        const __m256 vZero   = _mm256_setzero_ps();
        const __m256 vClampMin = _mm256_set1_ps(-2.5f);
        const __m256 vClampMax = _mm256_set1_ps(2.5f);

        for (int y = 1; y <= GRID_H; ++y) {
            const int rowOffset  = y * ROW_STRIDE;
            const int upOffset   = rowOffset - ROW_STRIDE;
            const int downOffset = rowOffset + ROW_STRIDE;

            // 横幅16ノードを 8ノード x 2ベクトル (v=0: x=1..8, v=1: x=9..16) で一括処理
            for (int v = 0; v < 2; ++v) {
                const int xOff = 1 + v * 8;
                const int idx = rowOffset + xOff;

                // 5点ステンシルロード (上下・過去・中心はアライン, 左右はアンアライン)
                const __m256 curr   = _mm256_load_ps(&u_curr[idx]);
                const __m256 u_left  = _mm256_loadu_ps(&u_curr[idx - 1]);
                const __m256 u_right = _mm256_loadu_ps(&u_curr[idx + 1]);
                const __m256 u_up    = _mm256_load_ps(&u_curr[upOffset + xOff]);
                const __m256 u_down  = _mm256_load_ps(&u_curr[downOffset + xOff]);
                const __m256 past    = _mm256_load_ps(&u_past[idx]);

                // 4近傍の和
                const __m256 sum_lr  = _mm256_add_ps(u_left, u_right);
                const __m256 sum_ud  = _mm256_add_ps(u_up, u_down);
                const __m256 sum_all = _mm256_add_ps(sum_lr, sum_ud);

                // 更新式: next = c_neighbor * sum_all + c_center * curr + c_prev * past
                __m256 next_val = _mm256_fmadd_ps(v_c_neighbor, sum_all,
                                  _mm256_fmadd_ps(v_c_center, curr,
                                  _mm256_mul_ps(v_c_prev, past)));

                // セーフティ1: ブランチレス NaN / Inf 自動修復
                const __m256 notNanMask = _mm256_cmp_ps(next_val, next_val, _CMP_EQ_OQ);
                const __m256 absVal     = _mm256_and_ps(next_val, absMask);
                const __m256 notInfMask = _mm256_cmp_ps(absVal, vMaxAmp, _CMP_LE_OQ);
                const __m256 validMask  = _mm256_and_ps(notNanMask, notInfMask);
                next_val = _mm256_blendv_ps(vZero, next_val, validMask);

                // セーフティ2: 局所エネルギー発散防止クランプ [-2.5, 2.5]
                next_val = _mm256_min_ps(_mm256_max_ps(next_val, vClampMin), vClampMax);

                _mm256_store_ps(&u_next[idx], next_val);
            }
        }

        // ─────────────────────────────────────────────────────────────
        // 2. 境界条件 (ディリクレ / 減衰反射)
        // ─────────────────────────────────────────────────────────────
        // 上端 (y=0) & 下端 (y=GRID_H+1)
        for (int x = 1; x <= GRID_W; ++x) {
            u_next[x] = u_next[ROW_STRIDE + x] * boundaryLoss * -0.95f; // 反転吸音反射
            u_next[(GRID_H + 1) * ROW_STRIDE + x] = u_next[GRID_H * ROW_STRIDE + x] * boundaryLoss * -0.95f;
        }
        // 左端 (x=0) & 右端 (x=GRID_W+1)
        for (int y = 1; y <= GRID_H; ++y) {
            u_next[y * ROW_STRIDE] = u_next[y * ROW_STRIDE + 1] * boundaryLoss * -0.95f;
            u_next[y * ROW_STRIDE + (GRID_W + 1)] = u_next[y * ROW_STRIDE + GRID_W] * boundaryLoss * -0.95f;
        }

        // ─────────────────────────────────────────────────────────────
        // 3. 非対称直交加振 (Dual Orthogonal Injection)
        // ─────────────────────────────────────────────────────────────
        const float mid  = (inL + inR) * 0.5f;
        const float side = (inL - inR) * 0.5f;

        // 1次オールパスによる直交位相信号 (Hilbert 直交成分)
        const float qMid = QUAD_APF_COEFF * mid + quadApfState;
        quadApfState = mid - QUAD_APF_COEFF * qMid;

        // 加振点 A (対称成分 + 正相 Side)
        const float injectA = (mid + side) * 0.25f;
        // 加振点 B (反対称直交成分 - 逆相 Side)
        const float injectB = (qMid - side) * 0.25f;

        u_next[INJECT_Y_A * ROW_STRIDE + INJECT_X_A] += injectA;
        u_next[INJECT_Y_B * ROW_STRIDE + INJECT_X_B] += injectB;

        // ─────────────────────────────────────────────────────────────
        // 4. 16ch 直交和差マトリックス抽出 → 16ch FDN & ステレオER
        // ─────────────────────────────────────────────────────────────
        static constexpr int PICKUP_X[16] = { 2, 6, 10, 14,  3, 7, 11, 15,  2, 5,  9, 13,  4, 8, 12, 15 };
        static constexpr int PICKUP_Y[16] = { 2, 2,  2,  2,  5, 5,  5,  5,  8, 8,  8,  8, 11, 11, 11, 11 };

        // 空間デコリレーション直交符号 (同相モード総和ゼロ保証: 全和=0, 偶奇和=0)
        static constexpr float MESH_SIGN[16] = {
            1.0f, -1.0f,  1.0f, -1.0f,
           -1.0f,  1.0f, -1.0f,  1.0f,
            1.0f,  1.0f, -1.0f, -1.0f,
           -1.0f, -1.0f,  1.0f,  1.0f
        };

        float sumMid  = 0.0f;
        float sumSide = 0.0f;

        for (int k = 0; k < 16; ++k) {
            float p = u_next[PICKUP_Y[k] * ROW_STRIDE + PICKUP_X[k]];
            
            // FDNへの直交注入 (同相成分完全キャンセル)
            fdn16Out[k] = p * MESH_SIGN[k];

            sumMid  += p * 0.25f;
            sumSide += p * MESH_SIGN[k] * 0.25f;
        }

        // Mid/Side 和差直交ステレオ合成 (エネルギー等配分: 1/sqrt(2) = 0.7071f)
        float rawOutL = (sumMid + sumSide) * 0.70710678f;
        float rawOutR = (sumMid - sumSide) * 0.70710678f;

        // ─────────────────────────────────────────────────────────────
        // 5. EMT 140 曲げ波分散オールパス (Plate時のみ直列適用)
        // ─────────────────────────────────────────────────────────────
        if (enableDispersion) {
            for (int i = 0; i < 4; ++i) {
                rawOutL = dispersionL[i].tick(rawOutL);
                rawOutR = dispersionR[i].tick(rawOutR);
            }
        }

        // DCブロッカー
        dcBlockL += 0.002f * (rawOutL - dcBlockL);
        dcBlockR += 0.002f * (rawOutR - dcBlockR);
        outL = rawOutL - dcBlockL;
        outR = rawOutR - dcBlockR;

        // ─────────────────────────────────────────────────────────────
        // 6. 3面ポインタローテーション (ゼロコピー)
        // ─────────────────────────────────────────────────────────────
        planeIdx = (planeIdx + 1);
        if (planeIdx >= 3) planeIdx = 0;
    }

private:
    double fs{ 48000.0 };
    bool enableDispersion{ true };

    float c_center{ 0.0f };
    float c_prev{ -0.998f };
    float c_neighbor{ 0.245f };
    float boundaryLoss{ 0.995f };

    __m256 v_c_center{ _mm256_setzero_ps() };
    __m256 v_c_prev{ _mm256_set1_ps(-0.998f) };
    __m256 v_c_neighbor{ _mm256_set1_ps(0.245f) };

    // 32バイトアライメントを保証した3平面バッファ (L1Dキャッシュ常駐: 5.37KB)
    alignas(32) std::array<std::array<float, PLANE_SIZE>, 3> planes{};
    int planeIdx{ 0 };

    // EMT 140 曲げ波分散オールパスカスケード (4段)
    std::array<DispersionAllpass2nd, 4> dispersionL;
    std::array<DispersionAllpass2nd, 4> dispersionR;

    float dcBlockL{ 0.0f };
    float dcBlockR{ 0.0f };
};

} // namespace FDNReverb
