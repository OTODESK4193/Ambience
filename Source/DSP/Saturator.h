#pragma once
#include <cmath>
#include <algorithm>

namespace FDNReverb {

    enum class SaturationMode {
        Warm = 0,
        Tape = 1,
        Tube = 2,
        Hard = 3
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  VintageSaturator (Vintage Warmth & Even Harmonics Engine)
    // ─────────────────────────────────────────────────────────────────────────────
    //  - Warm: 2次偶数倍音 + 3次ソフトクリップの黄金ブレンド (豊かな温もり)
    //  - Tape: 磁気テープ飽和 (高域ソフトコンプレッション + シルキー倍音)
    //  - Tube: 3極真空管 Triode 非対称性 (豊かな 2次倍音による太さと艶)
    //  - Hard: アナログトランスコア飽和 (クリッピング抑制)
    // ─────────────────────────────────────────────────────────────────────────────
    class Saturator {
    public:
        Saturator() = default;

        void reset() noexcept {
            dcState = 0.0f;
        }

        void setMode(SaturationMode mode) noexcept {
            currentMode = mode;
            reset();
        }

        void setMode(int modeIndex) noexcept {
            setMode(static_cast<SaturationMode>(std::clamp(modeIndex, 0, 3)));
        }

        void setAmount(float amount) noexcept {
            currentAmount = std::clamp(amount, 0.0f, 1.0f);
            const float effAmount = (currentMode == SaturationMode::Tube) ? (currentAmount * 0.30f) : currentAmount;
            drive = 1.0f + effAmount * 2.2f;
            dryMix = 1.0f - effAmount * 0.20f;
            wetMix = effAmount * 0.85f;
        }

        inline float processSample(float in) noexcept {
            if (currentAmount < 0.001f) return in;
            const float x = in * drive;
            float y = x;

            switch (currentMode) {
            case SaturationMode::Warm: { // 2次偶数倍音 + 3次ソフトクリップ
                const float t = std::tanh(x);
                const float h2 = 0.22f * x * x / (1.0f + std::abs(x));
                y = t + h2 * (1.0f - t * t);
                break;
            }
            case SaturationMode::Tape: { // 磁気テープ飽和
                const float s = std::clamp(x * 0.7f, -3.0f, 3.0f);
                const float tape = (s - (s * s * s) / 27.0f) * 1.25f;
                const float h2 = 0.08f * x * x / (1.0f + std::abs(x));
                y = tape + h2;
                break;
            }
            case SaturationMode::Tube: { // 真空管 3極管 Triode 非対称性
                const float x_pos = (x > 0.0f) ? x * 1.2f : x * 0.8f;
                const float tube = x_pos / (1.0f + 0.45f * std::abs(x_pos));
                const float h2 = 0.35f * x * x / (1.0f + 2.0f * std::abs(x));
                y = tube + h2;
                break;
            }
            case SaturationMode::Hard: { // トランスコア飽和 (単調増加・完全安全)
                const float x_clamped = std::clamp(x * 0.8f, -2.5f, 2.5f);
                y = 1.5f * x_clamped / (1.0f + std::abs(x_clamped));
                break;
            }
            }

            // 微小 DC ブロック (非対称歪みによる直流成分を除去)
            dcState += 0.002f * (y - dcState);
            y -= dcState;

            return in * dryMix + y * wetMix;
        }

    private:
        SaturationMode currentMode{ SaturationMode::Warm };
        float currentAmount{ 0.0f };
        float drive{ 1.0f };
        float dryMix{ 1.0f };
        float wetMix{ 0.0f };
        float dcState{ 0.0f };
    };

} // namespace FDNReverb