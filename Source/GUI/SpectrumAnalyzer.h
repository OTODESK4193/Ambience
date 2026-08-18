#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

class SpectrumAnalyzer : public juce::Component, private juce::Timer {
public:
    SpectrumAnalyzer() : forwardFFT(fftOrder), window(fftSize, juce::dsp::WindowingFunction<float>::hann) {
        setOpaque(false);
        startTimerHz(30);
    }
    
    void pushBuffer(const float* dryL, const float* wetL, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            pushSample(dryL[i], wetL[i]);
        }
    }
    
    void pushSample(float dry, float wet) {
        if (fifoIndex < fftSize) {
            fifoDry[fifoIndex] = dry;
            fifoWet[fifoIndex] = wet;
            fifoIndex++;
            if (fifoIndex == fftSize) {
                if (!nextFFTBlockReady) {
                    std::copy(fifoDry.begin(), fifoDry.end(), fftDataDry.begin());
                    std::copy(fifoWet.begin(), fifoWet.end(), fftDataWet.begin());
                    nextFFTBlockReady = true;
                }
                fifoIndex = 0;
            }
        }
    }
    
    void paint(juce::Graphics& g) override {
        // Draw background or grid here if needed (e.g. EQ area bg)
        g.setColour(juce::Colour(0xFF1E1E1E));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
        
        drawSpectrum(g, scopeDataDry, juce::Colour(0x66FFFFFF), 1.0f); // Gray for Dry
        drawSpectrum(g, scopeDataWet, juce::Colour(0x994090FF), 1.5f); // Blue for Wet
    }
    
    void drawSpectrum(juce::Graphics& g, const std::array<float, 1024>& scopeData, juce::Colour c, float thickness) {
        auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        juce::Path p;
        p.startNewSubPath(bounds.getX(), bounds.getBottom());
        
        float mindB = -100.0f;
        float maxdB = 0.0f;
        
        for (int i = 0; i < 1024; ++i) {
            float mappedX = juce::mapToLog10((float)i / 1024.0f, 0.001f, 1.0f);
            float x = bounds.getX() + mappedX * bounds.getWidth();
            float y = juce::jmap(scopeData[i], mindB, maxdB, bounds.getBottom(), bounds.getY());
            y = juce::jlimit(bounds.getY(), bounds.getBottom(), y);
            if (i == 0) p.startNewSubPath(x, y);
            else p.lineTo(x, y);
        }
        
        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(thickness));
    }
    
    void timerCallback() override {
        if (nextFFTBlockReady) {
            window.multiplyWithWindowingTable(fftDataDry.data(), fftSize);
            window.multiplyWithWindowingTable(fftDataWet.data(), fftSize);
            
            forwardFFT.performFrequencyOnlyForwardTransform(fftDataDry.data());
            forwardFFT.performFrequencyOnlyForwardTransform(fftDataWet.data());
            
            auto mindB = -100.0f;
            auto maxdB = 0.0f;
            
            for (int i = 0; i < 1024; ++i) {
                float dbDry = juce::Decibels::gainToDecibels(fftDataDry[i]) - juce::Decibels::gainToDecibels((float)fftSize);
                float dbWet = juce::Decibels::gainToDecibels(fftDataWet[i]) - juce::Decibels::gainToDecibels((float)fftSize);
                
                // Smoothing
                scopeDataDry[i] = scopeDataDry[i] * 0.7f + dbDry * 0.3f;
                scopeDataWet[i] = scopeDataWet[i] * 0.7f + dbWet * 0.3f;
            }
            
            nextFFTBlockReady = false;
            repaint();
        }
    }
    
private:
    static constexpr int fftOrder = 11; // 2048
    static constexpr int fftSize = 1 << fftOrder;
    
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;
    
    std::array<float, fftSize> fifoDry {0};
    std::array<float, fftSize> fifoWet {0};
    std::array<float, fftSize*2> fftDataDry {0};
    std::array<float, fftSize*2> fftDataWet {0};
    std::array<float, fftSize/2> scopeDataDry {-100.0f};
    std::array<float, fftSize/2> scopeDataWet {-100.0f};
    
    int fifoIndex {0};
    std::atomic<bool> nextFFTBlockReady {false};
};