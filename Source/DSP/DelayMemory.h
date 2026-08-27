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
        // ─────────────────────────────────────────────────────────────────────
        //  なぜブロック間にパディングを入れるのか / Why the blocks are padded
        // ─────────────────────────────────────────────────────────────────────
        // Every block is rounded up to the next power of two so the wrap can be
        // a mask instead of a modulo. That part is a good idea. Packing the
        // results back-to-back is the part that hurts: it puts every buffer at
        // an exact power-of-two offset from every other one, so the 16 FDN
        // lines land 128 KB apart and the 48 serial-allpass lines 8 KB apart.
        //
        // Caches on this target are indexed on low address bits - a 32 KB
        // 4-way L1D on bits [11:6], and a physically-indexed 1 MB L2. Buffers
        // spaced an exact multiple of 8 KB apart therefore map onto the SAME
        // cache set at the same read offset, and 48 allpass reads end up
        // competing for four ways.
        //
        // Measured on the device (Cortex-A72, 48 kHz, perf): 132 L1D misses
        // per audio sample against the ~8.8 that pure streaming would compel -
        // a 15x conflict rate. See Ambience-profile.md.
        //
        // Rotating each block's start by a whole number of cache lines breaks
        // the stride and costs nothing else. The size stays a power of two and
        // the mask is relative to each buffer's OWN pointer, so every wrap
        // computation is unchanged and the DSP output is bit-identical.
        static constexpr size_t kLineFloats  = 16;   // 64-byte cache line
        static constexpr size_t kPadRotation = 16;   // distinct start colours
        static constexpr size_t kMaxAllocs   = 128;  // prepare() makes 70

        // Worst-case extra the rotation can consume. Kept as headroom inside
        // the pool so requestMemory() can never start returning nullptr on
        // account of the padding - prepare() does not null-check.
        static constexpr size_t padHeadroom() {
            return kMaxAllocs * (kPadRotation - 1) * kLineFloats;
        }

        void allocate(size_t totalSamples) {
            buffer.assign(totalSamples + padHeadroom(), 0.0f);
            allocOffset = 0;
            allocIndex = 0;
        }

        float* requestMemory(size_t samplesNeeded, int& outMask) {
            size_t powerOfTwoSize = 1;
            while (powerOfTwoSize < samplesNeeded) powerOfTwoSize *= 2;

            const size_t pad = (allocIndex % kPadRotation) * kLineFloats;
            ++allocIndex;

            if (allocOffset + pad + powerOfTwoSize > buffer.size()) return nullptr;

            allocOffset += pad;
            float* ptr = buffer.data() + allocOffset;
            outMask = static_cast<int>(powerOfTwoSize - 1);
            allocOffset += powerOfTwoSize;

            return ptr;
        }

        void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); }

    private:
        std::vector<float> buffer;
        size_t allocOffset{ 0 };
        size_t allocIndex{ 0 };
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
        void resetState() noexcept {}
        void init(float* memory, int bitmask) {
            buffer = memory;
            mask = bitmask;
            writeIndex = 0;
        }

        inline float read(float delayInSamples) const noexcept {
            int id = static_cast<int>(delayInSamples);
            float frac = delayInSamples - static_cast<float>(id);

            uint32_t uWrite = static_cast<uint32_t>(writeIndex);
            uint32_t uId    = static_cast<uint32_t>(id);
            uint32_t uMask  = static_cast<uint32_t>(mask);

            // 4点サンプル取得: y0(未来1) y1(現在) y2(過去1) y3(過去2)
            float y0 = buffer[static_cast<int>((uWrite - uId + 1) & uMask)];
            float y1 = buffer[static_cast<int>((uWrite - uId)     & uMask)];
            float y2 = buffer[static_cast<int>((uWrite - uId - 1) & uMask)];
            float y3 = buffer[static_cast<int>((uWrite - uId - 2) & uMask)];

            // Hermite 多項式係数
            float c0 = y1;
            float c1 = 0.5f * (y2 - y0);
            float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

            return ((c3 * frac + c2) * frac + c1) * frac + c0;
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