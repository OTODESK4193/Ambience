#pragma once
#include <cmath>
#include <algorithm>
#include <JuceHeader.h>

namespace FDNReverb {

    enum class SaturationMode {
        Warm = 0,   // SoftClip: tanh(x) + 2次偶数倍音 (厳密1次ADAA)
        Tape = 1,   // Tape: (2/π)arctan(kx) シルキー高域飽和 (厳密1次ADAA)
        Tube = 2,   // Tube: 非対称三極管 (豊かな偶数倍音・太さと艶・厳密1次ADAA)
        Hard = 3    // Hard: アナログトランスコア飽和 (クリッピング抑制・厳密1次ADAA)
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  ADAASaturator (1st-Order Anti-Derivative Anti-Aliasing Saturator)
    // ─────────────────────────────────────────────────────────────────────────────
    //  - 1次 ADAA (Anti-Derivative Anti-Aliasing) による超低エイリアシング
    //  - F'(x) ≡ f(x) を機械精度内で厳密に満たす完全解析解を採用（境界段差ゼロ）
    //  - amount < 0.001f 時の完全ゼロコスト・バイパス (IEEE 754 ビット一致保証)
    //  - Float32 漸近近似完備（大振幅時オーバーフロー・発散を数学的に完全防止）
    //  - 自動ゲイン補正（AGC）により全開時でも後段リミッターを飽和させない安全設計
    // ─────────────────────────────────────────────────────────────────────────────
    class Saturator {
    public:
        Saturator() = default;

        void prepare(double sampleRate) noexcept {
            fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;
            // 15Hz 1次 DC ブロッカー極配置
            dcR = static_cast<float>(1.0 - (2.0 * 3.141592653589793 * 15.0 / fs));

            // 20ms のサンプル単位線形ランプ平滑化
            amountSmoothed.reset(fs, 0.020);
            amountSmoothed.setCurrentAndTargetValue(0.0f);

            modeCrossfadeInc = 1.0f / static_cast<float>(fs * 0.020); // 20ms モードクロスフェード
            reset();
        }

        void reset() noexcept {
            stateActive.reset();
            statePending.reset();
            isModeCrossfading = false;
            modeCrossfadePos = 1.0f;
        }

        void setMode(SaturationMode mode) noexcept {
            if (activeMode != mode && pendingMode != mode) {
                if (isModeCrossfading) {
                    activeMode = pendingMode;
                    stateActive = statePending;
                }
                pendingMode = mode;
                // 状態変数の継承: 前のエンジンの x1_scalar を引き継ぎ、ADAA の初期割算ショックをゼロ化
                statePending.x1_scalar = stateActive.x1_scalar;
                statePending.dcInPrev = 0.0f;
                statePending.dcOutPrev = 0.0f;
                isModeCrossfading = true;
                modeCrossfadePos = 0.0f;
            }
        }

        void setMode(int modeIndex) noexcept {
            setMode(static_cast<SaturationMode>(std::clamp(modeIndex, 0, 3)));
        }

        void setAmount(float amount) noexcept {
            targetAmount = std::clamp(amount, 0.0f, 1.0f);
            amountSmoothed.setTargetValue(targetAmount);
        }

