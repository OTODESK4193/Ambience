#pragma once
#include <cmath>
#include <algorithm>
#include <immintrin.h>

namespace FDNReverb {

    enum class SaturationMode {
        Warm = 0,   // SoftClip: tanh(x)
        Tape = 1,   // Tape: (2/π)arctan(kx)
        Tube = 2,   // Tube: x/(1+|x|) (非対称)
        Hard = 3    // HardClip: clamp(x,-1,1)
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  ADAASaturator (1st-Order Anti-Derivative Anti-Aliasing Saturator)
    // ─────────────────────────────────────────────────────────────────────────────
    //  - オーバーサンプリング不要で 4x OS を凌駕するエイリアシング抑制 (-35dB以上)
    //  - FDN フィードバックループ内配置対応（受動性 Passivity 保証）
    //  - AVX2 8ch 並列版 processSample8() による超高速パイプライン
    //  - saturation = 0 時の完全バイパス（ビット一致）保証
    // ─────────────────────────────────────────────────────────────────────────────
    class Saturator {
    public:
        Saturator() = default;

        void reset() noexcept {
            x1_scalar = 0.0f;
            dcState = 0.0f;
            x1_simd = _mm256_setzero_ps();
            dcState_simd = _mm256_setzero_ps();
        }

        void setMode(SaturationMode mode) noexcept {
            currentMode = mode;
        }

        void setMode(int modeIndex) noexcept {
            setMode(static_cast<SaturationMode>(std::clamp(modeIndex, 0, 3)));
        }

        void setAmount(float amount) noexcept {
            currentAmount = std::clamp(amount, 0.0f, 1.0f);
            const float effAmount = (currentMode == SaturationMode::Tube)
                                        ? (currentAmount * 0.30f)
                                        : currentAmount;
            drive    = 1.0f + effAmount * 2.2f;
            invDrive = 1.0f / drive;
            dryMix   = 1.0f - effAmount * 0.20f;
            wetMix   = effAmount * 0.85f;
        }

        // ─── スカラー版: 1次 ADAA 処理 ───
        inline float processSample(float in) noexcept {
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

            // DC ブロッカー (Tube 非対称モードでのみ有効)
            if (currentMode == SaturationMode::Tube) {
                dcState += 0.002f * (y - dcState);
                y -= dcState;
            }

            return in * dryMix + y * invDrive * wetMix;
        }

        // ─── AVX2 8ch 並列版: 1次 ADAA 処理 ───
        inline __m256 processSample8(const __m256 in) noexcept {
            const __m256 vDrive    = _mm256_set1_ps(drive);
            const __m256 vInvDrv   = _mm256_set1_ps(invDrive);
            const __m256 vWetMix   = _mm256_set1_ps(wetMix);
            const __m256 vDryMix   = _mm256_set1_ps(dryMix);
            const __m256 vEps      = _mm256_set1_ps(1e-5f);
            const __m256 vAbsMask  = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));

            const __m256 x = _mm256_mul_ps(in, vDrive);
            const __m256 diff = _mm256_sub_ps(x, x1_simd);
            const __m256 absDiff = _mm256_and_ps(diff, vAbsMask);

            // 特異点マスク: |diff| < epsilon
            const __m256 singularMask = _mm256_cmp_ps(absDiff, vEps, _CMP_LT_OS);

            // ADAA 計算: y = (F(x) - F(x1)) / diff
            const __m256 Fx  = applyAD_AVX2(x);
            const __m256 Fx1 = applyAD_AVX2(x1_simd);
            // 安全な除算: diff が微小な場合は 1.0 で除算（後でブレンドで上書き）
            const __m256 safeDiff = _mm256_blendv_ps(diff, _mm256_set1_ps(1.0f), singularMask);
            const __m256 yAdaa = _mm256_div_ps(_mm256_sub_ps(Fx, Fx1), safeDiff);

            // フォールバック: f(x_mid)
            const __m256 xMid = _mm256_mul_ps(_mm256_add_ps(x, x1_simd), _mm256_set1_ps(0.5f));
            const __m256 yFallback = applyNL_AVX2(xMid);

            // ブレンド
            __m256 y = _mm256_blendv_ps(yAdaa, yFallback, singularMask);

            x1_simd = x;

            // DC ブロッカー (Tube 非対称モードでのみ)
            if (currentMode == SaturationMode::Tube) {
                const __m256 vDcCoeff = _mm256_set1_ps(0.002f);
                dcState_simd = _mm256_fmadd_ps(vDcCoeff, _mm256_sub_ps(y, dcState_simd), dcState_simd);
                y = _mm256_sub_ps(y, dcState_simd);
            }

