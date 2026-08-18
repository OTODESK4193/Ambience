#include "DSPParams.h"
#include "UniversalEngine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace FDNReverb;

void analyzeResonance(float freqHz, const std::string& noteName) {
    UniversalEngine engine;
    DSPParams p;
    p.algorithmIndex = 0; // Room1
    p.decayScale = 1.0f; // ★ 通常のDecay
    p.hfDamping = 0.5f;  // ★ 通常のダンピング
    p.diffusion = 0.5f;
    p.wetDB = 0.0f;
    
    engine.prepare(48000.0, 512);
    engine.setParams(p);
    
    const int numSamples = 48000 * 2; // 2 seconds
    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    std::vector<float> outL(numSamples, 0.0f);
    std::vector<float> outR(numSamples, 0.0f);
    
    for (int i = 0; i < 24000; ++i) {
        float env = 1.0f;
        if (i < 480) env = i / 480.0f;
        if (i > 24000 - 480) env = (24000 - i) / 480.0f;
        inL[i] = inR[i] = env * std::sin(2.0f * 3.1415926535f * freqHz * i / 48000.0f);
    }
    
    for (int n = 0; n < numSamples; n += 512) {
        int processLength = std::min(512, numSamples - n);
        engine.processBlock(&inL[n], &inR[n], &outL[n], &outR[n], processLength);
    }
    
    // 0.5秒〜2.0秒のテイル
    float energy = 0;
    for (int i = 24000; i < numSamples; ++i) energy += outL[i] * outL[i];
    float tailRmsDB = 20.0f * std::log10(std::sqrt(energy / (48000.0f * 1.5f)) + 1e-9f);
    
    std::cout << "--- Realistic Test: " << noteName << " (" << freqHz << " Hz) ---\n";
    std::cout << "Tail RMS Level (0.5s-2s): " << tailRmsDB << " dB\n\n";
}

int main() {
    analyzeResonance(1108.73f, "C#6");
    analyzeResonance(1174.66f, "D6");
    return 0;
}