#pragma once
#include <cmath>
#include <algorithm>
#include <bit>
#include <cstdint>
#include "Saturator.h" // for the shared SaturationMode enum (interface type)

// ─────────────────────────────────────────────────────────────────────────────
//  SaturatorADAA — double-precision ADAA1 saturator prototype (HVSE reference)
//
//  Drop-in alternative to FDNReverb::Saturator with an identical public API
//  (prepare / reset / setMode / setAmount / processSample, same
//  SaturationMode enum, same drive / AGC / comp-gain laws, same P3/P4
//  band-limited difference path). To try it in the engine, change the two
//  member types in UniversalEngine.h from Saturator to SaturatorADAA and
//  include this header instead of Saturator.h. Nothing else changes.
//
//  Differences from the shipping float32 Saturator:
//   1. All internal math is IEEE-754 double (state, antiderivatives,
//      smoothing, crossfade, DC blocker, difference LPFs).
//   2. The |dx| fallback is the 2nd-derivative Taylor form used by HVSE's
//      ADAA1Waveshaper (Source/DSP/ADAA1Waveshaper.h):
//          y = f(x_bar) + (dx^2 / 24) * f''(x_bar),   x_bar = (x0 + x1) / 2
//      instead of the plain midpoint f(x_bar). All four modes supply an
//      analytic f''.
//   3. Antiderivatives are written in cancellation-free form:
//        Tube: sqrt(1+x^2) - 1  ==  x^2 / (sqrt(1+x^2) + 1)   (HVSE
//              AlgebraicADAA1Policy rationalisation), and x - atan(x) uses
//              its Maclaurin series for |x| < 0.01.
//        Warm: ln(cosh(x)) and tanh(x) are evaluated together with HVSE's
//              math_fast kernels (Pade(6,6) + Cody-Waite exp, atanh-series
//              log with the (1+e)/2 cancellation trick, Lambert continued
//              fraction for tanh at |x| <= 0.66, Maclaurin series below
//              1e-4, asymptotic form above 35).
//        Tape: ln(1 + k^2 x^2) via log1p.
//   4. Optional sinc droop compensation on the wet path: the 3-tap
//      symmetric FIR of HVSE's SincDroopCompensator,
//          y = -c * x + (1 + 2c) * z1 - c * z2,
//      which has exactly 1 sample of group delay; the dry reference of the
//      difference path is delayed by the same 1 sample so wet and dry stay
//      time-aligned (total added latency: 1 sample, only while
//      amount > 0; the amount == 0 bypass is untouched and bit-exact).
//      HVSE ships c = 0.25 because their ADAA runs as a 2-stage cascade
//      (droop cos^2(w/2) ~ 1 - w^2/4). Ambience uses a single ADAA1 stage
//      (droop sinc(w/2) ~ 1 - w^2/24), so the default here is c = 1/24,
//      which cancels the w^2 term of a single stage. Override with
//      AMBIENCE_ADAA_DROOP_COEFF if you want the HVSE value.
//
//  Note on the HVSE reference: AlgebraicADAA1Policy::evalF2ndDeriv in HVSE
//  returns -3 * x * f(x) / (1 + x^2)^2, which carries an extra factor of x
//  versus the analytic -3x / (1 + x^2)^(5/2). This prototype implements the
//  analytic form (verified by finite differences in the test harness).
// ─────────────────────────────────────────────────────────────────────────────

#ifndef AMBIENCE_ADAA_DROOP_COEFF
#define AMBIENCE_ADAA_DROOP_COEFF (1.0 / 24.0)
#endif

