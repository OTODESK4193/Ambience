#pragma once
// ============================================================================
//  JuceCompat.h - the entire JUCE surface the Ambience DSP actually uses.
// ============================================================================
//  UniversalEngine.cpp, BiquadFilters.cpp, MagnitudeResponseFitter.cpp and
//  AcousticMetrics.cpp between them reference exactly six JUCE things:
//
//      juce::jlimit (14x)   juce::jmax (4x)   juce::jmin (1x)
//      juce::MathConstants<T>::pi (5x)
//      juce::Decibels::decibelsToGain (4x)
//      juce::roundToInt (1x)
//
//  plus juce::String, which appears only as the type of the ParamID constants
//  in PluginParameters.h. That is the whole list, so this header lets every
//  one of those translation units compile UNMODIFIED against it.
//
//  juce::dsp::DelayLine and juce::dsp::ProcessSpec are deliberately absent:
//  their only users are Source/DSP/EarlyReflections.* and Source/DSP/SAPFStage.*,
//  which are not in the VST3 build either (absent from CMakeLists.txt, included
//  by nothing) and are not built here.
//
//  JuceHeader.h in this directory is a stub that pulls this in, so `-Ilv2/src`
//  is all it takes to satisfy the `#include <JuceHeader.h>` lines in the DSP
//  sources without touching them.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <string>

namespace juce
{
    // --- numeric helpers -----------------------------------------------------
    // Note the argument order: JUCE puts the value LAST.
    template <typename T>
    constexpr T jlimit (T lowerLimit, T upperLimit, T valueToConstrain) noexcept
    {
        return valueToConstrain < lowerLimit ? lowerLimit
             : (upperLimit < valueToConstrain ? upperLimit : valueToConstrain);
    }

    template <typename T> constexpr T jmax (T a, T b) noexcept { return a < b ? b : a; }
    template <typename T> constexpr T jmin (T a, T b) noexcept { return b < a ? b : a; }

    inline int roundToInt (double v) noexcept
    {
        return static_cast<int> (v + (v < 0.0 ? -0.5 : 0.5));
    }
    inline int roundToInt (float v) noexcept
    {
        return static_cast<int> (v + (v < 0.0f ? -0.5f : 0.5f));
    }

    template <typename T>
    struct MathConstants
    {
        static constexpr T pi      = static_cast<T> (3.141592653589793238L);
        static constexpr T twoPi   = static_cast<T> (2 * 3.141592653589793238L);
        static constexpr T halfPi  = static_cast<T> (3.141592653589793238L / 2);
        static constexpr T euler   = static_cast<T> (2.718281828459045235L);
        static constexpr T sqrt2   = static_cast<T> (1.414213562373095048L);
    };

    // --- decibels ------------------------------------------------------------
    // Matches juce::Decibels: anything at or below minusInfinityDb is silence,
    // which the engine relies on for the -60 dB bottom of the Wet/Dry range.
    struct Decibels
    {
        template <typename T>
        static T decibelsToGain (T decibels, T minusInfinityDb = static_cast<T> (-100)) noexcept
        {
            return decibels > minusInfinityDb
                 ? std::pow (static_cast<T> (10), decibels * static_cast<T> (0.05))
                 : static_cast<T> (0);
        }

        template <typename T>
        static T gainToDecibels (T gain, T minusInfinityDb = static_cast<T> (-100)) noexcept
        {
            return gain > static_cast<T> (0)
                 ? jmax (minusInfinityDb, static_cast<T> (std::log10 (gain)) * static_cast<T> (20))
                 : minusInfinityDb;
        }
    };

    // --- String --------------------------------------------------------------
    // Only ever used for the ParamID string constants in PluginParameters.h.
    // Nothing in the DSP compares, concatenates or formats one.
    class String
    {
    public:
        String() = default;
        String (const char* s) : text (s ? s : "") {}
        String (const std::string& s) : text (s) {}

        const char* toRawUTF8()   const noexcept { return text.c_str(); }
        bool        isEmpty()     const noexcept { return text.empty(); }
        bool        isNotEmpty()  const noexcept { return ! text.empty(); }
        operator const std::string&() const noexcept { return text; }

        bool operator== (const String& o) const noexcept { return text == o.text; }
        bool operator!= (const String& o) const noexcept { return text != o.text; }

    private:
        std::string text;
    };

    // --- AudioProcessorValueTreeState ---------------------------------------
    // Declared purely so that PluginParameters.h's
    //
    //     static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    //
    // parses. That is a DECLARATION of a function nobody here calls, and
    // PluginParameters.cpp (its only definition, which really does need JUCE)
    // is not part of the LV2 build - the port table lives in lv2/src/ports.h
    // instead. Nothing beyond the nested type name is needed.
    struct AudioProcessorValueTreeState
    {
        struct ParameterLayout { };
    };
} // namespace juce

// ============================================================================
//  ScopedNoDenormals
// ============================================================================
//  Not optional here. The FDN runs 16 feedback lines through 10-band biquad
//  cascades; once the tail decays past ~1e-38 every one of those multiplies
//  becomes a denormal trap and the plugin's CPU cost jumps by an order of
//  magnitude - on a Cortex-A72 that is the difference between running and
//  xrunning. processBlock() in the VST3 opens with juce::ScopedNoDenormals for
//  exactly this reason.
//
//  aarch64 has no MXCSR; the equivalent is bit 24 (FZ, flush-to-zero) of FPCR.
//  Unlike x86 there is no separate DAZ bit - FZ covers both inputs and results.
// ============================================================================
#if defined(__aarch64__)
struct ScopedNoDenormals
{
    ScopedNoDenormals() noexcept
    {
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (saved));
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (saved | (1ull << 24)));
    }
    ~ScopedNoDenormals() noexcept
    {
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (saved));
    }
    unsigned long long saved {};
};
#elif defined(__SSE2__)
#include <xmmintrin.h>
#include <pmmintrin.h>
struct ScopedNoDenormals
{
    ScopedNoDenormals() noexcept
        : saved (_mm_getcsr())
    {
        _mm_setcsr (saved | 0x8040); // FZ | DAZ
    }
    ~ScopedNoDenormals() noexcept { _mm_setcsr (saved); }
    unsigned int saved;
};
#else
struct ScopedNoDenormals { };
#endif

namespace juce { using ::ScopedNoDenormals; }
