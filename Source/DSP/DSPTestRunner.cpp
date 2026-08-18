#include <JuceHeader.h>
#include "UniversalEngine.h"
#include "DSPTestHarness.h"
#include "../PluginParameters.h"
#include "../AlgorithmPresets.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>

using namespace FDNReverb;
using namespace FDNReverb::TestHarness;

struct TestResult {
    bool passed;
    std::string name;
    std::string message;
};

std::vector<TestResult> results;

void logResult(bool passed, const std::string& name, const std::string& msg) {
    results.push_back({passed, name, msg});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << " | " << msg << "\n";
}

void processTest(UniversalEngine& engine, const std::vector<float>& inBuffer, std::vector<float>& outL, std::vector<float>& outR) {
    outL.resize(inBuffer.size(), 0.0f);
    outR.resize(inBuffer.size(), 0.0f);
    
    int blockSize = 256;
    for (size_t i = 0; i < inBuffer.size(); i += blockSize) {
        int samplesToProcess = std::min(blockSize, static_cast<int>(inBuffer.size() - i));
        std::vector<float> inRBuffer(samplesToProcess, 0.0f); // duplicate left to right
        for(int j=0; j<samplesToProcess; ++j) inRBuffer[j] = inBuffer[i+j];
        
        engine.processBlock(&inBuffer[i], inRBuffer.data(), &outL[i], &outR[i], samplesToProcess);
    }
}

