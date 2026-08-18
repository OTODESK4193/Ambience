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
    p.decayScale = 1.0f;
    p.hfDamping = 0.5f;
    p.diffusion = 1.0f; // Diffusion MAXでリングノイズが出やすいか
    p.wetDB = 0.0f;
    
    engine.prepare(48000.0, 512);
    engine.setParams(p);
    
    const int numSamples = 48000 * 2; // 2 seconds
    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    std::vector<float> outL(numSamples, 0.0f);
    std::vector<float> outR(numSamples, 0.0f);
    
    // 入力: 0.5秒間サイン波を鳴らす
    for (int i = 0; i < 24000; ++i) {
        float env = 1.0f;
        if (i < 480) env = i / 480.0f; // Attack
        if (i > 24000 - 480) env = (24000 - i) / 480.0f; // Release
        inL[i] = inR[i] = env * std::sin(2.0f * 3.1415926535f * freqHz * i / 48000.0f);
    }
    
    // 512サンプルずつ処理
    for (int n = 0; n < numSamples; n += 512) {
        int processLength = std::min(512, numSamples - n);
        engine.processBlock(&inL[n], &inR[n], &outL[n], &outR[n], processLength);
    }
    
    // サイン波停止後（1秒〜2秒の間）のリバーブテイルを分析
    // ゼロクロス周期から、どのような周波数が支配的になっているか簡易分析
    int zeroCrossings = 0;
    for (int i = 48000; i < numSamples - 1; ++i) {
        if ((outL[i] >= 0.0f && outL[i+1] < 0.0f) || (outL[i] < 0.0f && outL[i+1] >= 0.0f)) {
            zeroCrossings++;
        }
    }
    
    float dominantFreq = (zeroCrossings / 2.0f);
    
    // RMS
    float energy = 0;
    for (int i = 48000; i < numSamples; ++i) energy += outL[i] * outL[i];
    float tailRmsDB = 20.0f * std::log10(std::sqrt(energy / 48000.0f) + 1e-9f);
    
    std::cout << "--- Resonance Analysis: " << noteName << " (" << freqHz << " Hz) ---\n";
    std::cout << "Tail RMS Level: " << tailRmsDB << " dB\n";
    std::cout << "Dominant Freq in Tail: " << dominantFreq << " Hz\n";
    std::cout << "Deviation from Input: " << std::abs(dominantFreq - freqHz) << " Hz\n\n";
}

int main() {
    analyzeResonance(1108.73f, "C#6");
    analyzeResonance(1174.66f, "D6");
    return 0;
}