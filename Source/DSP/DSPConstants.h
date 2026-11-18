#pragma once
#include <array>
namespace FDNReverb {
    // ── Compile-time constants ────────────────────────────────────────────────────
    static constexpr int FDN_N = 8;          // FDN order (チャンネル数, 旧定義/参考用)
    static constexpr int SAPF_STAGES = 3;    // allpass stages per delay line
    static constexpr int ABSO_STAGES = 3;    // Stage 1 用: Jot 1次 + LF/HF ユーザー補正
    static constexpr int ER_TAPS = 16;       // early-reflection FIR taps

    // Stage 2 (Välimäki–Liski 累積バイカッドGEQ) 用の段数:
    //  10 段: 10 オクターブバンド GEQ (Interaction Matrix + WLS でフィット)
    //
    // 設計上の重要事項:
    //   - 中域基準ゲイン midGain は GEQ の最初の段 (band 0) の b0/b1/b2 係数に
    //     吸収させる。これにより独立した DC スカラー適用を不要にし、
    //     二重ゲイン問題を根本的に排除する。
    //   - LF Absorption / HF Damping ユーザー補正は GEQ 目標 dB に直接加算する形で
    //     組み込む。独立段は持たない。
    //   - 目標 dB は 0 以下にクランプし、ループゲイン ≤ 1 を数学的に保証する。
    // 11 段目 (index 10) は Plate 専用の APF 群遅延補償段 (phase3/plate234 候補B3)。
    // 他トポロジでは恒等係数を入れ、ビット透明を保つ。
    static constexpr int ABSO_STAGES_S2 = 11;

    // 入力ディフューザ段数 (phase3/diffusion 候補(a) で 4 → 6)。
    // メモリプールの要求側・積算側・処理ループは必ずこの単一定数を共用する。
    // 両側の数え間違いで末尾段に nullptr が渡り、その段が純ゲイン化して
    // 全アルゴの T30 が無言で劣化した事故の恒常対策。
    static constexpr int INPUT_DIFFUSER_STAGES = 6;

    // Mutually-prime base delays (samples @ 48 kHz), log-distributed 30–130 ms
    static constexpr std::array<int, FDN_N> BASE_PRIMES_48K = {
        1451, 1693, 1979, 2311, 2683, 3067, 3491, 3923
    };
} // namespace FDNReverb