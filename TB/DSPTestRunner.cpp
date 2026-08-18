#include "DSPParams.h"
#include "UniversalEngine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>

using namespace FDNReverb;

void analyzeRinging(float freqHz, const std::string& noteName) {
    UniversalEngine engine;
    DSPParams p;
    p.algorithmIndex = 0; // Room1
    p.decayScale = 5.0f;
    p.hfDamping = 0.1f;
    p.diffusion = 1.0f;
    p.wetDB = 0.0f;
    p.modAmount = 0.5f;
    p.modRate = 2.0f;
    
    engine.prepare(48000.0, 512);
    engine.setParams(p);
    
    const int numSamples = 48000 * 2;
    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    std::vector<float> outL(numSamples, 0.0f);
    std::vector<float> outR(numSamples, 0.0f);
    
    for (int i = 0; i < 9600; ++i) {
        float env = 1.0f;
        if (i < 480) env = i / 480.0f;
        if (i > 9600 - 480) env = (9600 - i) / 480.0f;
        inL[i] = inR[i] = env * std::sin(2.0f * 3.1415926535f * freqHz * i / 48000.0f);
    }
    
    for (int n = 0; n < numSamples; n += 512) {
        int processLength = std::min(512, numSamples - n);
        engine.processBlock(&inL[n], &inR[n], &outL[n], &outR[n], processLength);
    }
    
    int startIdx = 24000;
    int analyzeLen = 48000;
    
    float totalEnergy = 0.0f;
    float peakEnergy = 0.0f;
    float peakFreq = 0.0f;
    
    for (float f = freqHz - 200.0f; f <= freqHz + 200.0f; f += 2.0f) {
        float re = 0.0f;
        float im = 0.0f;
        for (int i = 0; i < analyzeLen; i += 4) {
            float val = outL[startIdx + i];
            totalEnergy += val * val;
            float phase = 2.0f * 3.1415926535f * f * i / 48000.0f;
            re += val * std::cos(phase);
            im -= val * std::sin(phase);
        }
        float binEnergy = (re * re + im * im) / analyzeLen;
        if (binEnergy > peakEnergy) {
            peakEnergy = binEnergy;
            peakFreq = f;
        }
    }
    
    totalEnergy /= (analyzeLen / 4.0f);
    float peakRatioDB = 10.0f * std::log10((peakEnergy + 1e-12f) / (totalEnergy + 1e-12f));
    
    std::cout << "Note: " << noteName << " (" << freqHz << " Hz)\n";
    std::cout << "Peak Frequency: " << peakFreq << " Hz\n";
    std::cout << "Ringing Sharpness (Peak/Avg Ratio): " << std::fixed << std::setprecision(2) << peakRatioDB << " dB\n\n";
}

int main() {
    std::cout << "=== AFTER AGGRESSIVE FIX (Valhalla Logic) ===\n";
    analyzeRinging(1108.73f, "C#6");
    analyzeRinging(1174.66f, "D6");
    return 0;
}