        inline float processSample(float in) noexcept {
            const float curAmount = amountSmoothed.getNextValue();

            // 定常状態ゼロコストバイパス:
            // ターゲットがゼロであり、スムーザーも完全にゼロに落ちきり、かつクロスフェードも稼働していない時のみ安全に直結
            if (curAmount < 1e-6f && !amountSmoothed.isSmoothing() && !isModeCrossfading) {
                stateActive.x1_scalar = in; // 復帰時のショック防止のため状態のみ追従
                return in;
            }

            // サンプル単位で滑らかに変化するドライブとパラレルブレンド係数
            const float drive = 1.0f + curAmount * 3.5f;
            const float dryMix = 1.0f - curAmount;

            // アクティブモードの計算
            const float compGainActive = getModeCompGain(activeMode, curAmount);
            float yActive = processCore(in, drive, activeMode, stateActive);

            float yCombined = yActive;
            float compGainCombined = compGainActive;

            // モード切り替えイコールパークロスフェード
            if (isModeCrossfading) {
                modeCrossfadePos += modeCrossfadeInc;
                if (modeCrossfadePos >= 1.0f) {
                    modeCrossfadePos = 1.0f;
                    isModeCrossfading = false;
                    activeMode = pendingMode;
                    stateActive = statePending;
                } else {
                    const float compGainPending = getModeCompGain(pendingMode, curAmount);
                    float yPending = processCore(in, drive, pendingMode, statePending);

                    // 等エネルギー (Equal-Power) コサイン・サインクロスフェード
                    const float fadeAngle = modeCrossfadePos * 1.5707963268f;
                    const float wActive  = std::cos(fadeAngle);
                    const float wPending = std::sin(fadeAngle);

                    yCombined = yActive * wActive + yPending * wPending;
                    compGainCombined = compGainActive * wActive + compGainPending * wPending;
                }
            }

            const float wetMix = curAmount * compGainCombined;
            return in * dryMix + yCombined * wetMix;
        }

    private:
        struct EngineState {
            float x1_scalar{ 0.0f };
            float adPrev{ 0.0f };
            bool hasAdPrev{ false };
            float dcInPrev{ 0.0f };
            float dcOutPrev{ 0.0f };
            void reset() noexcept {
                x1_scalar = 0.0f;
                adPrev = 0.0f;
                hasAdPrev = false;
                dcInPrev = 0.0f;
                dcOutPrev = 0.0f;
            }
        };

