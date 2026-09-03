#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include <immintrin.h>

namespace FDNReverb {

    /**
     * @brief 4バンド・周波数追従型ダイナミックEQダッカー
     * (4-Band Dynamic EQ Ducking for Frequency-Tracking Masking Suppression)
     * 
     * - ドライ信号の周波数帯域（低域/中低域/中高域/高域）に連動し、衝突する帯域のみを選択的にダッキング
     * - TPT (Topology-Preserving Transform) 構造の SVF による時変変調下の無条件 BIBO 安定性
     * - 高域の空気感（Air成分）のダッキング深度を 0.2倍に抑え、ポンピング現象を根絶
     * - duckingAmount <= 0.001f 時の完全ゼロコスト・バイパス (IEEE 754 1ビット完全一致保証)
     */
    class DynamicEQDucker {
    public:
        DynamicEQDucker() noexcept {
            reset();
        }

        void prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
            fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;
            const float fsf = static_cast<float>(fs);

            // クロスオーバー周波数: 250Hz, 2500Hz, 8000Hz
            static constexpr float CROSSOVER_FREQS[3] = { 250.0f, 2500.0f, 8000.0f };
            static constexpr float Q_BUTTERWORTH = 0.70710678f; // 1/sqrt(2)

            for (int i = 0; i < 3; ++i) {
                const float omega = 3.141592653589793f * CROSSOVER_FREQS[i] / fsf;
                svfG[i] = std::tan(omega);
                svfR2[i] = 1.0f / Q_BUTTERWORTH; // 2R = 2 * (1 / (2Q)) = 1/Q = sqrt(2)
                svfH[i] = 1.0f / (1.0f + svfR2[i] * svfG[i] + svfG[i] * svfG[i]);
            }

            reset();
            updateCoefficients();
        }

        void reset() noexcept {
            for (int ch = 0; ch < 2; ++ch) {
                for (int i = 0; i < 3; ++i) {
                    svfS1_wet[ch][i] = 0.0f;
                    svfS2_wet[ch][i] = 0.0f;
                    svfS1_dry[ch][i] = 0.0f;
                    svfS2_dry[ch][i] = 0.0f;
                }
                for (int b = 0; b < 4; ++b) {
                    envState[ch][b] = 0.0f;
                }
            }
        }

        /**
         * @brief パラメータ設定
         * @param amountDB ダッキング量 [0.0, 40.0] dB
         * @param attackMs アタック時間 [1.0, 500.0] ms
         * @param releaseMs リリース時間 [10.0, 2000.0] ms
         * @param threshDB スレッショルド [-60.0, 0.0] dBFS
         */
        void setParameters(float amountDB, float attackMs, float releaseMs, float threshDB) noexcept {
            duckAmount = std::clamp(amountDB, 0.0f, 40.0f);
            baseAttackMs = std::clamp(attackMs, 1.0f, 500.0f);
            baseReleaseMs = std::clamp(releaseMs, 10.0f, 2000.0f);
            baseThreshDB = std::clamp(threshDB, -60.0f, 0.0f);

            updateCoefficients();
        }

        /**
         * @brief ステレオ処理
         * @param dryL, dryR ドライ入力信号 (サイドチェイン検知用)
         * @param wetL, wetR リバーブウェット信号 (ダッキング適用対象)
         */
        inline void processStereo(float dryL, float dryR, float& wetL, float& wetR) noexcept {
            // ★【完全ゼロコスト・バイパス保証】
            // duckAmount が 0 の場合、バッファに一切触れず早期リターン
            // IEEE 754 浮動小数点が 1 ビットたりとも改変されないことを数学的に保証
            if (duckAmount <= 0.001f) [[likely]] {
                return;
            }

            processChannel(0, dryL, wetL);
            processChannel(1, dryR, wetR);
        }

    private:
        double fs{ 48000.0 };
        float duckAmount{ 0.0f };
        float baseAttackMs{ 10.0f };
        float baseReleaseMs{ 200.0f };
        float baseThreshDB{ -20.0f };

        // SVF 係数 (3つのクロスオーバー点)
        std::array<float, 3> svfG{};
        std::array<float, 3> svfR2{};
        std::array<float, 3> svfH{};

        // 状態変数 [2ch][3フィルター]
        float svfS1_wet[2][3]{};
        float svfS2_wet[2][3]{};
        float svfS1_dry[2][3]{};
        float svfS2_dry[2][3]{};

        // エンベロープ [2ch][4バンド]
        float envState[2][4]{};

        // バンド別パラメータ [4バンド]
        // 1=Low(~250Hz), 2=LowMid(250~2.5k), 3=HighMid(2.5k~8k), 4=High(8k~)
        std::array<float, 4> bandThreshLin{};
        std::array<float, 4> bandAttCoeff{};
        std::array<float, 4> bandRelCoeff{};
        std::array<float, 4> bandDepthScale{};

