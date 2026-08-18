#include "DSPParams.h"
#include "UniversalEngine.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace FDNReverb;

void analyzeModNoiseSweep() {
    UniversalEngine engine;
    DSPParams p;
    p.algorithmIndex = 0; // Room1
    p.decayScale = 1.0f;
    p.hfDamping = 0.5f;
    p.diffusion = 1.0f;
    p.wetDB = 0.0f;
    p.modRate = 2.0f;
    
    std::cout << "--- ModAmt FM Noise Analysis (High Freq RMS) ---\n";
    for (float modAmt : {0.0f, 1.0f}) {
        p.modAmount = modAmt;
        engine.prepare(48000.0, 512);
        engine.setParams(p);
        
        std::vector<float> inL(48000, 0.0f);
        std::vector<float> inR(48000, 0.0f);
        std::vector<float> outL(48000, 0.0f);
        std::vector<float> outR(48000, 0.0f);
        
        for (int i = 0; i < 48000; ++i) {
            inL[i] = inR[i] = std::sin(2.0f * 3.1415926535f * 1000.0f * i / 48000.0f);
        }
        
        for (int n = 0; n < 48000; n += 512) {
            engine.processBlock(&inL[n], &inR[n], &outL[n], &outR[n], 512);
        }
        
        float hfEnergy = 0.0f;
        float prev = 0.0f;
        for (int i = 24000; i < 48000; ++i) { // 後半0.5秒
            float diff = outL[i] - prev; // 簡易1次HPF (高周波ノイズ成分を抽出)
            hfEnergy += diff * diff;
            prev = outL[i];
        }
        float hfRMS = 20.0f * std::log10(std::sqrt(hfEnergy / 24000.0f) + 1e-9f);
        std::cout << "ModAmt=" << modAmt << " -> High-Freq Aliasing Noise: " << hfRMS << " dB\n";
    }
}

int main() {
    analyzeModNoiseSweep();
    return 0;
}