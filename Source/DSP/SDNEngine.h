#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>
#include <immintrin.h>

namespace FDNReverb {

class FarrowFractionalDelayLine {
public:
    void init(float* memory, int bitmask) noexcept {
        buffer = memory;
        mask = bitmask;
        writeIndex = 0;
    }

    // 4点サンプルおよび小数部 d の抽出 (AVX2 SIMD 用ヘルパー)
    inline void fetchSamples(float delayInSamples, float& ym1, float& y0, float& y1, float& y2, float& d) const noexcept {
        if (buffer == nullptr || mask < 5) {
            ym1 = y0 = y1 = y2 = d = 0.0f;
            return;
        }
        const float safeDelay = std::clamp(delayInSamples, 1.0f, static_cast<float>(mask - 4));
        const int id = static_cast<int>(safeDelay);
        d = safeDelay - static_cast<float>(id);

        const uint32_t uW = static_cast<uint32_t>(writeIndex);
        const uint32_t uD = static_cast<uint32_t>(id);
        const uint32_t uM = static_cast<uint32_t>(mask);

        ym1 = buffer[(uW - uD + 1) & uM];
        y0  = buffer[(uW - uD)     & uM];
        y1  = buffer[(uW - uD - 1) & uM];
        y2  = buffer[(uW - uD - 2) & uM];
    }

    inline float read(float delayInSamples) const noexcept {
        if (buffer == nullptr || mask < 5) return 0.0f;
        const float safeDelay = std::clamp(delayInSamples, 1.0f, static_cast<float>(mask - 4));
        const int id = static_cast<int>(safeDelay);
        const float d = safeDelay - static_cast<float>(id);

        const uint32_t uW = static_cast<uint32_t>(writeIndex);
        const uint32_t uD = static_cast<uint32_t>(id);
        const uint32_t uM = static_cast<uint32_t>(mask);

        const float ym1 = buffer[(uW - uD + 1) & uM];
        const float y0  = buffer[(uW - uD)     & uM];
        const float y1  = buffer[(uW - uD - 1) & uM];
        const float y2  = buffer[(uW - uD - 2) & uM];

        const float dm1 = d - 1.0f;
        const float dm2 = d - 2.0f;
        const float dp1 = d + 1.0f;

        const float hm1 = (-d * dm1 * dm2) * (1.0f / 6.0f);
        const float h0  = (dp1 * dm1 * dm2) * 0.5f;
        const float h1  = (-dp1 * d * dm2)  * 0.5f;
        const float h2  = (dp1 * d * dm1)   * (1.0f / 6.0f);

        return hm1 * ym1 + h0 * y0 + h1 * y1 + h2 * y2;
    }

    inline void write(float input) noexcept {
        if (buffer == nullptr) return;
        buffer[writeIndex] = input;
        writeIndex = (writeIndex + 1) & mask;
    }

    float* getBuffer() const noexcept { return buffer; }
    int getMask() const noexcept { return mask; }
    int getWriteIndex() const noexcept { return writeIndex; }

private:
    float* buffer{ nullptr };
    int mask{ 0 };
    int writeIndex{ 0 };
};

class BrownianModulator {
public:
    void prepare(double sampleRate) noexcept {
        fs = sampleRate;
        maxSlewPerSample = static_cast<float>(0.0005777 * (48000.0 / sampleRate));
        currentValue = 0.0f;
        targetValue = 0.0f;
    }

    void reset() noexcept {
        currentValue = 0.0f;
        targetValue = 0.0f;
    }

    inline float tick(float depthInSamples, float rate) noexcept {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        float noise = static_cast<float>(static_cast<int32_t>(rngState)) * (1.0f / 2147483648.0f);
        targetValue += noise * rate * 0.01f;
        targetValue = std::clamp(targetValue, -depthInSamples, depthInSamples);

        float diff = targetValue - currentValue;
        if (diff > maxSlewPerSample) diff = maxSlewPerSample;
        else if (diff < -maxSlewPerSample) diff = -maxSlewPerSample;

        currentValue += diff;
        currentValue += 1e-18f;
        currentValue -= 1e-18f;

        return currentValue;
    }

private:
    double fs{ 48000.0 };
    float maxSlewPerSample{ 0.0005777f };
    float currentValue{ 0.0f };
    float targetValue{ 0.0f };
    uint32_t rngState{ 0x12345678 };
};

class alignas(32) SDNShoebox3D {
public:
    static constexpr int NUM_NODES = 6;
    static constexpr float SOUND_SPEED = 343.0f;

    void prepare(double sampleRate, int) {
        fs = sampleRate;
        for (int i = 0; i < NUM_NODES; ++i) modulators[i].prepare(sampleRate);
    }

