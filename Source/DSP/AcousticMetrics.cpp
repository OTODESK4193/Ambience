#include "AcousticMetrics.h"
#include <algorithm>
#include <cmath>

namespace FDNReverb {

    void AcousticMetrics::prepare(double sr, float windowMs) {
        sampleRate = sr;
        analysisWindowMs = windowMs;

        // サンプルレートに応じて時間単位をサンプル数に変換
        samples50ms = static_cast<int>(0.050 * sr);
        samples80ms = static_cast<int>(0.080 * sr);
        analysisWindowSamples = static_cast<int>(windowMs * 0.001 * sr);

        // バッファサイズは解析窓長 + マージン
        size_t bufferSize = static_cast<size_t>(analysisWindowSamples + samples80ms + 64);
        energyHistory.assign(bufferSize, 0.0f);

        reset();
    }

    void AcousticMetrics::reset() noexcept {
        std::fill(energyHistory.begin(), energyHistory.end(), 0.0f);
        historyWritePos = 0;
        recent50msEnergy = 0.0;
        recent80msEnergy = 0.0;
        totalEnergy = 0.0;
        energyPeak = 0.0f;
        energyPeakPos = 0;
        updateCounter = 0;

        d50.store(0.0f, std::memory_order_relaxed);
        c50.store(0.0f, std::memory_order_relaxed);
        c80.store(0.0f, std::memory_order_relaxed);
        edt.store(0.0f, std::memory_order_relaxed);

        erServoGain = 1.0f;
        lateServoGain = 1.0f;
    }

    void AcousticMetrics::processSample(float sample) noexcept {
        if (energyHistory.empty()) return;

        const int bufferSize = static_cast<int>(energyHistory.size());

        // 現在のサンプルのエネルギー (二乗)
        float currentEnergy = sample * sample;

        // リングバッファ書き込み
        energyHistory[historyWritePos] = currentEnergy;

        // 累積値の更新（インクリメンタル）
        // 50ms 窓に入る要素を追加、出る要素を引く
        const int read50Pos = (historyWritePos - samples50ms + bufferSize) % bufferSize;
        const int read80Pos = (historyWritePos - samples80ms + bufferSize) % bufferSize;
        const int readWindowPos = (historyWritePos - analysisWindowSamples + bufferSize) % bufferSize;

        recent50msEnergy += currentEnergy - energyHistory[read50Pos];
        recent80msEnergy += currentEnergy - energyHistory[read80Pos];
        totalEnergy += currentEnergy - energyHistory[readWindowPos];

        // ピーク検出（EDT 推定用）
        if (currentEnergy > energyPeak) {
            energyPeak = currentEnergy;
            energyPeakPos = historyWritePos;
        }

        // 書き込みポジション進める
        historyWritePos = (historyWritePos + 1) % bufferSize;

        // 数値安定化（負の累積値を 0 に）
        if (recent50msEnergy < 0.0) recent50msEnergy = 0.0;
        if (recent80msEnergy < 0.0) recent80msEnergy = 0.0;
        if (totalEnergy < 0.0) totalEnergy = 0.0;

        // 指標値を一定間隔で更新
        if (++updateCounter >= kUpdateInterval) {
            updateMetrics();
            updateCounter = 0;
        }
    }

