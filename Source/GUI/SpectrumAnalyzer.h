#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "../PluginProcessor.h"

class SpectrumAnalyzer : public juce::Component, private juce::Timer {
public:
    SpectrumAnalyzer(FDNReverbAudioProcessor& p) : processor(p), forwardFFT(fftOrder), window(fftSize, juce::dsp::WindowingFunction<float>::hann) {
        fftDataDry.fill(0.0f);
        fftDataWet.fill(0.0f);
        scopeDataDry.fill(-100.0f);
        scopeDataWet.fill(-100.0f);
        setOpaque(false);
        startTimerHz(30);
    }
    
    ~SpectrumAnalyzer() override {
        stopTimer();
    }
    
    void paint(juce::Graphics& g) override {
        drawSpectrum(g, scopeDataDry, juce::Colour(0x88FFFFFF), 1.5f); // Gray for Dry
        drawSpectrum(g, scopeDataWet, juce::Colour(0xBB4090FF), 2.0f); // Blue for Wet
    }
    
        void drawSpectrum(juce::Graphics& g, const std::array<float, 1024>& scopeData, juce::Colour c, float thickness) {
        auto bounds = getLocalBounds().toFloat().reduced(4.0f);
        juce::Path p;
        p.startNewSubPath(bounds.getX(), bounds.getBottom());
        
        float mindB = -100.0f;
        float maxdB = 0.0f;
        
        const float sampleRate = 48000.0f; 
        const float minFreq = 30.0f;
        const float maxFreq = 16000.0f;
        
        bool first = true;
        for (int i = 1; i < 1024; ++i) {
            float freq = i * (sampleRate / fftSize);
            if (freq < minFreq) continue;
            if (freq > maxFreq) break;
            
            float mappedX = (std::log10(freq) - std::log10(minFreq)) / (std::log10(maxFreq) - std::log10(minFreq));
            float x = bounds.getX() + mappedX * bounds.getWidth();
            
            float y = juce::jmap(scopeData[i], mindB, maxdB, bounds.getBottom(), bounds.getY());
            y = juce::jlimit(bounds.getY(), bounds.getBottom(), y);
            
            if (first) {
                p.startNewSubPath(x, y);
                first = false;
            } else {
                p.lineTo(x, y);
            }
        }
        
        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(thickness));
    }
    
    void timerCallback() override {
        if (processor.specFifoReady.load(std::memory_order_acquire)) {
            std::copy(processor.specFifoDry.begin(), processor.specFifoDry.end(), fftDataDry.begin());
            std::copy(processor.specFifoWet.begin(), processor.specFifoWet.end(), fftDataWet.begin());
            
            processor.specFifoIndex.store(0, std::memory_order_release);
            processor.specFifoReady.store(false, std::memory_order_release);
            
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
            repaint();
        }
    }
    
private:
    static constexpr int fftOrder = 11; // 2048
    static constexpr int fftSize = 1 << fftOrder;
    
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;
    FDNReverbAudioProcessor& processor;
    
    std::array<float, fftSize*2> fftDataDry;
    std::array<float, fftSize*2> fftDataWet;
    std::array<float, fftSize/2> scopeDataDry;
    std::array<float, fftSize/2> scopeDataWet;
};