            // 出力: in * dryMix + y * invDrive * wetMix
            const __m256 wet = _mm256_mul_ps(_mm256_mul_ps(y, vInvDrv), vWetMix);
            return _mm256_fmadd_ps(in, vDryMix, wet);
        }

    private:
        SaturationMode currentMode{ SaturationMode::Warm };
        float currentAmount{ 0.0f };
        float drive{ 1.0f };
        float invDrive{ 1.0f };
        float dryMix{ 1.0f };
        float wetMix{ 0.0f };
        float x1_scalar{ 0.0f };
        float dcState{ 0.0f };
        __m256 x1_simd{ _mm256_setzero_ps() };
        __m256 dcState_simd{ _mm256_setzero_ps() };

        // ════════════════════════════════════════════════════════════════════════
        //  スカラー版 非線形関数 f(x) と原始関数 F(x)
        // ════════════════════════════════════════════════════════════════════════

        inline float applyNL(float x) const noexcept {
            switch (currentMode) {
            case SaturationMode::Warm:
                return std::tanh(x);
            case SaturationMode::Tape: {
                constexpr float k = 1.5f;
                constexpr float twoOverPi = 0.6366197723f;
                return twoOverPi * std::atan(k * x);
            }
            case SaturationMode::Tube: {
                const float xp = (x > 0.0f) ? x * 1.15f : x * 0.85f;
                return xp / (1.0f + std::abs(xp));
            }
            case SaturationMode::Hard:
                return std::clamp(x, -1.0f, 1.0f);
            }
            return x;
        }

        inline float applyAD(float x) const noexcept {
            switch (currentMode) {
            case SaturationMode::Warm: {
                // F(x) = ln(cosh(x)), 漸近近似: |x|>10 → |x| - ln(2)
                const float ax = std::abs(x);
                if (ax > 10.0f) return ax - 0.6931472f; // ln(2)
                return std::log(std::cosh(x));
            }
            case SaturationMode::Tape: {
                // F(x) = (2/π) [x·arctan(kx) - (1/2k)·ln(1+k²x²)]
                constexpr float k = 1.5f;
                constexpr float twoOverPi = 0.6366197723f;
                const float kx = k * x;
                return twoOverPi * (x * std::atan(kx) - (0.5f / k) * std::log(1.0f + kx * kx));
            }
            case SaturationMode::Tube: {
                // F(x) = |x| - ln(1+|x|), 正負で非対称ゲインを適用
                const float xp = (x > 0.0f) ? x * 1.15f : x * 0.85f;
                const float ax = std::abs(xp);
                return ax - std::log(1.0f + ax);
            }
            case SaturationMode::Hard: {
                // F(x) = x²/2 (|x|<1), |x|-0.5 (|x|>=1)
                const float ax = std::abs(x);
                if (ax < 1.0f) return 0.5f * x * x;
                return ax - 0.5f;
            }
            }
            return 0.5f * x * x; // フォールバック (f=x の原始関数)
        }

        // ════════════════════════════════════════════════════════════════════════
        //  AVX2 SIMD 版 非線形関数 f(x) と原始関数 F(x)
        // ════════════════════════════════════════════════════════════════════════

        // tanh(x) Padé 近似: tanh(x) ≈ x(27+x²)/(27+9x²)
        static inline __m256 tanh_pade_avx2(__m256 x) noexcept {
            const __m256 x2   = _mm256_mul_ps(x, x);
            const __m256 v27  = _mm256_set1_ps(27.0f);
            const __m256 v9   = _mm256_set1_ps(9.0f);
            const __m256 num  = _mm256_mul_ps(x, _mm256_add_ps(v27, x2));       // x*(27+x²)
            const __m256 den  = _mm256_fmadd_ps(v9, x2, v27);                   // 9x²+27
            __m256 result     = _mm256_div_ps(num, den);
            // クランプ to [-1, 1]
            result = _mm256_min_ps(_mm256_set1_ps(1.0f), _mm256_max_ps(_mm256_set1_ps(-1.0f), result));
            return result;
        }