namespace FDNReverb {

namespace adaa_detail {

// Pade(6,6) + Cody-Waite exp(z) for z in [-100, 100] (after HVSE math_fast::exp64;
// relative error < 1.2e-20). nearbyint replaces HVSE's SSE round intrinsic so
// the header stays free of ISA-specific includes.
[[nodiscard]] inline double exp64(double z) noexcept {
    const double kd = std::nearbyint(z * 1.44269504088896340736);
    const double r  = std::fma(-kd, 1.42860682030941723212e-6,
                               std::fma(-kd, 0.693145751953125, z));
    const double r2 = r * r;
    const double P  = r * (1.0 + r2 * ((1.0 / 33.0) + r2 * (1.0 / 7920.0)));
    const double Q  = 1.0 + r2 * ((5.0 / 44.0) + r2 * ((1.0 / 792.0) + r2 * (1.0 / 665280.0)));
    const double expR = 1.0 + P / (Q - 0.5 * P);
    const std::uint64_t scaleBits =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(kd) + 1023) << 52;
    return expR * std::bit_cast<double>(scaleBits);
}

// Simultaneous F1(x) = ln(cosh(x)) and tanh(x), after HVSE
// math_fast::evalF1AndTanh64 (single exp64(-2|x|), cancellation-free).
inline void lnCoshAndTanh(double x, double& outLnCosh, double& outTanh) noexcept {
    constexpr double kLn2 = 0.69314718055994530942;
    const double ax = std::abs(x);
    if (ax < 1.0e-4) {
        const double x2 = x * x;
        outLnCosh = x2 * (0.5 - x2 * ((1.0 / 12.0) - x2 * (1.0 / 45.0)));
        outTanh   = x * (1.0 - x2 * ((1.0 / 3.0) - x2 * (2.0 / 15.0)));
        return;
    }
    if (ax > 35.0) {
        outLnCosh = ax - kLn2;
        outTanh   = (x >= 0.0) ? 1.0 : -1.0;
        return;
    }
    const double e = exp64(-2.0 * ax);
    if (ax <= 0.66) {
        const double x2  = x * x;
        const double num = x * (2027025.0 + x2 * (270270.0 + x2 * (6930.0 + 36.0 * x2)));
        const double den = 2027025.0 + x2 * (945945.0 + x2 * (51975.0 + x2 * (630.0 + x2)));
        outTanh = num / den;
    } else {
        const double absTanh = (1.0 - e) / (1.0 + e);
        outTanh = (x >= 0.0) ? absTanh : -absTanh;
    }
    double s, baseOffset;
    if (e > 0.4142135623730950488) {
        s = (e - 1.0) / (e + 3.0);
        baseOffset = ax;
    } else {
        s = e / (2.0 + e);
        baseOffset = ax - kLn2;
    }
    const double s2 = s * s;
    const double poly = 1.0 + s2 * ((1.0 / 3.0) + s2 * (0.2 + s2 * ((1.0 / 7.0)
                      + s2 * ((1.0 / 9.0) + s2 * ((1.0 / 11.0) + s2 * ((1.0 / 13.0)
                      + s2 * ((1.0 / 15.0) + s2 * ((1.0 / 17.0) + s2 * (1.0 / 19.0)))))))));
    outLnCosh = std::fma(2.0 * s, poly, baseOffset);
}

// x - atan(x) without cancellation (Maclaurin for small |x|).
inline double xMinusAtan(double x) noexcept {
    const double ax = std::abs(x);
    if (ax < 0.01) {
        const double x2 = x * x;
        return x * x2 * ((1.0 / 3.0) - x2 * (0.2 - x2 * ((1.0 / 7.0) - x2 * (1.0 / 9.0))));
    }
    return x - std::atan(x);
}

} // namespace adaa_detail

class SaturatorADAA {
public:
    SaturatorADAA() = default;

