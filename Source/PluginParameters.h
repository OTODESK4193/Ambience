#pragma once

#include <JuceHeader.h>
#include <array>

namespace FDNReverb {

    namespace ParamID {
        inline const juce::String Algorithm = "algorithm";
        inline const juce::String PreDelay = "predelay";
        inline const juce::String RoomSize = "roomsize";
        inline const juce::String DecayTime = "decaytime";
        inline const juce::String HFDamping = "hfdamping";
        inline const juce::String LFAbsorption = "lfabsorption";
        inline const juce::String Diffusion = "diffusion";
        inline const juce::String ModAmount = "modamount";
        inline const juce::String ModRate = "modrate";
        inline const juce::String StereoWidth = "stereowidth";
        inline const juce::String ERLevel = "erlevel";
        inline const juce::String Saturation = "saturation";
        inline const juce::String SatType = "sattype";
        inline const juce::String WetLevel = "wetlevel";
        inline const juce::String DryLevel = "drylevel";
        inline const juce::String DuckAmount = "duckamount";
        inline const juce::String DuckAttack = "duckattack";
        inline const juce::String DuckRelease = "duckrelease";
        inline const juce::String DuckThresh = "duckthresh";
        inline const juce::String ERSolo = "ersolo";
        inline const juce::String ProMode = "promode";
        inline const juce::String TiltLow = "tiltlow";
        inline const juce::String TiltMid = "tiltmid";
        inline const juce::String TiltHigh = "tilthigh";
        inline const juce::String RTBand0 = "rtband0";
        inline const juce::String RTBand1 = "rtband1";
        inline const juce::String RTBand2 = "rtband2";
        inline const juce::String RTBand3 = "rtband3";
        inline const juce::String RTBand4 = "rtband4";
        inline const juce::String RTBand5 = "rtband5";
        inline const juce::String RTBand6 = "rtband6";
        inline const juce::String RTBand7 = "rtband7";
        inline const juce::String RTBand8 = "rtband8";
        inline const juce::String RTBand9 = "rtband9";
        inline const juce::String LoCut = "locut";
        inline const juce::String HiCut = "hicut";
        inline const juce::String Theme = "theme";
    }

#include "DSP/DSPParams.h"

    class ParameterHelper {
    public:
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    };

} // namespace FDNReverb