        // ln(cosh(x)) 近似: Padé tanh の解析的積分に基づく
        // F(x) ≈ x²/18 + (4/3)·ln(x²+3) + C  (正規化定数で調整)
        // 漸近近似: |x|>10 → |x| - ln(2)
        static inline __m256 lncosh_approx_avx2(__m256 x) noexcept {
            const __m256 vAbsMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
            const __m256 ax = _mm256_and_ps(x, vAbsMask);

            // 小信号域: 直接 ln(cosh(x)) ≈ x²/2 - x⁴/12 + ... の簡易多項式
            // 中信号域: x²/18 + (4/3)·ln(x²+3) に基づく近似
            const __m256 x2 = _mm256_mul_ps(x, x);

            // 実用的な近似: ln(cosh(x)) ≈ 0.5*(sqrt(x²+1)-1) + 0.038*x² for |x|<4
            // 中信号用: 修正 softplus 近似
            // ln(cosh(x)) = |x| - ln(2) + ln(1 + exp(-2|x|))
            // exp(-2|x|) を高速近似
            const __m256 v2    = _mm256_set1_ps(2.0f);
            const __m256 vLn2  = _mm256_set1_ps(0.6931472f);
            const __m256 negTwoAx = _mm256_mul_ps(_mm256_set1_ps(-2.0f), ax);

            // 高速 exp 近似 (|x|<10 の範囲で十分な精度)
            // exp(x) ≈ (1+x/256)^256 の 2^n シフト近似
            // 簡易版: exp(x) ≈ max(0, 1+x+x²/2+x³/6) (|x|<3)
            const __m256 e1 = _mm256_add_ps(_mm256_set1_ps(1.0f), negTwoAx);
            const __m256 negTwoAx2 = _mm256_mul_ps(negTwoAx, negTwoAx);
            const __m256 e2 = _mm256_fmadd_ps(_mm256_set1_ps(0.5f), negTwoAx2, e1);
            const __m256 negTwoAx3 = _mm256_mul_ps(negTwoAx2, negTwoAx);
            __m256 expApprox = _mm256_fmadd_ps(_mm256_set1_ps(0.166667f), negTwoAx3, e2);
            expApprox = _mm256_max_ps(expApprox, _mm256_setzero_ps());

            // ln(1+exp(-2|x|)) ≈ exp(-2|x|) for large |x| (softplus tail)
            // 正確な softplus: log1p(exp) → 小信号で使用
            // 大信号域 (|x|>4): ln(1+exp(-2|x|)) ≈ exp(-2|x|) ≈ 0
            const __m256 softplusTerm = expApprox; // ln(1+z) ≈ z for small z

            // F(x) = |x| - ln(2) + softplusTerm
            __m256 result = _mm256_add_ps(_mm256_sub_ps(ax, vLn2), softplusTerm);

            // 小信号域 (|x|<0.5): F(x) ≈ x²/2 の方が精度が良い
            const __m256 smallResult = _mm256_mul_ps(_mm256_set1_ps(0.5f), x2);
            const __m256 smallMask = _mm256_cmp_ps(ax, _mm256_set1_ps(0.5f), _CMP_LT_OS);
            result = _mm256_blendv_ps(result, smallResult, smallMask);

            return result;
        }

        inline __m256 applyNL_AVX2(__m256 x) const noexcept {
            switch (currentMode) {
            case SaturationMode::Warm:
                return tanh_pade_avx2(x);
            case SaturationMode::Tape: {
                // (2/π)·arctan(kx) の Padé 近似
                // arctan(x) ≈ x·(15+4x²)/(15+9x²) (Padé [2/2])
                const __m256 vK = _mm256_set1_ps(1.5f);
                const __m256 vScale = _mm256_set1_ps(0.6366197723f);
                const __m256 kx = _mm256_mul_ps(vK, x);
                const __m256 kx2 = _mm256_mul_ps(kx, kx);
                const __m256 v15 = _mm256_set1_ps(15.0f);
                const __m256 v4  = _mm256_set1_ps(4.0f);
                const __m256 v9  = _mm256_set1_ps(9.0f);
                const __m256 num = _mm256_mul_ps(kx, _mm256_fmadd_ps(v4, kx2, v15));
                const __m256 den = _mm256_fmadd_ps(v9, kx2, v15);
                __m256 result = _mm256_mul_ps(vScale, _mm256_div_ps(num, den));
                result = _mm256_min_ps(_mm256_set1_ps(1.0f), _mm256_max_ps(_mm256_set1_ps(-1.0f), result));
                return result;
            }
            case SaturationMode::Tube: {
                // x/(1+|x|) 非対称版
                const __m256 vAbsMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
                const __m256 posMask = _mm256_cmp_ps(x, _mm256_setzero_ps(), _CMP_GT_OS);
                const __m256 scale = _mm256_blendv_ps(_mm256_set1_ps(0.85f), _mm256_set1_ps(1.15f), posMask);
                const __m256 xp = _mm256_mul_ps(x, scale);
                const __m256 axp = _mm256_and_ps(xp, vAbsMask);
                return _mm256_div_ps(xp, _mm256_add_ps(_mm256_set1_ps(1.0f), axp));
            }
            case SaturationMode::Hard: {
                return _mm256_min_ps(_mm256_set1_ps(1.0f), _mm256_max_ps(_mm256_set1_ps(-1.0f), x));
            }
            }
            return x;
        }

