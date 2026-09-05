#pragma once

#include <cmath>
#include <algorithm>

namespace FDNReverb {

    // ─────────────────────────────────────────────────────────────────────────────
    //  OutputEQ: Wet 出力段の Lo/Hi Cut (TPT Linkwitz-Riley 12dB/oct)
    // ─────────────────────────────────────────────────────────────────────────────
    //   設計方針:
    //     - アナログ状態変数フィルタ (SVF) の物理トポロジーを完全保持する
    //       Zero-Delay Feedback (TPT) 構造を採用。
    //     - Q = 0.5 (臨界制動) により、理想的な Linkwitz-Riley 12dB/oct
    //       (-6dB at fc) の完全再構成・ゼロ過渡リンギングを達成。
    //     - 双一次プリワーピングによりナイキスト周波数 (fs/2) まで歪みゼロ。
    //
    //   バイパス挙動:
    //     - Lo Cut <= 20Hz: HPF 完全バイパス (CPU 負荷ゼロ、位相回転ゼロ)
    //     - Hi Cut >= 20kHz: LPF 完全バイパス (CPU 負荷ゼロ、位相回転ゼロ)
    // ─────────────────────────────────────────────────────────────────────────────

    class OutputEQ {
    public:
        OutputEQ() = default;

        void prepare(double sampleRate) noexcept {
            fs = sampleRate;
            reset();
            setLoCutHz(20.0f);
            setHiCutHz(20000.0f);
        }

        void reset() noexcept {
            // TPT SVF 内部積分器状態 (L/R)
            s1_loL = s2_loL = 0.0f;
            s1_loR = s2_loR = 0.0f;
            s1_hiL = s2_hiL = 0.0f;
            s1_hiR = s2_hiR = 0.0f;
        }

        // ─── カットオフ・シェルフ設定 (ブロック単位で呼ぶ) ───
        enum class EQType { Off = 0, Cut = 1, Shelf = 2 };

        void setLoParams(int type, float fcHz, float gainDB) noexcept {
            loType = static_cast<EQType>(type);
            currentLoCutHz = fcHz;
            currentLoGainDB = gainDB;
            if (loType == EQType::Off || (loType == EQType::Cut && fcHz <= 20.0f) || (loType == EQType::Shelf && std::abs(gainDB) < 0.05f)) {
                loActive = false;
                return;
            }
            loActive = true;
            constexpr float pi = 3.141592653589793f;
            const float clamped = std::clamp(fcHz, 20.0f, 1000.0f);
            g_lo = std::tan(pi * clamped / static_cast<float>(fs));
            denom_lo = 1.0f / (1.0f + 2.0f * g_lo + g_lo * g_lo);
            loShelfLin = std::pow(10.0f, gainDB / 20.0f) - 1.0f;
        }

        void setHiParams(int type, float fcHz, float gainDB) noexcept {
            hiType = static_cast<EQType>(type);
            currentHiCutHz = fcHz;
            currentHiGainDB = gainDB;
            if (hiType == EQType::Off || (hiType == EQType::Cut && fcHz >= 20000.0f) || (hiType == EQType::Shelf && std::abs(gainDB) < 0.05f)) {
                hiActive = false;
                return;
            }
            hiActive = true;
            constexpr float pi = 3.141592653589793f;
            const float nyquist = static_cast<float>(fs) * 0.48f;
            const float clamped = std::clamp(fcHz, 1000.0f, std::min(20000.0f, nyquist));
            g_hi = std::tan(pi * clamped / static_cast<float>(fs));
            denom_hi = 1.0f / (1.0f + 2.0f * g_hi + g_hi * g_hi);
            hiShelfLin = std::pow(10.0f, gainDB / 20.0f) - 1.0f;
        }

        void setLoCutHz(float fcHz) noexcept {
            setLoParams(fcHz <= 20.0f ? 0 : 1, fcHz, 0.0f);
        }

