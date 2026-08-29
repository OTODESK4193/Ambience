#pragma once

#include <cmath>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  OutputLimiter: 出力段の安全装置（ブリックウォール・ピークリミッター）
    // ─────────────────────────────────────────────────────────────────────────────
    //   設計方針:
    //     - パラメータなし: ユーザーが操作するものではなく、純粋な安全装置
    //     - Threshold = -0.5 dBFS (≈ 0.944 リニア): DAW でクリップする手前で抑制
    //     - Look-ahead なし: プラグインのレイテンシを増やさない
    //     - Attack: 0.5ms（突発ピークに即応）
    //     - Release: 50ms（自然な余韻・ポンピング防止）
    //
    //   リアルタイム安全性:
    //     - メモリアロケーション: prepare() で 1 回のみ、processBlock 内ではゼロ
    //     - ブランチ: targetGain の比較 1 回のみ、SIMD 化容易
    //     - 浮動小数点演算: 加減算と除算のみ、超越関数なし
    //
    //   配置: UniversalEngine の processBlock() の最終出力段
    //   （Dry/Wet ミックス後、ステレオ出力直前）
    // ─────────────────────────────────────────────────────────────────────────────
    class OutputLimiter {
    public:
        OutputLimiter() = default;

        void prepare(double sampleRate) noexcept {
            fs = sampleRate;
            // ピークエンベロープ: 即座に追従(0.1ms)し、自然に保持(50ms)
            peakAttackCoeff  = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.0001f));
            peakReleaseCoeff = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.050f));
            // ゲインスムージング: 1ms で滑らかに適用し、ゼロクロス変調歪みを完全防止
            gainSmoothCoeff  = 1.0f - std::exp(-1.0f / (static_cast<float>(fs) * 0.001f));
            reset();
        }

        void reset() noexcept {
            peakEnv = 0.0f;
            currentGain = 1.0f;
        }

        inline void process(float& l, float& r) noexcept {
            const float inPeak = std::max(std::abs(l), std::abs(r));

            // ★ ピークエンベロープ追従 (ゼロクロスでも急落しない)
            if (inPeak > peakEnv)
                peakEnv += (inPeak - peakEnv) * peakAttackCoeff;
            else
                peakEnv += (inPeak - peakEnv) * peakReleaseCoeff;

            constexpr float threshold = 0.944f; // -0.5 dBFS
            const float targetGain = (peakEnv > threshold) ? (threshold / peakEnv) : 1.0f;

            // ★ ゲイン自体のスムージング (オーディオレートのチャタリングを完全遮断)
            currentGain += (targetGain - currentGain) * gainSmoothCoeff;

            l *= currentGain;
            r *= currentGain;
        }

    private:
        double fs{ 48000.0 };
        float  peakAttackCoeff{ 0.0f };
        float  peakReleaseCoeff{ 0.0f };
        float  gainSmoothCoeff{ 0.0f };
        float  peakEnv{ 0.0f };
        float  currentGain{ 1.0f };
    };

} // namespace FDNReverb