    void prepare(double sampleRate) noexcept {
        fs = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        dcR = 1.0 - (2.0 * 3.14159265358979323846 * 15.0 / fs);
        lpCoeff = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 4500.0 / fs);
        amountSteps = std::max(1.0, fs * 0.020);
        amountCurrent = 0.0;
        amountTarget = 0.0;
        amountInc = 0.0;
        modeCrossfadeInc = 1.0 / (fs * 0.020);
        reset();
    }

    void reset() noexcept {
        // [phase3/mode-fix] クロスフェード進行中の reset() は、要求済み
        // モード (pendingMode) を確定させてから状態を消去する。従来は
        // isModeCrossfading だけが落とされ pendingMode が残留するため、
        // activeMode != pendingMode の不整合状態のまま固定され、以降の
        // setMode(pendingMode) がガード条件 (pendingMode != mode) で
        // no-op になり、要求モードへ二度と到達できなかった。実測済みの
        // 経路: プリセット読込で setParams の setMode がクロスフェードを
        // 開始 → 8ms グレースフルミュート遷移完了時の panicReset() が
        // 本関数でキャンセル → updateTopologyAndRouting() の setMode が
        // no-op になり、モードが Warm に固定される。
        // reset() は信号履歴を全消去する遷移点 (prepare / panicReset)
        // でのみ呼ばれるため、クロスフェードの残りを省略して確定させ
        // てもクリックは発生しない。これにより reset() 後は常に
        // activeMode == pendingMode の整合状態になり、
        // captureResetState() の捕捉値も整合する。
        if (isModeCrossfading) {
            activeMode = pendingMode;
        }
        lpDiffState = 0.0;
        lpDiffState2 = 0.0;
        stateActive.reset();
        statePending.reset();
        isModeCrossfading = false;
        modeCrossfadePos = 1.0;
        wetZ1 = 0.0;
        wetZ2 = 0.0;
        dryZ1 = 0.0;
    }

    // [phase3/small-fixes] reset() は amount ランプ状態と active/pending
    // モードを戻さない (prepare では 0 / Warm に初期化される)。呼び出し側が
    // prepare 終端の値を捕捉し reset() の後に復元するためのアクセサ。
    // 復元先の状態は reset() 直後の信号状態 (履歴ゼロ、クロスフェード完了)
    // と整合する: prepare 終端でもクロスフェードは発生していない。
    struct ResetState {
        double amountCurrent{ 0.0 }, amountTarget{ 0.0 }, amountInc{ 0.0 };
        SaturationMode activeMode{ SaturationMode::Warm };
        SaturationMode pendingMode{ SaturationMode::Warm };
    };
    ResetState captureResetState() const noexcept {
        return { amountCurrent, amountTarget, amountInc, activeMode, pendingMode };
    }
    void restoreResetState(const ResetState& s) noexcept {
        amountCurrent = s.amountCurrent;
        amountTarget = s.amountTarget;
        amountInc = s.amountInc;
        activeMode = s.activeMode;
        pendingMode = s.pendingMode;
    }

    void setMode(SaturationMode mode) noexcept {
        if (activeMode != mode && pendingMode != mode) {
            if (isModeCrossfading) {
                activeMode = pendingMode;
                stateActive = statePending;
            }
            pendingMode = mode;
            statePending.x1_scalar = stateActive.x1_scalar;
            statePending.dcInPrev = 0.0;
            statePending.dcOutPrev = 0.0;
            isModeCrossfading = true;
            modeCrossfadePos = 0.0;
        }
    }

    void setMode(int modeIndex) noexcept {
        setMode(static_cast<SaturationMode>(std::clamp(modeIndex, 0, 3)));
    }

    void setAmount(float amount) noexcept {
        amountTarget = static_cast<double>(std::clamp(amount, 0.0f, 1.0f));
        amountInc = (amountTarget - amountCurrent) / amountSteps;
    }

    void setDroopCompEnabled(bool enabled) noexcept { droopEnabled = enabled; }
    [[nodiscard]] bool isDroopCompEnabled() const noexcept { return droopEnabled; }

    inline float processSample(float inF) noexcept {
        // 20 ms linear amount ramp (double version of the shipping smoother).
        if (amountInc != 0.0) {
            amountCurrent += amountInc;
            if ((amountInc > 0.0 && amountCurrent >= amountTarget) ||
                (amountInc < 0.0 && amountCurrent <= amountTarget)) {
                amountCurrent = amountTarget;
                amountInc = 0.0;
            }
        }
        const double curAmount = amountCurrent;
        const double in = static_cast<double>(inF);

        // Steady-state zero-cost bypass (bit-exact, zero latency), as shipping.
        if (curAmount < 1e-6 && amountInc == 0.0 && !isModeCrossfading) {
            stateActive.x1_scalar = in;
            lpDiffState = 0.0;
            lpDiffState2 = 0.0;
            wetZ1 = 0.0;
            wetZ2 = 0.0;
            dryZ1 = in;
            return inF;
        }

        const double drive = 1.0 + curAmount * 3.5;

        const double compGainActive = getModeCompGain(activeMode, curAmount);
        double yActive = processCore(in, drive, activeMode, stateActive);

        double yCombined = yActive;
        double compGainCombined = compGainActive;

        if (isModeCrossfading) {
            modeCrossfadePos += modeCrossfadeInc;
            if (modeCrossfadePos >= 1.0) {
                modeCrossfadePos = 1.0;
                isModeCrossfading = false;
                activeMode = pendingMode;
                stateActive = statePending;
            } else {
                const double compGainPending = getModeCompGain(pendingMode, curAmount);
                double yPending = processCore(in, drive, pendingMode, statePending);
                const double fadeAngle = modeCrossfadePos * 1.57079632679489661923;
                const double wActive  = std::cos(fadeAngle);
                const double wPending = std::sin(fadeAngle);
                yCombined = yActive * wActive + yPending * wPending;
                compGainCombined = compGainActive * wActive + compGainPending * wPending;
            }
        }

        double wet = yCombined * compGainCombined;
        double dryRef = in;
        if (droopEnabled) {
            // HVSE SincDroopCompensator FIR on the wet signal; the dry
            // reference takes the same 1-sample delay so the difference
            // path stays time-aligned.
            constexpr double c = AMBIENCE_ADAA_DROOP_COEFF;
            const double wetComp = -c * wet + (1.0 + 2.0 * c) * wetZ1 - c * wetZ2;
            wetZ2 = wetZ1;
            wetZ1 = wet;
            wet = wetComp;
            dryRef = dryZ1;
        }
        dryZ1 = in;

        // P3/P4 band-limited difference path (unchanged law, double math).
        const double diff = (wet - dryRef) * curAmount;
        lpDiffState += lpCoeff * (diff - lpDiffState);
        lpDiffState2 += lpCoeff * (lpDiffState - lpDiffState2);
        return static_cast<float>(dryRef + lpDiffState2);
    }