    void reset() {
        for (int i = 0; i < NUM_NODES; ++i) {
            modulators[i].reset();
            nodeStates[i] = 0.0;
            lpfState[i] = 0.0f;
            smoothedDelaySamples[i] = baseDelaySamples[i];
        }
        lpfState[6] = 0.0f;
        lpfState[7] = 0.0f;
        baseDelaySamples[6] = 0.0f;
        baseDelaySamples[7] = 0.0f;
        smoothedDelaySamples[6] = 0.0f;
        smoothedDelaySamples[7] = 0.0f;
    }

    void updateGeometry(float width, float depth, float height,
                        float srcX, float srcY, float srcZ,
                        float lisX, float lisY, float lisZ) {
        float wallX[6] = { 0, width, srcX, srcX, srcX, srcX };
        float wallY[6] = { srcY, srcY, 0, height, srcY, srcY };
        float wallZ[6] = { srcZ, srcZ, srcZ, srcZ, 0, depth };
        const float fsf = static_cast<float>(fs);

        for (int i = 0; i < NUM_NODES; ++i) {
            float dx1 = srcX - wallX[i], dy1 = srcY - wallY[i], dz1 = srcZ - wallZ[i];
            float d1 = std::sqrt(dx1*dx1 + dy1*dy1 + dz1*dz1);
            float dx2 = lisX - wallX[i], dy2 = lisY - wallY[i], dz2 = lisZ - wallZ[i];
            float d2 = std::sqrt(dx2*dx2 + dy2*dy2 + dz2*dz2);

            float delaySamples = ((d1 + d2) / SOUND_SPEED) * fsf;
            static constexpr float DITHER[6] = {
                1.0f, 1.0f + 0.0314159f, 1.0f - 0.0271828f,
                1.0f + 0.0173205f, 1.0f - 0.0223607f, 1.0f + 0.0141421f
            };
            delaySamples *= DITHER[i];
            baseDelaySamples[i] = std::max(3.0f, delaySamples);
        }
        baseDelaySamples[6] = 0.0f;
        baseDelaySamples[7] = 0.0f;
    }

