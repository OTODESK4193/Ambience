#include "DSPParams.h"
#include "UniversalEngine.h"
#include <iostream>
#include <vector>
#include <cmath>

using namespace FDNReverb;

void analyzeModNoise() {
    UniversalEngine engine;
    DSPParams p;
    p.algorithmIndex = 0; // Room1
    p.decayScale = 1.0f;
    p.modRate = 2.0f;
    p.erLevel = 0.0f;
    p.wetDB = 0.0f;
    
    std::cout << "--- ModAmt Noise Analysis (1kHz Sine) ---\n";
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
        
        engine.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), 48000);
        
        // 高周波成分(>5kHz)のRMSを簡易計算
        float hfEnergy = 0.0f;
        float prev = 0.0f;
        for (int i = 0; i < 48000; ++i) {
            float diff = outL[i] - prev; // 簡易的な1次HPF
            hfEnergy += diff * diff;
            prev = outL[i];
        }
        float hfRMS = 20.0f * std::log10(std::sqrt(hfEnergy / 48000.0f) + 1e-9f);
        std::cout << "ModAmt=" << modAmt << " -> High-Freq Energy Level: " << hfRMS << " dB\n";
    }
}

void analyzeDiffusion() {
    UniversalEngine engine;
    DSPParams p;
    p.algorithmIndex = 0;
    p.decayScale = 1.0f;
    p.erLevel = 0.0f; // Lateのみ
    p.wetDB = 0.0f;
    
    std::cout << "\n--- Diffusion Density Analysis (Impulse) ---\n";
    for (float diffVal : {0.0f, 1.0f}) {
        p.diffusion = diffVal;
        engine.prepare(48000.0, 512);
        engine.setParams(p);
        
        std::vector<float> inL(48000, 0.0f);
        std::vector<float> inR(48000, 0.0f);
        std::vector<float> outL(48000, 0.0f);
        std::vector<float> outR(48000, 0.0f);
        
        inL[0] = inR[0] = 1.0f; // インパルス
        
        engine.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), 48000);
        
        // 最初の100ms(4800サンプル)のゼロ交差数をカウント(密度の指標)
        int zeroCrossings = 0;
        for (int i = 1; i < 4800; ++i) {
            if ((outL[i-1] >= 0.0f && outL[i] < 0.0f) || (outL[i-1] < 0.0f && outL[i] >= 0.0f)) {
                zeroCrossings++;
            }
        }
        std::cout << "Diffusion=" << diffVal << " -> Zero Crossings in first 100ms: " << zeroCrossings << "\n";
    }
}

int main() {
    analyzeModNoise();
    analyzeDiffusion();
    return 0;
}