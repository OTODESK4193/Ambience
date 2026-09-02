#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <array>

namespace FDNReverb {

class FarrowFractionalDelayLine {
public:
    void init(float* memory, int bitmask) noexcept {
        buffer = memory;
        mask = bitmask;
        writeIndex = 0;
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

class SDNShoebox3D {
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
        }
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
    }

    inline void processOneSample(float inputL, float inputR, float& outL, float& outR) noexcept {
        const float mid = (inputL + inputR) * 0.5f;
        const float side = (inputL - inputR) * 0.5f;

        float delayOutputs[6] = {};
        for (int i = 0; i < NUM_NODES; ++i) {
            float modOffset = modulators[i].tick(modDepth, modRate);
            delayOutputs[i] = delayLines[i].read(std::max(baseDelaySamples[i] + modOffset, 3.0f));
        }

        double sum = 0.0;
        for (int i = 0; i < NUM_NODES; ++i) sum += static_cast<double>(delayOutputs[i]);
        const double scatterCoeff = 2.0 / static_cast<double>(NUM_NODES);

        float scattered[6] = {};
        for (int i = 0; i < NUM_NODES; ++i) {
            double yi = scatterCoeff * sum - static_cast<double>(delayOutputs[i]);
            if (std::isnan(yi) || std::isinf(yi)) yi = 0.0;
            if (yi > 10.0) yi = 10.0; else if (yi < -10.0) yi = -10.0;
            scattered[i] = static_cast<float>(yi);
            nodeStates[i] = yi;
        }

        const float inputScale = 1.0f / std::sqrt(static_cast<float>(NUM_NODES));
        for (int i = 0; i < NUM_NODES; ++i) {
            float injection = (i % 2 == 0) ? mid : side;
            float rawOut = injection * inputScale + scattered[i];
            
            // 1-pole LPF for wall absorption
            lpfState[i] += lpfCoeff * (rawOut - lpfState[i]);
            
            // Apply global damping
            delayLines[i].write(lpfState[i] * damping);
        }

        float sumMid = scattered[0] + scattered[2] + scattered[4];
        float sumSide = scattered[1] + scattered[3] + scattered[5];
        const float outScale = 1.0f / std::sqrt(3.0f);
        outL = (sumMid + sumSide) * outScale;
        outR = (sumMid - sumSide) * outScale;
    }

    float modDepth{ 0.5f };
    float modRate{ 0.3f };
    float damping{ 0.95f };
    float lpfCoeff{ 1.0f }; // 1.0 = bypass
    FarrowFractionalDelayLine delayLines[6];

private:
    double fs{ 48000.0 };
    std::array<float, 6> baseDelaySamples{};
    std::array<double, 6> nodeStates{};
    std::array<float, 6> lpfState{};
    std::array<BrownianModulator, 6> modulators;
};

} // namespace FDNReverb