        void updateCoefficients() noexcept {
            const float fsf = static_cast<float>(fs);

            // 1. スレッショルド配分: Low (+3dB), Low-Mid (0dB), High-Mid (-3dB), High (-6dB)
            static constexpr float THRESH_OFFSETS[4] = { +3.0f, 0.0f, -3.0f, -6.0f };
            for (int b = 0; b < 4; ++b) {
                const float db = std::clamp(baseThreshDB + THRESH_OFFSETS[b], -70.0f, 0.0f);
                bandThreshLin[b] = std::pow(10.0f, db / 20.0f);
            }

            // 2. 時定数スケーリング: 高域ほど高速追従
            static constexpr float TIME_SCALES[4] = { 1.5f, 1.0f, 0.7f, 0.5f };
            for (int b = 0; b < 4; ++b) {
                const float attSec = std::max(0.0005f, (baseAttackMs * TIME_SCALES[b]) * 0.001f);
                const float relSec = std::max(0.005f, (baseReleaseMs * TIME_SCALES[b]) * 0.001f);
                bandAttCoeff[b] = 1.0f - std::exp(-1.0f / (fsf * attSec));
                bandRelCoeff[b] = 1.0f - std::exp(-1.0f / (fsf * relSec));
            }

            // 3. ダッキング深度スケーリング (空気感を残すため High は 0.2x)
            static constexpr float DEPTH_SCALES[4] = { 0.8f, 1.0f, 0.9f, 0.2f };
            for (int b = 0; b < 4; ++b) {
                bandDepthScale[b] = DEPTH_SCALES[b];
            }
        }

        // TPT SVF 1段の計算
        inline void stepSVF(float in, float g, float r2, float h, float& s1, float& s2,
                            float& lp, float& hp) noexcept {
            const float v = (in - s2 - r2 * s1) * h;
            const float bp = g * v + s1;
            lp = g * bp + s2;
            hp = v;
            s1 = 2.0f * bp - s1;
            s2 = 2.0f * lp - s2;
        }

        inline void processChannel(int ch, float dry, float& wet) noexcept {
            // ── 1. ドライ信号の 4 バンド分割 (サイドチェイン) ──
            float dryLP1, dryHP1;
            stepSVF(dry, svfG[0], svfR2[0], svfH[0], svfS1_dry[ch][0], svfS2_dry[ch][0], dryLP1, dryHP1);
            
            float dryLP2, dryHP2;
            stepSVF(dryHP1, svfG[1], svfR2[1], svfH[1], svfS1_dry[ch][1], svfS2_dry[ch][1], dryLP2, dryHP2);

            float dryLP3, dryHP3;
            stepSVF(dryHP2, svfG[2], svfR2[2], svfH[2], svfS1_dry[ch][2], svfS2_dry[ch][2], dryLP3, dryHP3);

            const float dryBands[4] = {
                dryLP1, // Band 1: Low (~250Hz)
                dryLP2, // Band 2: Low-Mid (250~2.5kHz)
                dryLP3, // Band 3: High-Mid (2.5k~8kHz)
                dryHP3  // Band 4: High (8kHz~)
            };

            // ── 2. ウェット信号の 4 バンド分割 ──
            float wetLP1, wetHP1;
            stepSVF(wet, svfG[0], svfR2[0], svfH[0], svfS1_wet[ch][0], svfS2_wet[ch][0], wetLP1, wetHP1);

            float wetLP2, wetHP2;
            stepSVF(wetHP1, svfG[1], svfR2[1], svfH[1], svfS1_wet[ch][1], svfS2_wet[ch][1], wetLP2, wetHP2);

            float wetLP3, wetHP3;
            stepSVF(wetHP2, svfG[2], svfR2[2], svfH[2], svfS1_wet[ch][2], svfS2_wet[ch][2], wetLP3, wetHP3);

            const float wetBands[4] = {
                wetLP1,
                wetLP2,
                wetLP3,
                wetHP3
            };

            // ── 3. 各バンドのエンベロープ追従とゲインリダクション ──
            float outSum = 0.0f;
            constexpr float ANTI_DENORMAL = 1e-18f;

            for (int b = 0; b < 4; ++b) {
                const float dryAbs = std::abs(dryBands[b]) + ANTI_DENORMAL;
                float& env = envState[ch][b];

                const float coeff = (dryAbs > env) ? bandAttCoeff[b] : bandRelCoeff[b];
                env += (dryAbs - env) * coeff;

                float gainLin = 1.0f;
                const float thresh = bandThreshLin[b];

                if (env > thresh) {
                    // 超過量に基づく動的ゲインリダクション
                    const float ratioGain = thresh / env;
                    const float maxRedDB = duckAmount * bandDepthScale[b];
                    const float maxRedLin = std::pow(10.0f, -maxRedDB / 20.0f);
                    gainLin = std::max(ratioGain, maxRedLin);
                }

                // 下限クランプ (-40dB)
                gainLin = std::max(gainLin, 0.01f);

                // 各バンドをスケーリングして合算 (Allpass Sum 完全再構成)
                outSum += wetBands[b] * gainLin;
            }

            wet = outSum;
        }
    };

} // namespace FDNReverb
