#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    // 統合メモリプール (Single-Large Buffer)
    // ─────────────────────────────────────────────────────────────────────────────
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

        // 要求されたサイズを「次の2のべき乗」に切り上げてポインタを返す（高速なマスク演算のため）
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

    // ─────────────────────────────────────────────────────────────────────────────
    // 高速リニア補間ディレイライン
    // ─────────────────────────────────────────────────────────────────────────────
    class LinearDelayLine {
    public:
        void init(float* memory, int bitmask) {
            buffer = memory;
            mask = bitmask;
            writeIndex = 0;
        }

        // 線形補間（高域の自然なAir Absorptionを生む）
        inline float read(float delayInSamples) const noexcept {
            int id = static_cast<int>(delayInSamples);
            float frac = delayInSamples - static_cast<float>(id);

            // 負の数に対するビット演算の未定義動作を完全に防ぐため、uint32_tでラップアラウンドさせる
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

    // ─────────────────────────────────────────────────────────────────────────────
    // Thiran Allpass補間ディレイライン（フラット位相応答）
    //   線形補間は高域を減衰させる（sinc(πf)特性）が、Thiran allpassは
    //   |H(ω)|=1 を維持するため、FDNフィードバックループ内の高域透明感が向上。
    // ─────────────────────────────────────────────────────────────────────────────
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

            // frac→0 で a→1 (不安定) のため下限クランプ
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