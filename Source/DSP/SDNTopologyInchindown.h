#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>
#include <immintrin.h>

namespace FDNReverb {

/**
 * @brief 世界最長112秒残響タンク専用 SDN コアエンジン
 * (Inchindown Short-Axis Boost Anisotropic SDN + Long-Axis Air Absorption Waveguide)
 */
class SDNTopologyInchindown {
public:
    static constexpr int NUM_NODES = 6;
    static constexpr float SOUND_SPEED = 337.33f; // 10℃ 坑道音速 (常温時は 343.0f)

    // バッファサイズ定義 (192kHz, サイズスケール2.5倍対応)
    // 短軸 (9m〜13.5m): 192kHz * 2.5x -> 最大約 18,900 samples -> 2^15 = 32,768 (マスク 0x7FFF)
    static constexpr int SHORT_BUFFER_SIZE = 32768;
    static constexpr int SHORT_BUFFER_MASK = SHORT_BUFFER_SIZE - 1;
    // 長軸 (237m): 192kHz * 2.5x -> 最大約 332,000 samples -> 2^19 = 524,288 (マスク 0x7FFFF)
    static constexpr int LONG_BUFFER_SIZE  = 524288;
    static constexpr int LONG_BUFFER_MASK  = LONG_BUFFER_SIZE - 1;

    SDNTopologyInchindown() noexcept {
        reset();
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
        fs = sampleRate;
        const float fsf = static_cast<float>(sampleRate);

        // 基本物理寸法 (m)
        // ノード 0, 1: X軸 (幅 9m)
        // ノード 2, 3: Z軸 (高さ 13.5m)
        // ノード 4, 5: Y軸 (長さ 237m)
        static constexpr float BASE_DISTANCES[6] = {
            9.0f,  9.0f * 1.0314159f,     // X軸 (ディザー摂動)
            13.5f, 13.5f * 0.9728182f,    // Z軸 (ディザー摂動)
            237.0f, 237.0f * 1.0173205f   // Y軸 (ディザー摂動)
        };

        for (int i = 0; i < 6; ++i) {
            geomDistances[i] = BASE_DISTANCES[i];
            baseDelays[i] = (BASE_DISTANCES[i] / SOUND_SPEED) * fsf;
        }

        // 短軸・長軸バッファメモリ確保 (ゼロ初期化)
        for (int i = 0; i < 4; ++i)
            shortBuffer[i].assign(SHORT_BUFFER_SIZE, 0.0f);
        for (int i = 0; i < 2; ++i)
            longBuffer[i].assign(LONG_BUFFER_SIZE, 0.0f);

        setParameters(112.0f, 0.25f, 1.0f, 1.0f);
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < 4; ++i)
            std::fill(shortBuffer[i].begin(), shortBuffer[i].end(), 0.0f);
        for (int i = 0; i < 2; ++i)
            std::fill(longBuffer[i].begin(), longBuffer[i].end(), 0.0f);

        writeIndexShort = 0;
        writeIndexLong  = 0;
        v_airState = _mm256_setzero_ps();
        dcBlockL = dcBlockR = 0.0f;
    }

