#pragma once
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include "Saturator.h"    // shared SaturationMode enum
#include "SaturatorADAA.h" // inner ADAA1 (current shipping implementation)

// ─────────────────────────────────────────────────────────────────────────────
//  SaturatorADAAOS2 — comparison arm: current ADAA1 saturator run at 2x
//  oversampling, oversampling applied to the saturator stage only.
//
//  Structure per base-rate sample:
//      input ── polyphase interp FIR (gain 2) ──> 2 samples @2fs
//            ── SaturatorADAA prepared at 2fs ──> 2 samples @2fs
//            ── decimation FIR + downsample ──> 1 sample @fs
//  Both FIRs are the same Kaiser-windowed sinc lowpass (N = 95 taps,
//  cutoff at the base-rate Nyquist, beta = 9.0, ~90 dB stopband class;
//  designed in prepare()). The inner class is used completely unchanged,
//  including its droop compensation and P3/P4 difference path (its
//  prepare(2fs) keeps all time constants / cutoff frequencies correct).
//
//  Latency: each FIR contributes (N-1)/2 = 47 samples at 2fs = 23.5 base
//  samples; total FIR latency = 47 base samples, plus the inner class's
//  ~1 sample at 2fs (~0.5 base). Measured total is reported by the test
//  harness. The first 47 base-rate outputs after prepare/reset are zero
//  (pipeline fill), matching the engine's warmup convention.
//
//  Bypass: while the amount is settled at exactly 0 the input is returned
//  bit-exact (same contract as the inner class), but the oversampled path
//  keeps running internally so its state stays warm. The two paths differ
//  in latency by ~47.5 samples, so presenting them with a hard switch
//  produces a one-sample step of up to |x[n] - x[n-47.5]| when the knob
//  crosses 0 in either direction (measured 0.85 FS on a 500 Hz sine at
//  amplitude 0.5 — an audible click, independent of drag speed, target
//  value and mode). The presented output therefore crossfades between
//  the dry input and the oversampled path over kPathFadeSeconds whenever
//  the bypass state changes; both settled states stay bit-exact (pure
//  dry at fade 0, pure oversampled path at fade 1).
// ─────────────────────────────────────────────────────────────────────────────

namespace FDNReverb {

class SaturatorADAAOS2 {
public:
    SaturatorADAAOS2() = default;

    // Host-reported latency in base-rate samples: FIR pair 47 + inner
    // ADAA1 ~0.5 = 47.5, reported rounded up. The bypass path (settled
    // amount == 0) is bit-exact with no added delay, so this is the
    // worst-case (saturation active) figure, constant by design.
    static constexpr int kLatencySamples = 48;

    void prepare(double sampleRate) noexcept {
        fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        inner.prepare(2.0 * fs);
        designTaps();
        amountSteps = std::max(1.0, fs * 0.020);
        amountCurrent = 0.0;
        amountTarget = 0.0;
        amountInc = 0.0;
        pathFade = 0.0; // settled amount is 0 here: dry presented
        pathFadeInc = 1.0 / std::max(1.0, fs * kPathFadeSeconds);
        pathFadePrimed = false; // first sample presents the current path directly
        reset();
    }

    void reset() noexcept {
        inner.reset();
        inHist.fill(0.0);
        vHist.fill(0.0);
        inPos = 0;
        nin = 0;
        modeFadeCountdown = 0;
    }

    void setMode(SaturationMode mode) noexcept {
        inner.setMode(mode);
        modeFadeCountdown = (int)(fs * 0.020) + 4;
    }
    void setMode(int modeIndex) noexcept {
        setMode(static_cast<SaturationMode>(std::clamp(modeIndex, 0, 3)));
    }