        void setHiCutHz(float fcHz) noexcept {
            setHiParams(fcHz >= 20000.0f ? 0 : 1, fcHz, 0.0f);
        }

        // ─── サンプル単位の処理 (L/R を同時処理) ───
        inline void process(float& l, float& r) noexcept {
            // ── Lo Stage ──
            if (loActive) {
                // Left
                const float hpL = (l - (2.0f + g_lo) * s1_loL - s2_loL) * denom_lo;
                const float bpL = g_lo * hpL + s1_loL;
                const float lpL = g_lo * bpL + s2_loL;
                s1_loL = g_lo * hpL + bpL;
                s2_loL = g_lo * bpL + lpL;
                if (loType == EQType::Cut)
                    l = hpL;
                else if (loType == EQType::Shelf)
                    l = l + loShelfLin * lpL;

                // Right
                const float hpR = (r - (2.0f + g_lo) * s1_loR - s2_loR) * denom_lo;
                const float bpR = g_lo * hpR + s1_loR;
                const float lpR = g_lo * bpR + s2_loR;
                s1_loR = g_lo * hpR + bpR;
                s2_loR = g_lo * bpR + lpR;
                if (loType == EQType::Cut)
                    r = hpR;
                else if (loType == EQType::Shelf)
                    r = r + loShelfLin * lpR;
            }

            // ── Hi Stage ──
            if (hiActive) {
                // Left
                const float hpL = (l - (2.0f + g_hi) * s1_hiL - s2_hiL) * denom_hi;
                const float bpL = g_hi * hpL + s1_hiL;
                const float lpL = g_hi * bpL + s2_hiL;
                s1_hiL = g_hi * hpL + bpL;
                s2_hiL = g_hi * bpL + lpL;
                if (hiType == EQType::Cut)
                    l = lpL;
                else if (hiType == EQType::Shelf)
                    l = l + hiShelfLin * hpL;

                // Right
                const float hpR = (r - (2.0f + g_hi) * s1_hiR - s2_hiR) * denom_hi;
                const float bpR = g_hi * hpR + s1_hiR;
                const float lpR = g_hi * bpR + s2_hiR;
                s1_hiR = g_hi * hpR + bpR;
                s2_hiR = g_hi * bpR + lpR;
                if (hiType == EQType::Cut)
                    r = lpR;
                else if (hiType == EQType::Shelf)
                    r = r + hiShelfLin * hpR;
            }
        }

        float getCurrentLoCutHz() const noexcept { return currentLoCutHz; }
        float getCurrentHiCutHz() const noexcept { return currentHiCutHz; }
        float getCurrentLoGainDB() const noexcept { return currentLoGainDB; }
        float getCurrentHiGainDB() const noexcept { return currentHiGainDB; }
        EQType getLoType() const noexcept { return loType; }
        EQType getHiType() const noexcept { return hiType; }

    private:
        double fs{ 48000.0 };

        // ── Lo Stage ──
        EQType loType{ EQType::Off };
        bool  loActive{ false };
        float currentLoCutHz{ 20.0f };
        float currentLoGainDB{ 0.0f };
        float loShelfLin{ 0.0f };
        float g_lo{ 0.0f };
        float denom_lo{ 1.0f };
        float s1_loL{ 0.0f }, s2_loL{ 0.0f };
        float s1_loR{ 0.0f }, s2_loR{ 0.0f };

        // ── Hi Stage ──
        EQType hiType{ EQType::Off };
        bool  hiActive{ false };
        float currentHiCutHz{ 20000.0f };
        float currentHiGainDB{ 0.0f };
        float hiShelfLin{ 0.0f };
        float g_hi{ 0.0f };
        float denom_hi{ 1.0f };
        float s1_hiL{ 0.0f }, s2_hiL{ 0.0f };
        float s1_hiR{ 0.0f }, s2_hiR{ 0.0f };
    };

} // namespace FDNReverb
