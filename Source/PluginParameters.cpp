#include "PluginParameters.h"

namespace FDNReverb {

    juce::AudioProcessorValueTreeState::ParameterLayout ParameterHelper::createLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        auto addFloat = [&](const juce::String& id,
            const juce::String& name,
            float min, float max, float def,
            float skew = 1.0f,
            const juce::String& label = "")
            {
                params.push_back(std::make_unique<juce::AudioParameterFloat>(
                    id, name,
                    juce::NormalisableRange<float>(min, max, 0.01f, skew),
                    def,
                    juce::AudioParameterFloatAttributes().withLabel(label)));
            };

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::Algorithm, "Algorithm",
            juce::StringArray{ "ROOM1","ROOM2","HALL1","HALL2","PLATE","SPRING","GOLDFOIL","INCHINDOWN" }, 0));

        addFloat(ParamID::PreDelay, "Pre-Delay", 0.0f, 500.0f, 10.0f, 1.0f, "ms");
        addFloat(ParamID::RoomSize, "Room Size", 0.3f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::DecayTime, "Decay Time", 0.1f, 120.0f, 1.5f, 0.25f, "s");
        addFloat(ParamID::HFDamping, "HF Damping", 0.0f, 1.0f, 0.0f, 1.0f, "%");
        addFloat(ParamID::LFAbsorption, "LF Absorption", 0.0f, 1.0f, 0.0f, 1.0f, "%");

        addFloat(ParamID::Diffusion, "Diffusion", 0.0f, 1.0f, 0.7f, 1.0f, "%");
        addFloat(ParamID::ModAmount, "Mod Amount", 0.0f, 1.0f, 0.25f, 1.0f, "%");
        addFloat(ParamID::ModRate, "Mod Rate", 0.05f, 2.0f, 0.5f, 0.35f, "Hz");

        addFloat(ParamID::StereoWidth, "Stereo Width", 0.0f, 1.0f, 0.8f, 1.0f, "%");

        addFloat(ParamID::ERLevel, "ER Level", 0.0f, 1.0f, 0.6f, 1.0f, "%");
        addFloat(ParamID::Saturation, "Saturation", 0.0f, 1.0f, 0.0f, 1.0f, "%");

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::SatType, "Sat Type",
            juce::StringArray{ "Warm","Tape","Tube","Hard" },
            0,
            juce::AudioParameterChoiceAttributes().withAutomatable(false)));

        // ★ Step B: Wet -6dB / Dry 0dB をデフォルトに変更
        //   内部オフセット -3dB (PluginProcessor.cpp) により
        //   実効 Wet は表示値 -3dB 分低くなる。
        //   Wet=-6dB 表示 → 実効 -9dB、 Wet=0dB 表示 → 実効 -3dB
        addFloat(ParamID::WetLevel, "Wet", -60.0f, 0.0f, -6.0f, 1.0f, "dB");
        addFloat(ParamID::DryLevel, "Dry", -60.0f, 0.0f, 0.0f, 1.0f, "dB");

        addFloat(ParamID::DuckAmount, "Ducking", 0.0f, 20.0f, 0.0f, 1.0f, "dB");
        addFloat(ParamID::DuckAttack, "Duck Attack", 0.5f, 100.0f, 10.0f, 0.4f, "ms");
        addFloat(ParamID::DuckRelease, "Duck Release", 10.0f, 2000.0f, 200.0f, 0.4f, "ms");
        addFloat(ParamID::DuckThresh, "Duck Thresh", -60.0f, 0.0f, -20.0f, 1.0f, "dB");

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::ERSolo, "ER Solo", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::ProMode, "Pro Mode", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));

        addFloat(ParamID::TiltLow, "Tilt Low", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::TiltMid, "Tilt Mid", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::TiltHigh, "Tilt High", 0.5f, 2.0f, 1.0f, 1.0f, "x");

        addFloat(ParamID::RTBand0, "RT 31Hz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand1, "RT 62Hz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand2, "RT 125Hz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand3, "RT 250Hz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand4, "RT 500Hz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand5, "RT 1kHz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand6, "RT 2kHz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand7, "RT 4kHz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand8, "RT 8kHz", 0.5f, 2.0f, 1.0f, 1.0f, "x");
        addFloat(ParamID::RTBand9, "RT 16kHz", 0.5f, 2.0f, 1.0f, 1.0f, "x");

        // ── OutEQ (Lo / Hi) ──
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::LoEQType, "Lo EQ Curve",
            juce::StringArray{ "Off", "Cut", "Shelf" }, 0));
        addFloat(ParamID::LoCut, "Lo Freq", 20.0f, 1000.0f, 20.0f, 0.3f, "Hz");
        addFloat(ParamID::LoGain, "Lo Gain", -12.0f, 12.0f, 0.0f, 1.0f, "dB");

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::HiEQType, "Hi EQ Curve",
            juce::StringArray{ "Off", "Cut", "Shelf" }, 0));
        addFloat(ParamID::HiCut, "Hi Freq", 1000.0f, 20000.0f, 20000.0f, 0.3f, "Hz");
        addFloat(ParamID::HiGain, "Hi Gain", -12.0f, 12.0f, 0.0f, 1.0f, "dB");

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            ParamID::Theme, "Theme",
            juce::StringArray{ "Cyber Neon", "Solar Flare", "Matrix Glow", "Vaporwave", "Dark Amber", "Nordic Frost", "Deep Purple", "Midnight", "Blood Moon", "Monochrome" }, 0));

        // ── PRO Tab 物理音響パラメータ ──
        addFloat(ParamID::Scattering, "Scattering", 0.0f, 1.0f, 0.5f, 1.0f, "%");
        addFloat(ParamID::ERCrossover, "ER Crossover", 10.0f, 100.0f, 40.0f, 1.0f, "ms");
        addFloat(ParamID::LateDensity, "Late Density", 0.0f, 1.0f, 0.7f, 1.0f, "%");
        addFloat(ParamID::Asymmetry, "Asymmetry", 0.0f, 1.0f, 0.3f, 1.0f, "%");
        addFloat(ParamID::Clarity, "Clarity", -6.0f, 6.0f, 0.0f, 1.0f, "dB");
        addFloat(ParamID::AirAbsorb, "Air Absorb", 0.2f, 2.5f, 1.0f, 1.0f, "x");

        // ── タブ切替トグル ──
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::RT60Tab, "RT60 Tab", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            ParamID::ProTab, "Pro Tab", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));

        return { params.begin(), params.end() };
    }

} // namespace FDNReverb