    void setAmount(float amount) noexcept {
        inner.setAmount(amount);
        amountTarget = static_cast<double>(std::clamp(amount, 0.0f, 1.0f));
        amountInc = (amountTarget - amountCurrent) / amountSteps;
        // Snap the last epsilon of a return to exactly 0. Without this,
        // a caller that re-issues setAmount every audio block (the engine
        // does) recomputes amountInc from the current value each block,
        // so the 20 ms ramp restarts forever and amountCurrent decays
        // geometrically ((1 - block/steps) per block) without ever
        // reaching 0: the bypass then never re-engages after a drag back
        // to 0 (measured: never within 8 s at block 256/512), and the
        // settled-at-0 bit-exact contract is silently lost. Below 1e-6
        // the amount is already treated as bypass by the gate itself,
        // so snapping changes no audible state.
        if (amountTarget == 0.0 && amountCurrent < 1e-6) {
            amountCurrent = 0.0;
            amountInc = 0.0;
        }
    }

    void setDroopCompEnabled(bool enabled) noexcept { inner.setDroopCompEnabled(enabled); }
    [[nodiscard]] bool isDroopCompEnabled() const noexcept { return inner.isDroopCompEnabled(); }

    // [phase3/small-fixes] reset() は amount ランプ状態 (自クラスと inner の
    // 双方) と pathFade 状態を戻さない。UniversalEngine が prepare 終端の
    // 値を捕捉し reset() の後に復元するためのアクセサ
    // (SaturatorADAA::ResetState 参照)。
    struct ResetState {
        SaturatorADAA::ResetState inner;
        double amountCurrent{ 0.0 }, amountTarget{ 0.0 }, amountInc{ 0.0 };
        double pathFade{ 0.0 }, pathFadeInc{ 0.0 };
        bool pathFadePrimed{ false };
    };
    ResetState captureResetState() const noexcept {
        return { inner.captureResetState(), amountCurrent, amountTarget, amountInc,
                 pathFade, pathFadeInc, pathFadePrimed };
    }
    void restoreResetState(const ResetState& s) noexcept {
        inner.restoreResetState(s.inner);
        amountCurrent = s.amountCurrent;
        amountTarget = s.amountTarget;
        amountInc = s.amountInc;
        pathFade = s.pathFade;
        pathFadeInc = s.pathFadeInc;
        pathFadePrimed = s.pathFadePrimed;
    }

    inline float processSample(float inF) noexcept {
        if (amountInc != 0.0) {
            amountCurrent += amountInc;
            if ((amountInc > 0.0 && amountCurrent >= amountTarget) ||
                (amountInc < 0.0 && amountCurrent <= amountTarget)) {
                amountCurrent = amountTarget;
                amountInc = 0.0;
            }
        }
        if (modeFadeCountdown > 0) --modeFadeCountdown;
        // Bypass matches SaturatorADAA semantics: settled amount == 0
        // returns the input bit-exactly even during a mode crossfade
        // (the fade only shapes the oversampled path, which is muted).
        // An earlier version also required modeFadeCountdown == 0; that
        // leaked the FIR-filtered dry signal for ~20 ms after a mode
        // change at amount 0 (measured: Plate sat0 render differed from
        // the ADAA1 build, max 5.4e-4). Fixed.
        const bool bypass = (amountCurrent < 1e-6 && amountInc == 0.0);

        const double x = static_cast<double>(inF);
        inHist[inPos] = x;

        // Polyphase interpolation: two 2fs samples from the base-rate history.
        // u0 uses even taps, u1 odd taps (zero-stuffed convolution, gain 2).
        double u0 = 0.0, u1 = 0.0;
        for (int k = 0; k <= 47; ++k) u0 += tapsI[2 * k] * histAt(k);
        for (int k = 0; k <= 46; ++k) u1 += tapsI[2 * k + 1] * histAt(k);
        inPos = (inPos + 1) & 127;

        const std::int64_t t0 = 2 * nin;
        vHist[(size_t)(t0 & 255)] = static_cast<double>(inner.processSample(static_cast<float>(u0)));
        vHist[(size_t)((t0 + 1) & 255)] = static_cast<double>(inner.processSample(static_cast<float>(u1)));

        float out = 0.0f;
        if (nin >= 47) {
            const std::int64_t m = nin - 47; // output base index
            double acc = 0.0;
            // Window of v centred at 2m+47 (2fs index), taps j = 0..94.
            for (int j = 0; j < kTaps; ++j)
                acc += tapsD[j] * vHist[(size_t)((2 * m + 94 - j) & 255)];
            out = static_cast<float>(acc);
        }
        ++nin;

        // Present the dry input or the oversampled path, crossfading over
        // kPathFadeSeconds whenever the bypass state changes (see header
        // note). The endpoints are returned through the exact original
        // expressions, so both settled states are bit-identical to the
        // pre-fix build; the blend only exists while a fade is running.
        const double fadeTarget = bypass ? 0.0 : 1.0;
        if (!pathFadePrimed) {
            // First sample after prepare(): present whichever path the
            // current state dictates, exactly like the pre-fix build —
            // a preset loaded with saturation > 0 must render bit-
            // identically from sample 0. Only later crossings fade.
            pathFade = fadeTarget;
            pathFadePrimed = true;
        }
        else if (pathFade < fadeTarget) pathFade = std::min(fadeTarget, pathFade + pathFadeInc);
        else if (pathFade > fadeTarget) pathFade = std::max(fadeTarget, pathFade - pathFadeInc);
        if (pathFade <= 0.0) return inF;
        if (pathFade >= 1.0) return out;
        return static_cast<float>((1.0 - pathFade) * static_cast<double>(inF)
                                  + pathFade * static_cast<double>(out));
    }

private:
    static constexpr int kTaps = 95;
    static constexpr double kBeta = 9.0;
    // Dry <-> oversampled-path presentation crossfade length. Long enough
    // to spread the 47.5-sample path step below audibility, short enough
    // that the comb dip while the two delayed copies blend stays a
    // few-ms event (measured in phase3/sat-boundary/SAT_BOUNDARY.md).
    static constexpr double kPathFadeSeconds = 0.005;