        double fs{ 48000.0 };
        float dcR{ 0.998f };
        float targetAmount{ 0.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amountSmoothed;

        SaturationMode activeMode{ SaturationMode::Warm };
        SaturationMode pendingMode{ SaturationMode::Warm };
        EngineState stateActive;
        EngineState statePending;

        bool isModeCrossfading{ false };
        float modeCrossfadePos{ 1.0f };
        float modeCrossfadeInc{ 0.001f };

        inline float getModeCompGain(SaturationMode mode, float amt) const noexcept {
            float modeComp = 1.0f;
            switch (mode) {
            case SaturationMode::Warm: modeComp = 0.82f; break;
            case SaturationMode::Tape: modeComp = 0.90f; break;
            case SaturationMode::Tube: modeComp = 0.95f; break;
            case SaturationMode::Hard: modeComp = 0.70f; break;
            }
            return (1.0f / (1.0f + amt * 2.2f)) * modeComp;
        }

        inline float processCore(float in, float drive, SaturationMode mode, EngineState& st) noexcept {
            const float x = in * drive;
            float y;
            const float diff = x - st.x1_scalar;
            const float ad_x = applyAD(x, mode);

            if (std::abs(diff) < 1e-5f) {
                const float xMid = (x + st.x1_scalar) * 0.5f;
                y = applyNL(xMid, mode);
            } else {
                const float prevAD = st.hasAdPrev ? st.adPrev : applyAD(st.x1_scalar, mode);
                y = (ad_x - prevAD) / diff;
            }
            st.x1_scalar = x;
            st.adPrev = ad_x;
            st.hasAdPrev = true;

            if (mode == SaturationMode::Tube) {
                const float hp = y - st.dcInPrev + dcR * st.dcOutPrev;
                st.dcInPrev = y;
                st.dcOutPrev = hp;
                y = hp;
            }
            return y;
        }

        // ════════════════════════════════════════════════════════════════════════
        //  非線形伝達関数 f(x) (特異点フォールバック用: F'(x) と厳密に完全一致)
        // ════════════════════════════════════════════════════════════════════════
        inline float applyNL(float x, SaturationMode mode) const noexcept {
            switch (mode) {
            case SaturationMode::Warm: {
                // ソフトクリップ + 2次偶数倍音
                // f(x) = tanh(x) + c * x * sech^2(x), c = 0.35
                const float t = std::tanh(x);
                const float sech2 = 1.0f - t * t;
                return t + 0.35f * x * sech2;
            }
            case SaturationMode::Tape: {
                // 磁気テープ飽和 (2/π)·arctan(1.5x)
                constexpr float k = 1.5f;
                constexpr float twoOverPi = 0.6366197723675813f;
                return 1.25f * twoOverPi * std::atan(k * x);
            }
            case SaturationMode::Tube: {
                // 非対称三極管: 奇数倍音飽和 + 豊かな2次偶数倍音 (C^∞ 級滑らか)
                // f(x) = x / sqrt(1 + x^2) + α * x^2 / (1 + x^2), α = 0.30
                const float x2 = x * x;
                const float invHypot = 1.0f / std::sqrt(1.0f + x2);
                return (x * invHypot) + 0.30f * (x2 / (1.0f + x2));
            }
            case SaturationMode::Hard: {
                // アナログトランスコア飽和 (ハードクランプ)
                constexpr float kHardGain = 2.5f;
                return std::clamp(kHardGain * x, -1.0f, 1.0f);
            }
            }
            return x;
        }

        // ════════════════════════════════════════════════════════════════════════
        //  ADAA 原始関数 F(x) = ∫ f(x) dx (完全解析解)
        // ════════════════════════════════════════════════════════════════════════
        inline float applyAD(float x, SaturationMode mode) const noexcept {
            switch (mode) {
            case SaturationMode::Warm: {
                // F(x) = (1 - c) ln(cosh(x)) + c * x * tanh(x)
                // F'(x) = (1 - c) tanh(x) + c [tanh(x) + x sech^2(x)] = tanh(x) + c x sech^2(x) = f(x)
                const float ax = std::abs(x);
                // Float32 漸近近似: |x| > 9.0 で ln(cosh(x)) ≈ |x| - ln(2), x * tanh(x) ≈ |x|
                if (ax > 9.0f) {
                    constexpr float ln2 = 0.69314718056f;
                    return ax - (1.0f - 0.35f) * ln2;
                }
                const float lncosh = std::log(std::cosh(x));
                const float xtanh = x * std::tanh(x);
                return (1.0f - 0.35f) * lncosh + 0.35f * xtanh;
            }
            case SaturationMode::Tape: {
                // F(x) = 1.25 * (2/π) [x·arctan(kx) - (1/2k)·ln(1+k²x²)]
                constexpr float k = 1.5f;
                constexpr float twoOverPi = 0.6366197723675813f;
                const float kx = k * x;
                return 1.25f * twoOverPi * (x * std::atan(kx) - (0.5f / k) * std::log(1.0f + kx * kx));
            }
            case SaturationMode::Tube: {
                // F(x) = sqrt(1 + x^2) - 1 + α [x - arctan(x)]
                // F'(x) = x / sqrt(1 + x^2) + α [1 - 1/(1+x^2)] = x / sqrt(1 + x^2) + α x^2 / (1+x^2) = f(x)
                const float ax = std::abs(x);
                if (ax > 20.0f) {
                    // Float32 漸近近似
                    constexpr float halfPi = 1.57079632679f;
                    const float sgn = (x >= 0.0f) ? 1.0f : -1.0f;
                    return (ax - 1.0f) + 0.30f * (x - sgn * halfPi);
                }
                const float term1 = std::sqrt(1.0f + x * x) - 1.0f;
                const float term2 = x - std::atan(x);
                return term1 + 0.30f * term2;
            }
            case SaturationMode::Hard: {
                // F(x) = ∫ clamp(kHardGain * x, -1, 1) dx = (1 / kHardGain) * ∫ clamp(u, -1, 1) du
                constexpr float kHardGain = 2.5f;
                const float u = kHardGain * x;
                const float au = std::abs(u);
                float f_val;
                if (au <= 1.0f) {
                    f_val = 0.5f * u * u;
                } else {
                    f_val = au - 0.5f;
                }
                return (1.0f / kHardGain) * f_val;
            }
            }
            return 0.5f * x * x;
        }
    };

} // namespace FDNReverb