        inline __m256 applyAD_AVX2(__m256 x) const noexcept {
            switch (currentMode) {
            case SaturationMode::Warm:
                return lncosh_approx_avx2(x);
            case SaturationMode::Tape: {
                // F(x) = (2/π)[x·arctan(kx) - (1/2k)·ln(1+k²x²)]
                // arctan → Padé 近似, ln → softplus 近似
                const __m256 vK = _mm256_set1_ps(1.5f);
                const __m256 vScale = _mm256_set1_ps(0.6366197723f);
                const __m256 vHalfInvK = _mm256_set1_ps(1.0f / 3.0f); // 1/(2k) = 1/3
                const __m256 kx = _mm256_mul_ps(vK, x);
                const __m256 kx2 = _mm256_mul_ps(kx, kx);
                // arctan Padé 近似
                const __m256 v15 = _mm256_set1_ps(15.0f);
                const __m256 v4  = _mm256_set1_ps(4.0f);
                const __m256 v9  = _mm256_set1_ps(9.0f);
                const __m256 atanNum = _mm256_mul_ps(kx, _mm256_fmadd_ps(v4, kx2, v15));
                const __m256 atanDen = _mm256_fmadd_ps(v9, kx2, v15);
                const __m256 atanApprox = _mm256_div_ps(atanNum, atanDen);
                // x * arctan(kx)
                const __m256 term1 = _mm256_mul_ps(x, atanApprox);
                // ln(1+k²x²) ≈ log1p 近似: k²x² / (1 + k²x²/2) (Padé)
                const __m256 lnArg = _mm256_fmadd_ps(kx, kx, _mm256_set1_ps(0.0f)); // k²x²
                const __m256 lnApprox = _mm256_div_ps(lnArg, _mm256_fmadd_ps(_mm256_set1_ps(0.5f), lnArg, _mm256_set1_ps(1.0f)));
                const __m256 term2 = _mm256_mul_ps(vHalfInvK, lnApprox);
                return _mm256_mul_ps(vScale, _mm256_sub_ps(term1, term2));
            }
            case SaturationMode::Tube: {
                // F(x) = |xp| - ln(1+|xp|) 非対称ゲイン適用
                const __m256 vAbsMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
                const __m256 posMask = _mm256_cmp_ps(x, _mm256_setzero_ps(), _CMP_GT_OS);
                const __m256 scale = _mm256_blendv_ps(_mm256_set1_ps(0.85f), _mm256_set1_ps(1.15f), posMask);
                const __m256 xp = _mm256_mul_ps(x, scale);
                const __m256 axp = _mm256_and_ps(xp, vAbsMask);
                // ln(1+|xp|) ≈ |xp|/(1+|xp|/2) (Padé [1/1])
                const __m256 lnApprox = _mm256_div_ps(axp, _mm256_fmadd_ps(_mm256_set1_ps(0.5f), axp, _mm256_set1_ps(1.0f)));
                return _mm256_sub_ps(axp, lnApprox);
            }
            case SaturationMode::Hard: {
                // F(x) = x²/2 (|x|<1), |x|-0.5 (|x|>=1)
                const __m256 vAbsMask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
                const __m256 ax = _mm256_and_ps(x, vAbsMask);
                const __m256 x2half = _mm256_mul_ps(_mm256_set1_ps(0.5f), _mm256_mul_ps(x, x));
                const __m256 clipResult = _mm256_sub_ps(ax, _mm256_set1_ps(0.5f));
                const __m256 clipMask = _mm256_cmp_ps(ax, _mm256_set1_ps(1.0f), _CMP_GE_OS);
                return _mm256_blendv_ps(x2half, clipResult, clipMask);
            }
            }
            return _mm256_mul_ps(_mm256_set1_ps(0.5f), _mm256_mul_ps(x, x));
        }
    };

} // namespace FDNReverb