    inline void processOneSample(float inputL, float inputR, float& outL, float& outR) noexcept {
        const float mid = (inputL + inputR) * 0.5f;
        const float side = (inputL - inputR) * 0.5f;

        // 1. 各遅延線のサンプルおよび小数部 d を取得
        alignas(32) float ym1Array[8] = { 0.0f };
        alignas(32) float y0Array[8]  = { 0.0f };
        alignas(32) float y1Array[8]  = { 0.0f };
        alignas(32) float y2Array[8]  = { 0.0f };
        alignas(32) float dArray[8]   = { 0.0f };

        for (int i = 0; i < NUM_NODES; ++i) {
            smoothedDelaySamples[i] += 0.0004f * (baseDelaySamples[i] - smoothedDelaySamples[i]);
            float modOffset = modulators[i].tick(modDepth, modRate);
            float targetDelay = std::max(smoothedDelaySamples[i] + modOffset, 3.0f);
            delayLines[i].fetchSamples(targetDelay, ym1Array[i], y0Array[i], y1Array[i], y2Array[i], dArray[i]);
        }

        // 2. AVX2 SIMD 並列 Farrow 3次ラグランジュ補間
        __m256 vD     = _mm256_load_ps(dArray);
        __m256 vOne   = _mm256_set1_ps(1.0f);
        __m256 vTwo   = _mm256_set1_ps(2.0f);
        __m256 vHalf  = _mm256_set1_ps(0.5f);
        __m256 vSixth = _mm256_set1_ps(1.0f / 6.0f);

        __m256 dm1 = _mm256_sub_ps(vD, vOne);
        __m256 dm2 = _mm256_sub_ps(vD, vTwo);
        __m256 dp1 = _mm256_add_ps(vD, vOne);

        // ラグランジュ多項式重み (現行スカラーと数学的に完全一致)
        // hm1 = (-d * dm1 * dm2) * (1/6)
        __m256 hm1 = _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(_mm256_setzero_ps(), vD), dm1), _mm256_mul_ps(dm2, vSixth));
        // h0 = (dp1 * dm1 * dm2) * 0.5
        __m256 h0  = _mm256_mul_ps(_mm256_mul_ps(dp1, dm1), _mm256_mul_ps(dm2, vHalf));
        // h1 = (-dp1 * d * dm2) * 0.5
        __m256 h1  = _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(_mm256_setzero_ps(), dp1), vD), _mm256_mul_ps(dm2, vHalf));
        // h2 = (dp1 * d * dm1) * (1/6)
        __m256 h2  = _mm256_mul_ps(_mm256_mul_ps(dp1, vD), _mm256_mul_ps(dm1, vSixth));

        __m256 vYm1 = _mm256_load_ps(ym1Array);
        __m256 vY0  = _mm256_load_ps(y0Array);
        __m256 vY1  = _mm256_load_ps(y1Array);
        __m256 vY2  = _mm256_load_ps(y2Array);

        // FMA 積和補間: hm1*ym1 + h0*y0 + h1*y1 + h2*y2
        __m256 x = _mm256_mul_ps(hm1, vYm1);
        x = _mm256_fmadd_ps(h0, vY0, x);
        x = _mm256_fmadd_ps(h1, vY1, x);
        x = _mm256_fmadd_ps(h2, vY2, x);

        // ダミーノード (ch 6, 7) の厳密ゼロマスク (エネルギー漏洩防止)
        static const __m256i activeMask = _mm256_set_epi32(0, 0, -1, -1, -1, -1, -1, -1);
        x = _mm256_and_ps(x, _mm256_castsi256_ps(activeMask));

        // 3. 超低レイテンシ Householder 6x6 散乱 (最速水平加算シーケンス)
        // x の 8要素中、ch 6, 7 は 0 であるため、8要素総和 = 6ノード総和
        __m128 lo = _mm256_castps256_ps128(x);
        __m128 hi = _mm256_extractf128_ps(x, 1);
        __m128 sum128 = _mm_add_ps(lo, hi);
        __m128 shuf1  = _mm_movehl_ps(sum128, sum128);
        __m128 sum64  = _mm_add_ps(sum128, shuf1);
        __m128 shuf2  = _mm_shuffle_ps(sum64, sum64, _MM_SHUFFLE(1, 1, 1, 1));
        __m128 totalSum = _mm_add_ss(sum64, shuf2);

        // N=6 の Householder 係数: 2 / 6 = 1 / 3
        __m256 factor = _mm256_set1_ps(0.33333334f);
        __m256 scaledSum = _mm256_mul_ps(_mm256_broadcastss_ps(totalSum), factor);

        // y = scaledSum - x
        __m256 scattered = _mm256_sub_ps(scaledSum, x);
        scattered = _mm256_and_ps(scattered, _mm256_castsi256_ps(activeMask));

        // NaN/Inf & デノーマル保護クランプ (-10.0f ~ +10.0f)
        scattered = _mm256_max_ps(_mm256_set1_ps(-10.0f), _mm256_min_ps(_mm256_set1_ps(10.0f), scattered));

        // 4. AVX2 8ch 並列 Mid/Side 注入 ＆ 1-pole 壁面吸音 LPF ＆ ダンピング
        __m256 inject = _mm256_set_ps(0.0f, 0.0f, side, mid, side, mid, side, mid);
        __m256 inScale = _mm256_set1_ps(0.40824829f); // 1 / sqrt(6)
        __m256 rawOut = _mm256_fmadd_ps(inject, inScale, scattered);

        __m256 vLpfCoeff = _mm256_set1_ps(lpfCoeff);
        __m256 vLpfState = _mm256_load_ps(lpfState.data());
        vLpfState = _mm256_fmadd_ps(vLpfCoeff, _mm256_sub_ps(rawOut, vLpfState), vLpfState);
        _mm256_store_ps(lpfState.data(), vLpfState);

        __m256 toWrite = _mm256_mul_ps(vLpfState, _mm256_set1_ps(damping));
        alignas(32) float writeVals[8];
        _mm256_store_ps(writeVals, toWrite);

        for (int i = 0; i < NUM_NODES; ++i) {
            delayLines[i].write(writeVals[i]);
        }

        // 5. ステレオ直交射影出力
        alignas(32) float sc[8];
        _mm256_store_ps(sc, scattered);
        float sumMid = sc[0] + sc[2] + sc[4];
        float sumSide = sc[1] + sc[3] + sc[5];
        constexpr float outScale = 0.57735027f; // 1 / sqrt(3)
        outL = (sumMid + sumSide) * outScale;
        outR = (sumMid - sumSide) * outScale;
    }

    inline void tickModulatorsOnly() noexcept {
        for (int i = 0; i < NUM_NODES; ++i) {
            modulators[i].tick(modDepth, modRate);
        }
    }

    float modDepth{ 0.5f };
    float modRate{ 0.3f };
    float damping{ 0.95f };
    float lpfCoeff{ 1.0f }; // 1.0 = bypass
    FarrowFractionalDelayLine delayLines[6];

private:
    double fs{ 48000.0 };
    alignas(32) std::array<float, 8> baseDelaySamples{};
    alignas(32) std::array<float, 8> smoothedDelaySamples{};
    std::array<double, 6> nodeStates{};
    alignas(32) std::array<float, 8> lpfState{};
    std::array<BrownianModulator, 6> modulators;
};

} // namespace FDNReverb