private:
    struct EngineState {
        double x1_scalar{ 0.0 };
        double adPrev{ 0.0 };
        bool hasAdPrev{ false };
        double dcInPrev{ 0.0 };
        double dcOutPrev{ 0.0 };
        void reset() noexcept {
            x1_scalar = 0.0;
            adPrev = 0.0;
            hasAdPrev = false;
            dcInPrev = 0.0;
            dcOutPrev = 0.0;
        }
    };

    double fs{ 48000.0 };
    double dcR{ 0.998 };
    double lpCoeff{ 0.443 };
    double lpDiffState{ 0.0 };
    double lpDiffState2{ 0.0 };

    double amountCurrent{ 0.0 };
    double amountTarget{ 0.0 };
    double amountInc{ 0.0 };
    double amountSteps{ 960.0 };

    SaturationMode activeMode{ SaturationMode::Warm };
    SaturationMode pendingMode{ SaturationMode::Warm };
    EngineState stateActive;
    EngineState statePending;

    bool isModeCrossfading{ false };
    double modeCrossfadePos{ 1.0 };
    double modeCrossfadeInc{ 0.001 };

    bool droopEnabled{ true };
    double wetZ1{ 0.0 };
    double wetZ2{ 0.0 };
    double dryZ1{ 0.0 };

    inline double getModeCompGain(SaturationMode mode, double amt) const noexcept {
        double modeComp = 1.0;
        switch (mode) {
        case SaturationMode::Warm: modeComp = 0.82; break;
        case SaturationMode::Tape: modeComp = 0.90; break;
        case SaturationMode::Tube: modeComp = 0.95; break;
        case SaturationMode::Hard: modeComp = 0.70; break;
        }
        return (1.0 / (1.0 + amt * 2.2)) * modeComp;
    }

    inline double processCore(double in, double drive, SaturationMode mode, EngineState& st) noexcept {
        const double x = in * drive;
        double y;
        const double dx = x - st.x1_scalar;
        const double ad_x = applyAD(x, mode);

        if (std::abs(dx) < 1.0e-4) {
            // HVSE Taylor fallback: f(x_bar) + (dx^2 / 24) * f''(x_bar).
            const double xMid = (x + st.x1_scalar) * 0.5;
            y = applyNL(xMid, mode) + (dx * dx * (1.0 / 24.0)) * applyD2(xMid, mode);
        } else {
            const double prevAD = st.hasAdPrev ? st.adPrev : applyAD(st.x1_scalar, mode);
            y = (ad_x - prevAD) / dx;
        }
        st.x1_scalar = x;
        st.adPrev = ad_x;
        st.hasAdPrev = true;

        if (mode == SaturationMode::Tube) {
            const double hp = y - st.dcInPrev + dcR * st.dcOutPrev;
            st.dcInPrev = y;
            st.dcOutPrev = hp;
            y = hp;
        }
        return y;
    }

    // f(x) — identical transfer laws to the shipping Saturator, in double.
    inline double applyNL(double x, SaturationMode mode) const noexcept {
        switch (mode) {
        case SaturationMode::Warm: {
            double lnCosh, t;
            adaa_detail::lnCoshAndTanh(x, lnCosh, t);
            const double sech2 = 1.0 - t * t;
            return t + 0.35 * x * sech2;
        }
        case SaturationMode::Tape: {
            constexpr double k = 1.5;
            constexpr double twoOverPi = 0.63661977236758134308;
            return 1.25 * twoOverPi * std::atan(k * x);
        }
        case SaturationMode::Tube: {
            const double x2 = x * x;
            const double invHypot = 1.0 / std::sqrt(1.0 + x2);
            return (x * invHypot) + 0.30 * (x2 / (1.0 + x2));
        }
        case SaturationMode::Hard: {
            constexpr double kHardGain = 2.5;
            return std::clamp(kHardGain * x, -1.0, 1.0);
        }
        }
        return x;
    }

    // F(x) = integral of f — analytic antiderivatives, cancellation-free.
    inline double applyAD(double x, SaturationMode mode) const noexcept {
        switch (mode) {
        case SaturationMode::Warm: {
            double lnCosh, t;
            adaa_detail::lnCoshAndTanh(x, lnCosh, t);
            return (1.0 - 0.35) * lnCosh + 0.35 * x * t;
        }
        case SaturationMode::Tape: {
            constexpr double k = 1.5;
            constexpr double twoOverPi = 0.63661977236758134308;
            const double kx = k * x;
            return 1.25 * twoOverPi * (x * std::atan(kx) - (0.5 / k) * std::log1p(kx * kx));
        }
        case SaturationMode::Tube: {
            const double x2 = x * x;
            // sqrt(1+x^2) - 1, rationalised (HVSE AlgebraicADAA1Policy).
            const double term1 = x2 / (std::sqrt(1.0 + x2) + 1.0);
            const double term2 = adaa_detail::xMinusAtan(x);
            return term1 + 0.30 * term2;
        }
        case SaturationMode::Hard: {
            constexpr double kHardGain = 2.5;
            const double u = kHardGain * x;
            const double au = std::abs(u);
            double f_val;
            if (au <= 1.0) {
                f_val = 0.5 * u * u;
            } else {
                f_val = au - 0.5;
            }
            return (1.0 / kHardGain) * f_val;
        }
        }
        return 0.5 * x * x;
    }

    // f''(x) — analytic second derivatives for the Taylor fallback.
    inline double applyD2(double x, SaturationMode mode) const noexcept {
        switch (mode) {
        case SaturationMode::Warm: {
            // f = t + c x s, t = tanh(x), s = sech^2(x) = 1 - t^2
            // f'' = -2 s t (1 + c - 2 c x t) - 2 c s t - 2 c x s^2
            constexpr double c = 0.35;
            double lnCosh, t;
            adaa_detail::lnCoshAndTanh(x, lnCosh, t);
            const double s = 1.0 - t * t;
            return -2.0 * s * t * (1.0 + c - 2.0 * c * x * t) - 2.0 * c * s * t - 2.0 * c * x * s * s;
        }
        case SaturationMode::Tape: {
            // f = A atan(kx); f'' = -A * 2 k^3 x / (1 + k^2 x^2)^2
            constexpr double k = 1.5;
            constexpr double A = 1.25 * 0.63661977236758134308;
            const double d = 1.0 + k * k * x * x;
            return -A * (2.0 * k * k * k) * x / (d * d);
        }
        case SaturationMode::Tube: {
            // g = x / sqrt(1+x^2): g'' = -3x / (1+x^2)^(5/2)
            // h = a x^2 / (1+x^2): h'' = a (2 - 6x^2) / (1+x^2)^3
            const double x2 = x * x;
            const double d = 1.0 + x2;
            const double g2 = -3.0 * x / (d * d * std::sqrt(d));
            const double h2 = 0.30 * (2.0 - 6.0 * x2) / (d * d * d);
            return g2 + h2;
        }
        case SaturationMode::Hard: {
            return 0.0; // piecewise linear
        }
        }
        return 0.0;
    }
};

} // namespace FDNReverb

#undef AMBIENCE_ADAA_DROOP_COEFF
