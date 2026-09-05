#pragma once
#include <cmath>
#include <algorithm>

namespace FDNReverb {

    /**
     * @brief プロフェッショナル・ブロードバンド・クリーンダッカー
     * (Professional Broadband Clean Ducker with Zero Phase Distortion)
     * 
     * - WET 信号の音色・位相特性を 100% 保持したまま、全体の音量のみを滑らかにダッキング
     * - 帯域分割フィルタリングを完全撤廃し、位相キャンセレーション・ノッチ干渉・ドンシャリ化を根絶
     * - ドライ信号（サイドチェイン）のピークエンベロープ追従による音楽的なソフトニー制御
     * - duckingAmount <= 0.001f 時の完全ゼロコスト・バイパス (IEEE 754 1ビット完全一致保証)
     */
    class DynamicEQDucker {
    public:
        DynamicEQDucker() noexcept {
            reset();
        }

        void prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
            fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;
            reset();
            updateCoefficients();
        }

        void reset() noexcept {
            envelope = 0.0f;
            gainSmoothed = 1.0f;
        }

        /**
         * @brief パラメータ設定
         * @param amountDB ダッキング量 [0.0, 40.0] dB
         * @param attackMs アタック時間 [0.5, 500.0] ms
         * @param releaseMs リリース時間 [10.0, 2000.0] ms
         * @param threshDB スレッショルド [-60.0, 0.0] dBFS
         */
        void setParameters(float amountDB, float attackMs, float releaseMs, float threshDB) noexcept {
            duckAmount = std::clamp(amountDB, 0.0f, 40.0f);
            baseAttackMs = std::clamp(attackMs, 0.5f, 500.0f);
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
            if (duckAmount <= 0.001f) [[likely]] {
                return;
            }

            // 1. ドライ入力のステレオピーク検出 (アンチデノーマル保護)
            constexpr float ANTI_DENORMAL = 1e-18f;
            const float dryPeak = std::max(std::abs(dryL), std::abs(dryR)) + ANTI_DENORMAL;

            // 2. エンベロープ追従 (アタック / リリース)
            const float envCoeff = (dryPeak > envelope) ? attackCoeff : releaseCoeff;
            envelope += (dryPeak - envelope) * envCoeff;

            // 3. ゲインリダクションの計算 (音楽的ソフトニー特性)
            float targetGain = 1.0f;
            if (envelope > threshLin) {
                // スレッショルド超過量に基づく比率ゲイン
                const float ratioGain = threshLin / envelope;
                targetGain = std::max(ratioGain, maxReductionLin);
            }

            // 4. 超短時定数スムージング (急激な変化によるジッパーノイズ防止)
            gainSmoothed += (targetGain - gainSmoothed) * smoothCoeff;

            // 5. WET信号への乗算 (周波数特性・位相は 100% 保持、音量のみ純粋に減衰)
            wetL *= gainSmoothed;
            wetR *= gainSmoothed;
        }

    private:
        double fs{ 48000.0 };
        float duckAmount{ 0.0f };
        float baseAttackMs{ 10.0f };
        float baseReleaseMs{ 200.0f };
        float baseThreshDB{ -20.0f };

        float envelope{ 0.0f };
        float gainSmoothed{ 1.0f };

        float threshLin{ 0.1f };
        float maxReductionLin{ 1.0f };
        float attackCoeff{ 0.0f };
        float releaseCoeff{ 0.0f };
        float smoothCoeff{ 0.0f };

        void updateCoefficients() noexcept {
            const float fsf = static_cast<float>(fs);

            threshLin = std::pow(10.0f, baseThreshDB / 20.0f);
            maxReductionLin = std::pow(10.0f, -duckAmount / 20.0f);

            const float attSec = std::max(0.0005f, baseAttackMs * 0.001f);
            const float relSec = std::max(0.005f, baseReleaseMs * 0.001f);
            attackCoeff = 1.0f - std::exp(-1.0f / (fsf * attSec));
            releaseCoeff = 1.0f - std::exp(-1.0f / (fsf * relSec));

            // ゲインスムージング時定数: 1.5ms
            smoothCoeff = 1.0f - std::exp(-1.0f / (fsf * 0.0015f));
        }
    };

} // namespace FDNReverb
