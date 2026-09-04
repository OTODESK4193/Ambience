#pragma once
#include "DSPConstants.h"
#include <array>
#include <atomic>
#include <vector>     // ← この1行を追加

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  AcousticMetrics クラス
    // ─────────────────────────────────────────────────────────────────────────────
    //  リアルタイムでオーディオストリームから音響指標 (D50, C50, C80, EDT) を計算。
    //  
    //  原理:
    //    入力信号の二乗エネルギーをリングバッファに蓄積。
    //    各時点で「直近 50ms / 80ms のエネルギー」と「それ以降」を比較し、
    //    指標値を計算する。
    //
    //  サンプルレート対応:
    //    プリミティブな時間単位（ミリ秒）からサンプル数を逆算するため、
    //    44.1kHz から 192kHz まで自動対応。
    //
    //  CPU 負荷:
    //    計算量 O(1) per sample（エネルギー累積は加算のみ）
    //    実時間係数：≈ 0.5% 以下
    // ─────────────────────────────────────────────────────────────────────────────
    class AcousticMetrics {
    public:
        AcousticMetrics() = default;

        // ── 初期化 ──
        // sampleRate: サンプルレート (Hz)
        // analysisWindowMs: 解析窓長 (ms)。標準は 2000ms (2秒間の積分)
        void prepare(double sampleRate, float analysisWindowMs = 2000.0f);

        // ── サンプル単位の状態更新 ──
        // sample: 各サンプル時点の Wet 信号 (mono)
        void processSample(float sample) noexcept;

        // ── 指標値の取得 ──
        // 正常範囲:
        //   D50: 0.0 ~ 1.0 (典型的に 0.3~0.9)
        //   C50: -10 ~ +30 dB
        //   C80: -10 ~ +30 dB
        //   EDT: 0.0 ~ 5.0 (秒)
        float getD50() const noexcept { return d50.load(std::memory_order_relaxed); }
        float getC50() const noexcept { return c50.load(std::memory_order_relaxed); }
        float getC80() const noexcept { return c80.load(std::memory_order_relaxed); }
        float getEDT() const noexcept { return edt.load(std::memory_order_relaxed); }

        // ─── 追加: 描画用エネルギー履歴アクセサ ───
        // 指定時間オフセット秒前の瞬時エネルギー (二乗値) を取得
        float getEnergyAtTimeOffset(float secondsAgo) const noexcept;

        // 入力アクティブ判定 (近時 50ms に有意なエネルギーがあるか)
        bool isActive() const noexcept;

        // ─── C80 自動調整サーボ ───
        void updateServo(float targetClarityOffsetDB, int blockSize) noexcept;
        float getERServoGain() const noexcept { return erServoGain; }
        float getLateServoGain() const noexcept { return lateServoGain; }

        // ── リセット ──
        void reset() noexcept;

    private:
        // ── 計算ヘルパー ──
        void updateMetrics() noexcept;

        // ── パラメータ ──
        double sampleRate{ 48000.0 };
        float analysisWindowMs{ 2000.0f };

        // 50ms / 80ms のサンプル数（サンプルレート依存）
        int samples50ms{ 2400 };   // @ 48kHz
        int samples80ms{ 3840 };   // @ 48kHz
        int analysisWindowSamples{ 96000 };  // 2000ms @ 48kHz

        // ── リングバッファ ──
        // エネルギー履歴 (二乗値)
        std::vector<float> energyHistory;
        int historyWritePos{ 0 };

        // ── 累積エネルギーの統計値 ──
        // 50ms 内の累積エネルギー (近時)
        double recent50msEnergy{ 0.0 };
        // 80ms 内の累積エネルギー (近時)
        double recent80msEnergy{ 0.0 };
        // 全体の累積エネルギー
        double totalEnergy{ 0.0 };

        // EDT 用：エネルギー減衰追跡
        float energyPeak{ 0.0f };
        int energyPeakPos{ 0 };

        // ── 出力指標値 (atomic for thread safety) ──
        std::atomic<float> d50{ 0.0f };
        std::atomic<float> c50{ 0.0f };
        std::atomic<float> c80{ 0.0f };
        std::atomic<float> edt{ 0.0f };

        // ── 更新カウンター ──
        // 全サンプルで指標を再計算するのは無駄なので、
        // ある程度のサンプル間隔で更新する
        int updateCounter{ 0 };
        static constexpr int kUpdateInterval = 1024;  // 約 21ms @ 48kHz

        // ── C80 自動調整サーボゲイン ──
        float erServoGain{ 1.0f };
        float lateServoGain{ 1.0f };
    };

}  // namespace FDNReverb