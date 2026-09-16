#pragma once

#include <cmath>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  OutputEQ: Wet 出力段 Lo/Hi EQ (Zero-Delay Feedback TPT Linkwitz-Riley 12dB/oct)
    // ─────────────────────────────────────────────────────────────────────────────
    //   設計方針:
    //     - アナログ状態変数フィルタ (SVF) の物理トポロジーを完全保持する
    //       Zero-Delay Feedback (TPT) 構造を採用。
    //     - Q = 0.5 (臨界制動) により、理想的な Linkwitz-Riley 12dB/oct
    //       (-6dB at fc) の完全再構成・ゼロ過渡リンギングを達成。
    //     - 双一次プリワーピングによりナイキスト周波数 (fs/2) まで歪みゼロ。
    //
    //   ノイズフリー・無音化アーキテクチャ (Anti-Click / Seamless Modulation):
    //     1. 常時アクティブ・統一ミキシング構造 (Always-Active Unified Mixing):
    //        出力信号を y = cx * in + chp * hp + clp * lp の線形結合として統一。
    //        モード切り替え (Off ↔ Cut ↔ Shelf) やバイパス判定によるハードスイッチング
    //        （波形瞬断）を完全撤廃し、内部積分器状態を常に連続的に保持。
    //     2. サンプル単位の係数平滑化 (Sample-by-Sample One-Pole Smoothing):
    //        時定数 tau ≈ 20ms の一次平滑化フィルタにより、ノブの高速操作や DAW の
    //        オートメーション時でも一切のジッパーノイズ・ステップ歪みを根絶。
    //     3. 可聴域境界でのソフトクロスフェード (Continuous Endpoint Smoothing):
    //        LoCut 20Hz 付近 (20Hz〜30Hz) および HiCut 20kHz 付近 (18kHz〜20kHz) において
    //        一次微分連続 (C^1) な Smoothstep 関数でバイパス状態へ漸近させることで、
    //        最小/最大値到達時や境界往復時のインパルス・クリックを 100% 排除。
    //     4. シェルフゲインの数学的連続性:
    //        0dB 付近での安易なしきい値判定を廃止し、gainDB = 0dB で数学的に完全な
    //        原音バイパス (y = in) に自然一致させる。
    // ─────────────────────────────────────────────────────────────────────────────

    class OutputEQ {
    public:
        enum class EQType { Off = 0, Cut = 1, Shelf = 2 };

        OutputEQ() = default;

        void prepare(double sampleRate) noexcept {
            fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;

            // スムーザー時定数 tau = 20ms (0.02s)
            // サンプルレートに応じた平滑化係数 lambda = 1 - exp(-1 / (tau * fs))
            constexpr float tau = 0.020f;
            smoothingLambda = 1.0f - std::exp(-1.0f / (tau * static_cast<float>(fs)));

            reset();

            // 初期パラメータ設定 (デフォルト: Off, 20Hz, 20000Hz, 0dB)
            setLoParams(static_cast<int>(loType), currentLoCutHz, currentLoGainDB);
            setHiParams(static_cast<int>(hiType), currentHiCutHz, currentHiGainDB);

            // 起動時のフェードインを防止するため、現在値を目標値へ即座にスナップ
            snapToTarget();
        }

        void reset() noexcept {
            // TPT SVF 内部積分器状態 (L/R) のクリア
            s1_loL = s2_loL = 0.0f;
            s1_loR = s2_loR = 0.0f;
            s1_hiL = s2_hiL = 0.0f;
            s1_hiR = s2_hiR = 0.0f;
        }

        // ─── カットオフ・シェルフ設定 (ブロック単位で呼び出し可能) ───
        void setLoParams(int type, float fcHz, float gainDB) noexcept {
            loType = static_cast<EQType>(type);
            currentLoCutHz = fcHz;
            currentLoGainDB = gainDB;

            constexpr float pi = 3.141592653589793f;
            const float clampedFc = std::clamp(fcHz, 20.0f, 1000.0f);
            target_g_lo = std::tan(pi * clampedFc / static_cast<float>(fs));

            switch (loType) {
            case EQType::Off:
                target_cx_lo  = 1.0f;
                target_chp_lo = 0.0f;
                target_clp_lo = 0.0f;
                break;

            case EQType::Cut: {
                // 20Hz 付近 (20Hz〜30Hz) でバイパスから HPF へ C^1 連続にクロスフェード
                // 20Hz ちょうどでは完全バイパス (cx=1, chp=0) となり、周波数変更時も無音遷移
                float fade = 1.0f;
                if (fcHz <= 20.0f) {
                    fade = 0.0f;
                } else if (fcHz < 30.0f) {
                    const float t = (fcHz - 20.0f) * 0.1f; // (fcHz - 20.0f) / 10.0f
                    fade = t * t * (3.0f - 2.0f * t);      // Smoothstep: S(0)=0, S(1)=1
                }
                target_cx_lo  = 1.0f - fade;
                target_chp_lo = fade;
                target_clp_lo = 0.0f;
                break;
            }

            case EQType::Shelf: {
                const float gainLin = std::pow(10.0f, gainDB / 20.0f);
                target_cx_lo  = 1.0f;
                target_chp_lo = 0.0f;
                target_clp_lo = gainLin - 1.0f;
                break;
            }
            }
        }

        void setHiParams(int type, float fcHz, float gainDB) noexcept {
            hiType = static_cast<EQType>(type);
            currentHiCutHz = fcHz;
            currentHiGainDB = gainDB;

            constexpr float pi = 3.141592653589793f;
            const float nyquistGuard = static_cast<float>(fs) * 0.49f;
            const float clampedFc = std::clamp(fcHz, 1000.0f, std::min(20000.0f, nyquistGuard));
            target_g_hi = std::tan(pi * clampedFc / static_cast<float>(fs));

            switch (hiType) {
            case EQType::Off:
                target_dx_hi  = 1.0f;
                target_dhp_hi = 0.0f;
                target_dlp_hi = 0.0f;
                break;

            case EQType::Cut: {
                // 20000Hz 付近 (18000Hz〜20000Hz) で LPF からバイパスへ C^1 連続にクロスフェード
                // 20000Hz ちょうどでは完全バイパス (dx=1, dlp=0) となり、高域端でのクリックを根絶
                float fade = 1.0f;
                if (fcHz >= 20000.0f) {
                    fade = 0.0f;
                } else if (fcHz > 18000.0f) {
                    const float t = (20000.0f - fcHz) / 2000.0f;
                    fade = t * t * (3.0f - 2.0f * t);      // Smoothstep: S(0)=0, S(1)=1
                }
                target_dx_hi  = 1.0f - fade;
                target_dlp_hi = fade;
                target_dhp_hi = 0.0f;
                break;
            }

            case EQType::Shelf: {
                const float gainLin = std::pow(10.0f, gainDB / 20.0f);
                target_dx_hi  = 1.0f;
                target_dhp_hi = gainLin - 1.0f;
                target_dlp_hi = 0.0f;
                break;
            }
            }
        }

        void setLoCutHz(float fcHz) noexcept {
            setLoParams(fcHz <= 20.0f ? 0 : 1, fcHz, 0.0f);
        }

        void setHiCutHz(float fcHz) noexcept {
            setHiParams(fcHz >= 20000.0f ? 0 : 1, fcHz, 0.0f);
        }

        // ─── サンプル単位の処理 (L/R ステレオ処理) ───
        inline void process(float& l, float& r) noexcept {
            // ── 係数スムージング (Sample-by-Sample 1-Pole Lowpass) ──
            const float lambda = smoothingLambda;

            // Lo Stage 係数追従
            g_lo     += lambda * (target_g_lo     - g_lo);
            denom_lo = 1.0f / ((1.0f + g_lo) * (1.0f + g_lo));
            cx_lo    += lambda * (target_cx_lo    - cx_lo);
            chp_lo   += lambda * (target_chp_lo   - chp_lo);
            clp_lo   += lambda * (target_clp_lo   - clp_lo);

            // Hi Stage 係数追従
            g_hi     += lambda * (target_g_hi     - g_hi);
            denom_hi = 1.0f / ((1.0f + g_hi) * (1.0f + g_hi));
            dx_hi    += lambda * (target_dx_hi    - dx_hi);
            dhp_hi   += lambda * (target_dhp_hi   - dhp_hi);
            dlp_hi   += lambda * (target_dlp_hi   - dlp_hi);

            // ── Lo Stage (Linkwitz-Riley 12dB/oct TPT SVF) ──
            // Left
            const float inLoL = l;
            const float hpLoL = (inLoL - (2.0f + g_lo) * s1_loL - s2_loL) * denom_lo;
            const float bpLoL = g_lo * hpLoL + s1_loL;
            const float lpLoL = g_lo * bpLoL + s2_loL;
            s1_loL = g_lo * hpLoL + bpLoL;
            s2_loL = g_lo * bpLoL + lpLoL;
            l = cx_lo * inLoL + chp_lo * hpLoL + clp_lo * lpLoL;

            // Right
            const float inLoR = r;
            const float hpLoR = (inLoR - (2.0f + g_lo) * s1_loR - s2_loR) * denom_lo;
            const float bpLoR = g_lo * hpLoR + s1_loR;
            const float lpLoR = g_lo * bpLoR + s2_loR;
            s1_loR = g_lo * hpLoR + bpLoR;
            s2_loR = g_lo * bpLoR + lpLoR;
            r = cx_lo * inLoR + chp_lo * hpLoR + clp_lo * lpLoR;

            // ── Hi Stage (Linkwitz-Riley 12dB/oct TPT SVF) ──
            // Left
            const float inHiL = l;
            const float hpHiL = (inHiL - (2.0f + g_hi) * s1_hiL - s2_hiL) * denom_hi;
            const float bpHiL = g_hi * hpHiL + s1_hiL;
            const float lpHiL = g_hi * bpHiL + s2_hiL;
            s1_hiL = g_hi * hpHiL + bpHiL;
            s2_hiL = g_hi * bpHiL + lpHiL;
            l = dx_hi * inHiL + dhp_hi * hpHiL + dlp_hi * lpHiL;

            // Right
            const float inHiR = r;
            const float hpHiR = (inHiR - (2.0f + g_hi) * s1_hiR - s2_hiR) * denom_hi;
            const float bpHiR = g_hi * hpHiR + s1_hiR;
            const float lpHiR = g_hi * bpHiR + s2_hiR;
            s1_hiR = g_hi * hpHiR + bpHiR;
            s2_hiR = g_hi * bpHiR + lpHiR;
            r = dx_hi * inHiR + dhp_hi * hpHiR + dlp_hi * lpHiR;
        }

        // ─── ゲッター (後方互換性) ───
        float getCurrentLoCutHz() const noexcept { return currentLoCutHz; }
        float getCurrentHiCutHz() const noexcept { return currentHiCutHz; }
        float getCurrentLoGainDB() const noexcept { return currentLoGainDB; }
        float getCurrentHiGainDB() const noexcept { return currentHiGainDB; }
        EQType getLoType() const noexcept { return loType; }
        EQType getHiType() const noexcept { return hiType; }

    private:
        void snapToTarget() noexcept {
            g_lo     = target_g_lo;
            denom_lo = 1.0f / ((1.0f + g_lo) * (1.0f + g_lo));
            cx_lo    = target_cx_lo;
            chp_lo   = target_chp_lo;
            clp_lo   = target_clp_lo;

            g_hi     = target_g_hi;
            denom_hi = 1.0f / ((1.0f + g_hi) * (1.0f + g_hi));
            dx_hi    = target_dx_hi;
            dhp_hi   = target_dhp_hi;
            dlp_hi   = target_dlp_hi;
        }

        double fs{ 48000.0 };
        float  smoothingLambda{ 0.001f };

        // ── Lo Stage パラメータ ──
        EQType loType{ EQType::Off };
        float  currentLoCutHz{ 20.0f };
        float  currentLoGainDB{ 0.0f };

        // Lo 係数 (現在値＆ターゲット値)
        float  target_g_lo{ 0.0013f };
        float  g_lo{ 0.0013f };
        float  denom_lo{ 1.0f };

        float  target_cx_lo{ 1.0f };
        float  cx_lo{ 1.0f };
        float  target_chp_lo{ 0.0f };
        float  chp_lo{ 0.0f };
        float  target_clp_lo{ 0.0f };
        float  clp_lo{ 0.0f };

        // Lo 状態変数 (L/R)
        float  s1_loL{ 0.0f }, s2_loL{ 0.0f };
        float  s1_loR{ 0.0f }, s2_loR{ 0.0f };

        // ── Hi Stage パラメータ ──
        EQType hiType{ EQType::Off };
        float  currentHiCutHz{ 20000.0f };
        float  currentHiGainDB{ 0.0f };

        // Hi 係数 (現在値＆ターゲット値)
        float  target_g_hi{ 3.7f };
        float  g_hi{ 3.7f };
        float  denom_hi{ 1.0f };

        float  target_dx_hi{ 1.0f };
        float  dx_hi{ 1.0f };
        float  target_dhp_hi{ 0.0f };
        float  dhp_hi{ 0.0f };
        float  target_dlp_hi{ 0.0f };
        float  dlp_hi{ 0.0f };

        // Hi 状態変数 (L/R)
        float  s1_hiL{ 0.0f }, s2_hiL{ 0.0f };
        float  s1_hiR{ 0.0f }, s2_hiR{ 0.0f };
    };

} // namespace FDNReverb