    /**
     * @brief 物理パラメータの設定
     * @param rt60 減衰時間 (0.5秒 〜 112秒)
     * @param hfDamping 高域ダンピング [0.0, 1.0]
     * @param roomSizeScale 空間スケーリング [0.5, 2.5]
     * @param airAbsorption 空気吸収強調度 [0.0, 2.0]
     */
    void setParameters(float rt60, float hfDamping, float roomSizeScale, float airAbsorption = 1.0f) noexcept {
        const float fsf = static_cast<float>(fs);
        const float sizeScale = std::clamp(roomSizeScale, 0.5f, 2.5f);
        const float clampedRt60 = std::clamp(rt60, 0.5f, 120.0f);

        // 1. 遅延サンプル数更新
        for (int i = 0; i < 4; ++i) {
            const float dSmp = (geomDistances[i] * sizeScale / SOUND_SPEED) * fsf;
            delaySamplesShort[i] = std::clamp(static_cast<int>(dSmp), 4, SHORT_BUFFER_SIZE - 4);
        }
        for (int i = 0; i < 2; ++i) {
            const float dSmp = (geomDistances[4 + i] * sizeScale / SOUND_SPEED) * fsf;
            delaySamplesLong[i] = std::clamp(static_cast<int>(dSmp), 4, LONG_BUFFER_SIZE - 4);
        }

        // 2. 幾何学的表面積に基づく非等方 Householder 重みベクトルの計算
        // S_x = 2 * (Ly * Lz) = 2 * (237 * 13.5) = 6399 m^2
        // S_z = 2 * (Lx * Ly) = 2 * (9 * 237)    = 4266 m^2
        // S_y = 2 * (Lx * Lz) = 2 * (9 * 13.5)   = 243 m^2
        // S_total = 10908 m^2
        const float Ly = geomDistances[4] * sizeScale;
        const float Lz = geomDistances[2] * sizeScale;
        const float Lx = geomDistances[0] * sizeScale;

        const float Sx = 2.0f * Ly * Lz;
        const float Sz = 2.0f * Lx * Ly;
        const float Sy = 2.0f * Lx * Lz;
        const float S_total = Sx + Sz + Sy;

        const float wx = std::sqrt(Sx * 0.5f);
        const float wz = std::sqrt(Sz * 0.5f);
        const float wy = std::sqrt(Sy * 0.5f);

        // 重み付きハウスホルダーベクトルの正規化: ||v||^2 = 2.0
        // S = v * v^T - I (ユニタリ直交行列: S^T * S = I)
        const float normFactor = std::sqrt(2.0f / S_total);
        alignas(32) float u_arr[8] = {
            wx * normFactor, wx * normFactor,
            wz * normFactor, wz * normFactor,
            wy * normFactor, wy * normFactor,
            0.0f, 0.0f
        };
        v_u = _mm256_load_ps(u_arr);

        // 3. ISO 9613-1 大気分子緩和吸収フィルタ係数計算
        alignas(32) float b0_arr[8] = { 0.0f };
        alignas(32) float a1_arr[8] = { 0.0f };

        // コンクリート壁面反射損失 (低域112秒を維持するため 0.999f 以上の極低損失)
        const float dbPerSec = -60.0f / clampedRt60;

        for (int i = 0; i < 6; ++i) {
            const float dist = geomDistances[i] * sizeScale;
            // 距離連動カットオフ周波数 (ISO 9613-1 近似)
            // 短軸: ~12-14kHz, 長軸: ~900-1200Hz
            const float distFactor = dist * (0.05f + 0.05f * airAbsorption + 0.05f * hfDamping);
            const float fc = std::clamp(16000.0f / (1.0f + distFactor), 300.0f, 18000.0f);
            const float omega = 2.0f * 3.14159265f * fc / fsf;
            const float a1 = std::exp(-omega);

            // 伝播距離に伴う広帯域損失
            const float travelTime = dist / SOUND_SPEED;
            const float broadbandGain = std::pow(10.0f, (dbPerSec * travelTime) / 20.0f);

            a1_arr[i] = a1;
            b0_arr[i] = (1.0f - a1) * broadbandGain;
        }

        v_airB0 = _mm256_load_ps(b0_arr);
        v_airA1 = _mm256_load_ps(a1_arr);
    }