    void AcousticMetrics::updateMetrics() noexcept {
        // 50ms 以降のエネルギー
        double energy50ToInf = totalEnergy - recent50msEnergy;
        if (energy50ToInf < 1e-12) energy50ToInf = 1e-12;

        // 80ms 以降のエネルギー
        double energy80ToInf = totalEnergy - recent80msEnergy;
        if (energy80ToInf < 1e-12) energy80ToInf = 1e-12;

        // 全体エネルギー（最小値クリッピング）
        double totalSafe = std::max(1e-12, totalEnergy);

        // ── D50 計算 (0~1 比率) ──
        float d50val = static_cast<float>(recent50msEnergy / totalSafe);
        d50val = std::min(1.0f, std::max(0.0f, d50val));
        d50.store(d50val, std::memory_order_relaxed);

        // ── C50 計算 (dB) ──
        float c50val = static_cast<float>(10.0 * std::log10(recent50msEnergy / energy50ToInf));
        c50val = std::min(60.0f, std::max(-60.0f, c50val));
        c50.store(c50val, std::memory_order_relaxed);

        // ── C80 計算 (dB) ──
        float c80val = static_cast<float>(10.0 * std::log10(recent80msEnergy / energy80ToInf));
        c80val = std::min(60.0f, std::max(-60.0f, c80val));
        c80.store(c80val, std::memory_order_relaxed);

        // ── EDT 推定 (簡易版) ──
        // ピーク後、エネルギーが 1/10 (10dB減衰) に達するまでの時間
        // ※ 厳密な EDT 計算は IR 解析が必要だが、ここではピーク減衰時間を簡易推定
        float edtVal = 0.0f;
        if (energyPeak > 1e-9f) {
            // 解析窓内でピークを検索済み
            // ピーク以降のサンプルを走査して 1/10 になるまでの時間を計算
            const int bufferSize = static_cast<int>(energyHistory.size());
            int searchStart = energyPeakPos;
            float threshold = energyPeak * 0.1f;  // 10dB 減衰
            int decaySamples = 0;
            for (int i = 1; i < analysisWindowSamples; ++i) {
                int pos = (searchStart + i) % bufferSize;
                if (energyHistory[pos] < threshold) {
                    decaySamples = i;
                    break;
                }
            }
            edtVal = static_cast<float>(decaySamples) / static_cast<float>(sampleRate) * 6.0f;
            // ※ 10dB 減衰時間 × 6 ≈ EDT (60dB減衰時間に相当する補正)
        }
        edt.store(edtVal, std::memory_order_relaxed);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 描画用: 指定時間オフセット秒前の瞬時エネルギーを取得
    // ─────────────────────────────────────────────────────────────────────────────
    //   secondsAgo: 何秒前のデータを取得するか
    //   戻り値: その時点のエネルギー値 (二乗値、正規化なし)
    //
    //   GUI から呼ばれることを想定しており、リングバッファから直接読む。
    //   範囲外の場合は 0 を返す。
    // ─────────────────────────────────────────────────────────────────────────────
    float AcousticMetrics::getEnergyAtTimeOffset(float secondsAgo) const noexcept {
        if (energyHistory.empty()) return 0.0f;

        const int bufferSize = static_cast<int>(energyHistory.size());
        int offsetSamples = static_cast<int>(secondsAgo * static_cast<float>(sampleRate));

        // 範囲チェック
        if (offsetSamples < 0) offsetSamples = 0;
        if (offsetSamples >= analysisWindowSamples) return 0.0f;

        // リングバッファから読み出し
        int readPos = (historyWritePos - 1 - offsetSamples + bufferSize) % bufferSize;
        return energyHistory[readPos];
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // 入力アクティブ判定: 近時 50ms に有意なエネルギーがあるか
    // ─────────────────────────────────────────────────────────────────────────────
    //   GUI が「実測線を表示するかどうか」を判断するために使用。
    //   閾値: -60dBFS 相当 (1e-6) のエネルギー
    // ─────────────────────────────────────────────────────────────────────────────
    bool AcousticMetrics::isActive() const noexcept {
        // 50ms 内に有意なエネルギーがあるか判定
        constexpr double kActivityThreshold = 1e-6;  // -60dBFS 相当
        return recent50msEnergy > kActivityThreshold;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  C80 自動調整サーボ
    // ─────────────────────────────────────────────────────────────────────────────
    void AcousticMetrics::updateServo(float targetClarityOffsetDB, int blockSize) noexcept {
        // デフォルト (0.0dB) 時は完全バイパス (IEEE 754 厳密にビット一致保証)
        if (std::abs(targetClarityOffsetDB) < 0.001f) {
            erServoGain = 1.0f;
            lateServoGain = 1.0f;
            return;
        }

        // 有意な入力エネルギーがない場合はゲインを 1.0f へ緩やかに復帰
        if (!isActive()) {
            constexpr float returnAlpha = 0.05f;
            erServoGain += (1.0f - erServoGain) * returnAlpha;
            lateServoGain += (1.0f - lateServoGain) * returnAlpha;
            return;
        }

        // 目標 C80 オフセットに応じて ER と Late のバランスをサーボ制御
        // targetClarityOffsetDB > 0: 明瞭度重視 (ER を微増、Late を微減)
        // targetClarityOffsetDB < 0: 拡散性重視 (ER を微減、Late を微増)
        // 最大変動幅は ±3dB (約 0.707 〜 1.414) に安全クランプ
        const float targetGainER = std::clamp(std::pow(10.0f, (targetClarityOffsetDB * 0.25f) / 20.0f), 0.70f, 1.40f);
        const float targetGainLate = std::clamp(std::pow(10.0f, (-targetClarityOffsetDB * 0.25f) / 20.0f), 0.70f, 1.40f);

        // 100ms 時定数のスムーズ追従 (クリック・ポンピング防止)
        const float timeConstSmp = 0.100f * static_cast<float>(sampleRate);
        const float alpha = 1.0f - std::exp(-static_cast<float>(blockSize) / timeConstSmp);

        erServoGain += (targetGainER - erServoGain) * alpha;
        lateServoGain += (targetGainLate - lateServoGain) * alpha;
    }

}  // namespace FDNReverb