    SaturatorADAA inner;
    double fs{ 48000.0 };
    std::array<double, kTaps> tapsI{}; // interpolation (DC gain 2)
    std::array<double, kTaps> tapsD{}; // decimation (DC gain 1)
    std::array<double, 128> inHist{};
    std::array<double, 256> vHist{};
    int inPos{ 0 };
    std::int64_t nin{ 0 };
    int modeFadeCountdown{ 0 };

    double amountCurrent{ 0.0 };
    double amountTarget{ 0.0 };
    double amountInc{ 0.0 };
    double amountSteps{ 960.0 };

    double pathFade{ 0.0 };    // 0 = dry presented, 1 = oversampled path
    double pathFadeInc{ 0.0 }; // per-sample step toward the target side
    bool pathFadePrimed{ false };

    // Base-rate input history: histAt(k) = x[n - k], k = 0..47.
    inline double histAt(int k) const noexcept {
        return inHist[(inPos - k) & 127];
    }

    static double besselI0(double x) noexcept {
        // Series: I0(x) = sum_k (x^2/4)^k / (k!)^2
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;
        for (int k = 1; k < 40; ++k) {
            term *= q / ((double)k * (double)k);
            sum += term;
            if (term < 1e-18 * sum) break;
        }
        return sum;
    }

    void designTaps() noexcept {
        constexpr int N = kTaps;
        constexpr double M = (N - 1) * 0.5; // 47
        const double i0b = besselI0(kBeta);
        double h[N];
        double sum = 0.0;
        for (int n = 0; n < N; ++n) {
            const double d = n - M;
            double ideal;
            if (std::abs(d) < 1e-12) {
                ideal = 0.5; // 2*fc with fc = 0.25 cycles/sample
            } else {
                const double u = 0.5 * d; // 2*fc*d
                ideal = 0.5 * std::sin(3.14159265358979323846 * u) / (3.14159265358979323846 * u);
            }
            const double r = d / M;
            const double w = besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            h[n] = ideal * w;
            sum += h[n];
        }
        for (int n = 0; n < N; ++n) {
            tapsD[n] = h[n] / sum;
            tapsI[n] = 2.0 * h[n] / sum;
        }
    }
};

} // namespace FDNReverb
