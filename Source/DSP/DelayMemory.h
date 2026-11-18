#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace FDNReverb {

    // ═══════════════════════════════════════════════════════════════════════════
    // 統合メモリプール (Single-Large Buffer)
    // ═══════════════════════════════════════════════════════════════════════════
    class DelayMemoryPool {
    public:
        void allocate(size_t totalSamples) {
            buffer.assign(totalSamples, 0.0f);
            allocOffset = 0;
        }

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

    // ═══════════════════════════════════════════════════════════════════════════
    // Hermite 3次補間ディレイライン
    //   線形補間は fs/4 で -3dB のシンク・ロールオフが発生し、FDNループ内で
    //   指数関数的に高域が削れてテールが暗くなる。
    //   Hermite 3次補間は FIR ベース（モジュレーション時も安全）で、
    //   fs/3 までほぼフラットな周波数特性を持ち、シルキーな透明感を維持。
    // ═══════════════════════════════════════════════════════════════════════════
    class LinearDelayLine {
    public:
        void resetState() noexcept { writeIndex = 0; }
        void init(float* memory, int bitmask) {
            buffer = memory;
            mask = bitmask;
            writeIndex = 0;
        }

        // read() が補間読み出し可能か（buffer 割り当て済み かつ タップ4点が確保できる）
        inline bool hasBuffer() const noexcept { return buffer != nullptr && mask >= 5; }

        // read() の前処理（clamp → 整数部 id / 小数部 frac）の分離。
        // 遅延が静的なチャンネルでは (id, frac) を事前計算して使い回せる。
        // read() 自身もこの関数を使うため、得られる値は read() と完全同一。
        inline void prepareReadArgs(float delayInSamples, int& outId, float& outFrac) const noexcept {
            const float safeDelay = std::clamp(delayInSamples, 1.0f, static_cast<float>(mask - 4));
            outId = static_cast<int>(safeDelay);
            outFrac = safeDelay - static_cast<float>(outId);
        }

        // prepareReadArgs で得た (id, frac) による Hermite 補間読み出し本体。
        // 式・演算順序は従来の read() と完全に同一（ビット完全一致）。
        inline float readPrepared(int id, float frac) const noexcept {
            const uint32_t uWrite = static_cast<uint32_t>(writeIndex);
            const uint32_t uId    = static_cast<uint32_t>(id);
            const uint32_t uMask  = static_cast<uint32_t>(mask);

            // 最新サンプルは (writeIndex - 1)
            // delay = 1 -> id = 1 -> y1 = buffer[writeIndex - 2]
            const float y0 = buffer[static_cast<int>((uWrite - uId)     & uMask)];
            const float y1 = buffer[static_cast<int>((uWrite - uId - 1) & uMask)];
            const float y2 = buffer[static_cast<int>((uWrite - uId - 2) & uMask)];
            const float y3 = buffer[static_cast<int>((uWrite - uId - 3) & uMask)];

            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            return ((c3 * frac + c2) * frac + c1) * frac + c0;
        }

        inline float read(float delayInSamples) const noexcept {
            if (buffer == nullptr || mask < 5) return 0.0f;
            int id; float frac;
            prepareReadArgs(delayInSamples, id, frac);
            return readPrepared(id, frac);
        }

        inline float readInt(int delayInSamples) const noexcept {
            if (buffer == nullptr || mask < 1) return 0.0f;
            const uint32_t uWrite = static_cast<uint32_t>(writeIndex);
            // Defensive clamp: an over-long delay must degrade to the oldest
            // stored sample, never wrap modulo the mask into a short delay
            // (that aliasing is what silently broke Inchindown's longest
            // FDN line; see phase3/inchindown/INCHINDOWN.md). In-range
            // delays are unaffected, so this is bit-exact for valid use.
            const uint32_t uId    = static_cast<uint32_t>(std::clamp(delayInSamples, 0, mask));
            const uint32_t uMask  = static_cast<uint32_t>(mask);
            return buffer[(uWrite - uId) & uMask];
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Thiran Allpass補間ディレイライン（フラット周波数特性）
    //   |H(ω)| = 1 を維持するため FDN フィードバックループ内の高域透過感が向上
    //   ※ モジュレーション時にIIR係数が急変しノイズが出るため、
    //     モジュレーション対象のディレイには LinearDelayLine (Hermite) を使用すること
    // ═══════════════════════════════════════════════════════════════════════════
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

        // Thiran 1次 allpass: y[n] = a*x[n] + x[n-1] - a*y[n-1]
        // a = (1-D)/(1+D), D = fractional delay
        inline float read(float delayInSamples) noexcept {
            int id = static_cast<int>(delayInSamples);
            float frac = delayInSamples - static_cast<float>(id);

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