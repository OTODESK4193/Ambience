#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace FDNReverb {

    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    // 邨ｱ蜷医Γ繝｢繝ｪ繝励・繝ｫ (Single-Large Buffer)
    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    class DelayMemoryPool {
    public:
        void allocate(size_t totalSamples) {
            buffer.assign(totalSamples, 0.0f);
            allocOffset = 0;
        }

        // 隕∵ｱゅ＆繧後◆繧ｵ繧､繧ｺ繧偵梧ｬ｡縺ｮ2縺ｮ縺ｹ縺堺ｹ励阪↓蛻・ｊ荳翫￡縺ｦ繝昴う繝ｳ繧ｿ繧定ｿ斐☆・磯ｫ倬溘↑繝槭せ繧ｯ貍皮ｮ励・縺溘ａ・・
        float* requestMemory(size_t samplesNeeded, int& outMask) {
            size_t powerOfTwoSize = 1;
            while (powerOfTwoSize < samplesNeeded) powerOfTwoSize *= 2;

            if (allocOffset + powerOfTwoSize > buffer.size()) return nullptr;

            float* ptr = buffer.data() + allocOffset;
            outMask = static_cast<int>(powerOfTwoSize - 1);
            allocOffset += powerOfTwoSize;

            return ptr;
        }

        void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); }

    private:
        std::vector<float> buffer;
        size_t allocOffset{ 0 };
    };

    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    // 鬮倬溘Μ繝九い陬憺俣繝・ぅ繝ｬ繧､繝ｩ繧､繝ｳ
    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    class LinearDelayLine {
    public:
        void resetState() noexcept {}
        void init(float* memory, int bitmask) {
            buffer = memory;
            mask = bitmask;
            writeIndex = 0;
        }

        // 邱壼ｽ｢陬憺俣・磯ｫ伜沺縺ｮ閾ｪ辟ｶ縺ｪAir Absorption繧堤函繧・・
        inline float read(float delayInSamples) const noexcept {
            int id = static_cast<int>(delayInSamples);
            float frac = delayInSamples - static_cast<float>(id);

            // 雋縺ｮ謨ｰ縺ｫ蟇ｾ縺吶ｋ繝薙ャ繝域ｼ皮ｮ励・譛ｪ螳夂ｾｩ蜍穂ｽ懊ｒ螳悟・縺ｫ髦ｲ縺舌◆繧√「int32_t縺ｧ繝ｩ繝・・繧｢繝ｩ繧ｦ繝ｳ繝峨＆縺帙ｋ
            uint32_t uWrite = static_cast<uint32_t>(writeIndex);
            uint32_t uId = static_cast<uint32_t>(id);
            uint32_t uMask = static_cast<uint32_t>(mask);

            int readIdx1 = static_cast<int>((uWrite - uId) & uMask);
            int readIdx2 = static_cast<int>((uWrite - uId - 1) & uMask);

            return buffer[readIdx1] + frac * (buffer[readIdx2] - buffer[readIdx1]);
        }

        inline void write(float input) noexcept {
            buffer[writeIndex] = input;
            writeIndex = (writeIndex + 1) & mask;
        }

    private:
        float* buffer{ nullptr };
        int mask{ 0 };
        int writeIndex{ 0 };
    };

    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    // Thiran Allpass陬憺俣繝・ぅ繝ｬ繧､繝ｩ繧､繝ｳ・医ヵ繝ｩ繝・ヨ菴咲嶌蠢懃ｭ費ｼ・
    //   邱壼ｽ｢陬憺俣縺ｯ鬮伜沺繧呈ｸ幄｡ｰ縺輔○繧具ｼ・inc(ﾏf)迚ｹ諤ｧ・峨′縲ゝhiran allpass縺ｯ
    //   |H(ﾏ・|=1 繧堤ｶｭ謖√☆繧九◆繧√：DN繝輔ぅ繝ｼ繝峨ヰ繝・け繝ｫ繝ｼ繝怜・縺ｮ鬮伜沺騾乗・諢溘′蜷台ｸ翫・
    // 笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏笏
    class ThiranDelayLine {
    public:
        void init(float* memory, int bitmask) {
            buffer = memory;
            mask = bitmask;
            writeIndex = 0;
            thiranX1 = 0.0f;
            thiranY1 = 0.0f;
        }

        void resetState() noexcept {
            thiranX1 = 0.0f;
            thiranY1 = 0.0f;
        }

        // Thiran 1谺｡ allpass: y[n] = a*x[n] + x[n-1] - a*y[n-1]
        // a = (1-D)/(1+D), D = fractional delay
        inline float read(float delayInSamples) noexcept {
            int id = static_cast<int>(delayInSamples);
            float frac = delayInSamples - static_cast<float>(id);

            // frac竊・ 縺ｧ a竊・ (荳榊ｮ牙ｮ・ 縺ｮ縺溘ａ荳矩剞繧ｯ繝ｩ繝ｳ繝・
            frac = std::max(frac, 0.1f);
            const float a = (1.0f - frac) / (1.0f + frac);

            uint32_t uWrite = static_cast<uint32_t>(writeIndex);
            uint32_t uId = static_cast<uint32_t>(id);
            uint32_t uMask = static_cast<uint32_t>(mask);

            float xn = buffer[static_cast<int>((uWrite - uId) & uMask)];

            float yn = a * xn + thiranX1 - a * thiranY1;
            thiranX1 = xn;
            thiranY1 = yn;

            return yn;
        }

        inline void write(float input) noexcept {
            buffer[writeIndex] = input;
            writeIndex = (writeIndex + 1) & mask;
        }

    private:
        float* buffer{ nullptr };
        int mask{ 0 };
        int writeIndex{ 0 };
        float thiranX1{ 0.0f };
        float thiranY1{ 0.0f };
    };

} // namespace FDNReverb