int main() {
    double sampleRate = 48000.0;
    int maxBlockSize = 256;
    
    std::cout << "=============================================\n";
    std::cout << " Ambience VST3 DSP Test Harness\n";
    std::cout << "=============================================\n\n";

    // 1. Impulse Response Test (RT60 verification)
    {
        UniversalEngine engine;
        engine.prepare(sampleRate, maxBlockSize);
        DSPParams params;
        params.wetDB = 0.0f;
        params.dryDB = -100.0f;
        params.decayScale = 1.0f;
        engine.setParams(params);
        
        auto impulse = SignalGenerator::generateImpulse(static_cast<size_t>(sampleRate * 5.0)); // 5s
        std::vector<float> outL, outR;
        processTest(engine, impulse, outL, outR);
        
        float rt60 = Analyzer::measureRT60(outL, sampleRate);
        bool passed = rt60 > 0.1f && rt60 < 5.0f;
        std::stringstream ss;
        ss << "RT60=" << rt60 << "s";
        logResult(passed, "Impulse Response RT60 Test", ss.str());
    }

    // 2. Sine THD Measurement
    {
        UniversalEngine engine;
        engine.prepare(sampleRate, maxBlockSize);
        DSPParams params;
        params.wetDB = 0.0f;
        params.dryDB = -100.0f;
        params.saturation = 0.0f; // clean
        engine.setParams(params);
        
        std::vector<double> freqs = {100.0, 1000.0, 10000.0};
        for (double freq : freqs) {
            auto sine = SignalGenerator::generateSine(freq, sampleRate, 65536);
            std::vector<float> outL, outR;
            processTest(engine, sine, outL, outR);
            
            auto thdRes = Analyzer::measureTHD(outL, freq, sampleRate);
            bool passed = thdRes.thd < 1.0f; // THD < 1%
            std::stringstream ss;
            ss << freq << "Hz THD=" << thdRes.thd << "% THD+N=" << thdRes.thd_n << "%";
            logResult(passed, "Sine THD Test", ss.str());
        }
    }

    // 3. Noise Floor Test
    {
        UniversalEngine engine;
        engine.prepare(sampleRate, maxBlockSize);
        DSPParams params;
        params.wetDB = 0.0f;
        params.dryDB = -100.0f;
        engine.setParams(params);
        
        auto silence = std::vector<float>(static_cast<size_t>(sampleRate * 10.0), 0.0f);
        // Ping with impulse first, then measure tail
        silence[0] = 1.0f; 
        std::vector<float> outL, outR;
        processTest(engine, silence, outL, outR);
        
        // Measure last 1 second
        std::vector<float> tail(outL.end() - static_cast<size_t>(sampleRate), outL.end());
        float noiseFloor = Analyzer::measureNoiseFloor(tail);
        
        bool passed = noiseFloor < -80.0f;
        std::stringstream ss;
        ss << "Tail Noise Floor=" << noiseFloor << " dBFS";
        logResult(passed, "Noise Floor Test", ss.str());
    }

    // 4. DC Offset Test
    {
        UniversalEngine engine;
        engine.prepare(sampleRate, maxBlockSize);
        DSPParams params;
        engine.setParams(params);
        
        auto sine = SignalGenerator::generateSine(100.0, sampleRate, 65536);
        std::vector<float> outL, outR;
        processTest(engine, sine, outL, outR);
        
        float dcOffset = Analyzer::getDCOffset(outL);
        bool passed = std::abs(dcOffset) < 0.001f;
        std::stringstream ss;
        ss << "DC Offset=" << dcOffset;
        logResult(passed, "DC Offset Test", ss.str());
    }

    // 5. Clipping Test
    {
        UniversalEngine engine;
        engine.prepare(sampleRate, maxBlockSize);
        DSPParams params;
        params.wetDB = 0.0f;
        params.dryDB = 0.0f;
        engine.setParams(params);
        
        auto sine = SignalGenerator::generateSine(1000.0, sampleRate, 65536);
        std::vector<float> outL, outR;
        processTest(engine, sine, outL, outR);
        
        bool clipped = Analyzer::detectClipping(outL);
        float peak = Analyzer::getPeak(outL);
        bool passed = !clipped && peak <= 0.0f;
        std::stringstream ss;
        ss << "Clipped=" << (clipped ? "YES" : "NO") << " Peak=" << peak << " dBFS";
        logResult(passed, "Clipping Test", ss.str());
    }

    // 6. Metallic Resonance Test (All algorithms x DecayTimes)
    {
        std::vector<float> decayTimes = {0.5f, 2.0f, 5.0f, 10.0f};
        
        for (int algIdx = 0; algIdx < NUM_ALGORITHMS; ++algIdx) {
            for (float decay : decayTimes) {
                UniversalEngine engine;
                engine.prepare(sampleRate, maxBlockSize);
                DSPParams params;
                params.algorithmIndex = algIdx;
                params.decayScale = decay / PRESET_DEFAULTS[algIdx].decayTime;
                params.wetDB = 0.0f;
                params.dryDB = -100.0f;
                engine.setParams(params);
                
                auto impulse = SignalGenerator::generateImpulse(65536);
                std::vector<float> outL, outR;
                processTest(engine, impulse, outL, outR);
                
                auto resonances = Analyzer::detectMetallicResonance(outL, sampleRate);
                
                std::stringstream ss;
                ss << "Alg=" << ALL_PRESETS[algIdx]->name << " Decay=" << decay << "s -> ";
                if (resonances.empty()) {
                    ss << "Clean";
                } else {
                    ss << "Resonances: ";
                    for (const auto& r : resonances) {
                        ss << r.frequency << "Hz (" << r.magnitude << "dB) ";
                    }
                }
                
                // Allow some resonances for Plate/Spring/Goldfoil
                bool allowed = algIdx >= 4 || resonances.empty() || resonances[0].magnitude < 15.0f;
                logResult(allowed, "Metallic Resonance Test", ss.str());
            }
        }
    }

    // 7. Parameter Sweep Test (DecayTime sweep checking for clicks/pops)
    {
        UniversalEngine engine;
        engine.prepare(sampleRate, maxBlockSize);
        DSPParams params;
        params.wetDB = 0.0f;
        engine.setParams(params);
        
        auto sine = SignalGenerator::generateSine(440.0, sampleRate, 65536);
        std::vector<float> outL(sine.size(), 0.0f), outR(sine.size(), 0.0f);
        
        int blockSize = 256;
        for (size_t i = 0; i < sine.size(); i += blockSize) {
            // Sweep decay scale 0.1 to 2.0
            params.decayScale = 0.1f + 1.9f * (static_cast<float>(i) / sine.size());
            engine.setParams(params);
            
            int samplesToProcess = std::min(blockSize, static_cast<int>(sine.size() - i));
            std::vector<float> inRBuffer(samplesToProcess, 0.0f);
            for(int j=0; j<samplesToProcess; ++j) inRBuffer[j] = sine[i+j];
            engine.processBlock(&sine[i], inRBuffer.data(), &outL[i], &outR[i], samplesToProcess);
        }
        
        // Find clicks by calculating sample-to-sample difference
        bool clicked = false;
        float maxDiff = 0.0f;
        for(size_t i=1; i<outL.size(); ++i) {
            float diff = std::abs(outL[i] - outL[i-1]);
            if (diff > maxDiff) maxDiff = diff;
            if (diff > 0.5f) clicked = true; // Huge discontinuity
        }
        
        bool passed = !clicked;
        std::stringstream ss;
        ss << "Max delta=" << maxDiff << (clicked ? " (CLICKS DETECTED)" : " (Clean transition)");
        logResult(passed, "Parameter Sweep Test (DecayTime)", ss.str());
    }

    // 8. Comprehensive Waveform Test
    {
        UniversalEngine engine;
        engine.prepare(sampleRate, maxBlockSize);
        DSPParams params;
        engine.setParams(params);
        
        struct WaveDef { std::string name; std::vector<float> data; };
        std::vector<WaveDef> waves = {
            {"Sine", SignalGenerator::generateSine(440.0, sampleRate, 65536)},
            {"Triangle", SignalGenerator::generateTriangle(440.0, sampleRate, 65536)},
            {"Square", SignalGenerator::generateSquare(440.0, sampleRate, 65536)},
            {"Sawtooth", SignalGenerator::generateSawtooth(440.0, sampleRate, 65536)},
            {"WhiteNoise", SignalGenerator::generateWhiteNoise(65536)},
            {"PinkNoise", SignalGenerator::generatePinkNoise(65536)}
        };
        
        for (const auto& w : waves) {
            std::vector<float> outL, outR;
            processTest(engine, w.data, outL, outR);
            float rms = Analyzer::getRMS(outL);
            float peak = Analyzer::getPeak(outL);
            
            bool passed = rms > -80.0f && peak <= 0.0f;
            std::stringstream ss;
            ss << "RMS=" << rms << "dBFS, Peak=" << peak << "dBFS";
            logResult(passed, "Comprehensive Waveform Test (" + w.name + ")", ss.str());
        }
    }

    std::cout << "\n=============================================\n";
    int passCount = 0;
    for (const auto& r : results) {
        if (r.passed) passCount++;
    }
    std::cout << " TEST SUMMARY: " << passCount << " / " << results.size() << " PASSED\n";
    std::cout << "=============================================\n";

    return 0;
}
