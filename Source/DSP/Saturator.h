#pragma once
#include <cmath>
#include <algorithm>

namespace FDNReverb {

    enum class SaturationMode {
        Warm = 0,   // SoftClip: tanh(x) + 2次偶数倍音
        Tape = 1,   // Tape: (2/π)arctan(kx) シルキー高域飽和
        Tube = 2,   // Tube: 非対称三極管 (豊かな偶数倍音・太さと艶)
        Hard = 3    // Hard: アナログトランスコア飽和 (クリッピング抑制)
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  ADAASaturator (1st-Order Anti-Derivative Anti-Aliasing Saturator)
    // ─────────────────────────────────────────────────────────────────────────────
    //  - 1次 ADAA (Anti-Derivative Anti-Aliasing) による超低エイリアシング
    //  - V1.3 の図太く明確な 4 モードキャラクター（倍音構造）を完全再現
    //  - amount < 0.001f 時の完全ゼロコスト・バイパス (IEEE 754 ビット一致保証)
    //  - 特異点 (ゼロ交差) 安全フォールバック & Float32 漸近近似完備
    // ─────────────────────────────────────────────────────────────────────────────
    class Saturator {
    public:
        Saturator() = default;

        void prepare(double sampleRate) noexcept {
            const double sr = (sampleRate > 1000.0) ? sampleRate : 48000.0;
            // 1次 DC ブロッカー係数 (10Hz カットオフ)
            dcCoeff = static_cast<float>(1.0 - std::exp(-2.0 * 3.141592653589793 * 10.0 / sr));
            reset();
        }

        void reset() noexcept {
            x1_scalar = 0.0f;
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
            const float effAmount = (currentMode == SaturationMode::Tube)
                                        ? (currentAmount * 0.35f)
                                        : currentAmount;
            // V1.3 の音楽的ダイナミクスカーブ (しっかり歪みが乗る設計)
            drive   = 1.0f + effAmount * 2.5f; // 1.0 〜 3.5倍 (+11dB)
            dryMix  = 1.0f - effAmount * 0.25f; // 1.0 〜 0.75
            wetMix  = effAmount * 0.90f;        // 0.0 〜 0.90
        }

        // ─── 1次 ADAA サチュレーション処理 ───
        inline float processSample(float in) noexcept {
            // ゼロコスト完全バイパス (IEEE 754 1ビットも改変しない)
            if (currentAmount < 0.001f) return in;

            const float x = in * drive;
            float y;

            const float diff = x - x1_scalar;
            if (std::abs(diff) < 1e-5f) {
                // 特異点回避: 中点での関数値 (ロピタルの定理フォールバック)
                const float xMid = (x + x1_scalar) * 0.5f;
                y = applyNL(xMid);
            } else {
                // 1次 ADAA: y = (F(x) - F(x1)) / (x - x1)
                y = (applyAD(x) - applyAD(x1_scalar)) / diff;
            }
            x1_scalar = x;

            // DC ブロッカー (Tube 非対称モードでのみ直流を除去)
            if (currentMode == SaturationMode::Tube) {
                dcState += dcCoeff * (y - dcState);
                y -= dcState;
            }

            // V1.3 準拠のパラレルブレンド
            return in * dryMix + y * wetMix;
        }

    private:
        SaturationMode currentMode{ SaturationMode::Warm };
        float currentAmount{ 0.0f };
        float drive{ 1.0f };
        float dryMix{ 1.0f };
        float wetMix{ 0.0f };
        float x1_scalar{ 0.0f };
        float dcState{ 0.0f };
        float dcCoeff{ 0.002f };

        // ════════════════════════════════════════════════════════════════════════
        //  非線形伝達関数 f(x) (特異点フォールバック用)
        // ════════════════════════════════════════════════════════════════════════
        inline float applyNL(float x) const noexcept {
            switch (currentMode) {
            case SaturationMode::Warm: {
                // 2次偶数倍音 + 3次ソフトクリップ
                const float t = std::tanh(x);
                const float h2 = 0.20f * x * x / (1.0f + std::abs(x));
                return t + h2 * (1.0f - t * t);
            }
            case SaturationMode::Tape: {
                // 磁気テープ飽和 (2/π)·arctan(1.5x)
                constexpr float k = 1.5f;
                constexpr float twoOverPi = 0.6366197723f;
                return twoOverPi * std::atan(k * x) * 1.25f;
            }
            case SaturationMode::Tube: {
                // 真空管 3極管 Triode 非対称性 (正相ブースト・偶数倍音)
                const float xp = (x > 0.0f) ? x * 1.25f : x * 0.75f;
                const float tube = xp / (1.0f + std::abs(xp));
                const float h2 = 0.25f * x * x / (1.0f + 1.5f * std::abs(x));
                return tube + h2;
            }
            case SaturationMode::Hard: {
                // アナログトランスコア飽和 (ハードクランプ)
                return std::clamp(x * 1.2f, -1.0f, 1.0f);
            }
            }
            return x;
        }

        // ════════════════════════════════════════════════════════════════════════
        //  ADAA 原始関数 F(x) = ∫ f(x) dx
        // ════════════════════════════════════════════════════════════════════════
        inline float applyAD(float x) const noexcept {
            switch (currentMode) {
            case SaturationMode::Warm: {
                // F_tanh(x) = ln(cosh(x)) + 偶数倍音成分の積分
                const float ax = std::abs(x);
                // Float32 漸近近似: |x| > 10.0 で ln(cosh(x)) ≈ |x| - ln(2)
                const float f_tanh = (ax > 10.0f) ? (ax - 0.6931472f) : std::log(std::cosh(x));
                // 偶数倍音成分の近似積分: ∫ 0.20 x²/(1+|x|) dx ≈ 0.10 sgn(x) x² / (1+0.5|x|)
                const float sgn = (x >= 0.0f) ? 1.0f : -1.0f;
                const float f_even = 0.10f * sgn * (x * x) / (1.0f + 0.5f * ax);
                return f_tanh + f_even;
            }
            case SaturationMode::Tape: {
                // F(x) = 1.25 * (2/π) [x·arctan(kx) - (1/2k)·ln(1+k²x²)]
                constexpr float k = 1.5f;
                constexpr float twoOverPi = 0.6366197723f;
                const float kx = k * x;
                return 1.25f * twoOverPi * (x * std::atan(kx) - (0.5f / k) * std::log(1.0f + kx * kx));
            }
            case SaturationMode::Tube: {
                // 非対称三極管の積分: 正相・逆相で異なるスケール
                const float s = (x > 0.0f) ? 1.25f : 0.75f;
                const float sx = s * x;
                const float asx = std::abs(sx);
                // ∫ (sx / (1+|sx|)) dx = (1/s) * [|sx| - ln(1+|sx|)] * sgn
                const float sgn = (x >= 0.0f) ? 1.0f : -1.0f;
                const float f_main = (1.0f / s) * sgn * (asx - std::log(1.0f + asx));
                // 偶数倍音項の積分
                const float ax = std::abs(x);
                const float f_even = 0.12f * sgn * (x * x) / (1.0f + 0.75f * ax);
                return f_main + f_even;
            }
            case SaturationMode::Hard: {
                // F(x) = ∫ clamp(1.2x, -1, 1) dx
                const float u = 1.2f * x;
                const float au = std::abs(u);
                float f_val;
                if (au < 1.0f) {
                    f_val = 0.5f * u * u;
                } else {
                    f_val = au - 0.5f;
                }
                return (1.0f / 1.2f) * f_val;
            }
            }
            return 0.5f * x * x;
        }
    };

} // namespace FDNReverb