    /**
     * @brief 1サンプル処理 (AVX2 非等方散乱 + 空気吸収 + 16ch FDN 直交射影)
     * @param inL, inR ステレオ入力
     * @param outL, outR ステレオ ER 出力
     * @param fdn16Out 16ch FDNへの早期直交注入バッファ (float[16])
     */
    inline void processOneSample(float inL, float inR, float& outL, float& outR, float* fdn16Out) noexcept {
        const int wIdxS = writeIndexShort;
        const int wIdxL = writeIndexLong;

        // 1. 遅延バッファからの読み出し (SoA ベクトルロード)
        alignas(32) float x_in[8];
        // 短軸 4ノード (X1, X2, Z1, Z2)
        for (int i = 0; i < 4; ++i) {
            const int rIdx = (wIdxS - delaySamplesShort[i]) & SHORT_BUFFER_MASK;
            x_in[i] = shortBuffer[i][rIdx];
        }
        // 長軸 2ノード (Y1, Y2)
        for (int i = 0; i < 2; ++i) {
            const int rIdx = (wIdxL - delaySamplesLong[i]) & LONG_BUFFER_MASK;
            x_in[4 + i] = longBuffer[i][rIdx];
        }
        x_in[6] = 0.0f;
        x_in[7] = 0.0f;

        // 入力注入: 短軸に Mid/Side を 85%、長軸に 15% を直交加振
        const float mid = (inL + inR) * 0.5f;
        const float side = (inL - inR) * 0.5f;
        x_in[0] += mid  * 0.60f;
        x_in[1] -= mid  * 0.60f;
        x_in[2] += side * 0.60f;
        x_in[3] -= side * 0.60f;
        x_in[4] += mid  * 0.15f;
        x_in[5] -= side * 0.15f;

        const __m256 v_x = _mm256_load_ps(x_in);

        // 2. AVX2 重み付き Householder 散乱 (Unitary Weighted Scattering)
        // y = (v^T x) * v - x  (||v||^2 = 2.0)
        const __m256 vProd = _mm256_mul_ps(v_x, v_u);
        const __m256 hsum1 = _mm256_hadd_ps(vProd, vProd);
        const __m256 hsum2 = _mm256_hadd_ps(hsum1, hsum1);
        const __m128 lo    = _mm256_castps256_ps128(hsum2);
        const __m128 hi    = _mm256_extractf128_ps(hsum2, 1);
        const __m128 dot128 = _mm_add_ss(lo, hi);
        const __m256 vDot   = _mm256_broadcastss_ps(dot128);

        // y = dot * v - x (FMA 命令: _mm256_fmsub_ps)
        const __m256 v_scattered = _mm256_fmsub_ps(vDot, v_u, v_x);

        // 3. AVX2 距離連動 空気吸収フィルタ (並列 1-pole Direct Form I)
        // y[n] = b0 * x[n] + a1 * y[n-1]
        const __m256 v_filtered = _mm256_fmadd_ps(v_airB0, v_scattered, _mm256_mul_ps(v_airA1, v_airState));
        v_airState = v_filtered;

        // セーフティ機構: ブランチレス発振防止クランプ (-8.0f 〜 +8.0f)
        const __m256 vClampMin = _mm256_set1_ps(-8.0f);
        const __m256 vClampMax = _mm256_set1_ps(8.0f);
        const __m256 vSafe = _mm256_min_ps(_mm256_max_ps(v_filtered, vClampMin), vClampMax);

        alignas(32) float feedback[8];
        _mm256_store_ps(feedback, vSafe);

        // 4. バッファ書き込み
        for (int i = 0; i < 4; ++i)
            shortBuffer[i][wIdxS] = feedback[i];
        for (int i = 0; i < 2; ++i)
            longBuffer[i][wIdxL] = feedback[4 + i];

        writeIndexShort = (wIdxS + 1) & SHORT_BUFFER_MASK;
        writeIndexLong  = (wIdxL + 1) & LONG_BUFFER_MASK;

        // 5. 16ch FDN 早期直交注入マトリックス
        // 短軸の高速パルス (0..3) と長軸スロッシング (4..5) を直交合成
        static constexpr float INCH_SIGN[16] = {
            1.0f, -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f, -1.0f,
           -1.0f,  1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,  1.0f
        };

        const float taps[16] = {
            feedback[0], feedback[1], feedback[2], feedback[3],
            feedback[0] - feedback[1], feedback[2] - feedback[3], feedback[4], feedback[5],
            feedback[4] + feedback[0], feedback[5] + feedback[1], feedback[2], feedback[3],
            feedback[4], feedback[5], feedback[0] + feedback[2], feedback[1] + feedback[3]
        };

        for (int k = 0; k < 16; ++k) {
            fdn16Out[k] = taps[k] * INCH_SIGN[k] * 0.353553f; // 1/sqrt(8)
        }

        // 6. ステレオ ER 出力 (X軸差分=Side, Z軸和=Mid, Y軸=長軸アンビエンス)
        const float erMid  = (feedback[2] + feedback[3]) * 0.7071f + (feedback[4] + feedback[5]) * 0.30f;
        const float erSide = (feedback[0] - feedback[1]) * 0.7071f + (feedback[4] - feedback[5]) * 0.20f;

        const float rawOutL = erMid + erSide;
        const float rawOutR = erMid - erSide;

        // リーキーDCブロッカー (3.0Hz)
        dcBlockL += 0.001f * (rawOutL - dcBlockL);
        dcBlockR += 0.001f * (rawOutR - dcBlockR);
        outL = rawOutL - dcBlockL;
        outR = rawOutR - dcBlockR;
    }

private:
    double fs{ 48000.0 };
    int writeIndexShort{ 0 };
    int writeIndexLong{ 0 };

    std::array<float, 6> geomDistances{};
    std::array<float, 6> baseDelays{};
    std::array<int, 4> delaySamplesShort{};
    std::array<int, 2> delaySamplesLong{};

    std::vector<float> shortBuffer[4];
    std::vector<float> longBuffer[2];

    __m256 v_u{ _mm256_setzero_ps() };
    __m256 v_airB0{ _mm256_setzero_ps() };
    __m256 v_airA1{ _mm256_setzero_ps() };
    __m256 v_airState{ _mm256_setzero_ps() };

    float dcBlockL{ 0.0f };
    float dcBlockR{ 0.0f };
};

} // namespace FDNReverb
