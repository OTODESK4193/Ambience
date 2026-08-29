#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "AmbienceUI.h"
#include <vector>

class DecayCurveViz : public juce::Component, private juce::Timer {
public:
    DecayCurveViz();
    ~DecayCurveViz() override;

    void setProcessor(FDNReverbAudioProcessor* p) noexcept { processor = p; }
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    struct AmbientParticle {
        float x{ 0.0f };
        float y{ 0.0f };
        float vx{ 0.0f };
        float vy{ 0.0f };
        float alpha{ 0.0f };
        float baseAlpha{ 0.0f };
        float size{ 1.0f };
        float phase{ 0.0f };
    };

    void initParticles();
    void updateParticles();

    FDNReverbAudioProcessor* processor{ nullptr };

    float cachedRT60Mid{ 1.0f };
    int   cachedERTapCount{ 0 };
    bool  cachedERBypassed{ false };

    static constexpr int MAX_DISPLAY_TAPS = 12;
    std::array<float, MAX_DISPLAY_TAPS> cachedERDelayMs;
    std::array<float, MAX_DISPLAY_TAPS> cachedERGains;

    std::vector<AmbientParticle> particles;
    float globalPhase{ 0.0f };

    static constexpr float splitSec = 0.20f;
    static constexpr float splitRatio = 0.30f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DecayCurveViz)
};
