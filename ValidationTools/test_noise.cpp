#include <iostream>
#include <vector>
#include <fstream>
#include <cmath>
#include "../Source/DSP/UniversalEngine.h"

using namespace FDNReverb;

void runContinuousSine(float modAmt, float modRate, float decayTimeSec, const std::string& filename) {
    UniversalEngine engine;
    engine.prepare(48000.0, 256);

    DSPParams params;
    params.algorithmIndex = 0; // Room 1
    params.roomSizeScale = 1.0f;
    params.decayScale = decayTimeSec / 1.5f;
    params.erLevel = 1.0f;
    params.lateLevel = 1.0f;
    params.diffusion = 1.0f;
    params.modAmount = modAmt;
    params.modRate = modRate;

    engine.setParams(params);
    engine.reset();

    const int sampleRate = 48000;
    const int totalSamples = sampleRate * 3; // 3 seconds of continuous 1kHz sine
    std::vector<float> inL(totalSamples, 0.0f);
    std::vector<float> inR(totalSamples, 0.0f);

    for (int i = 0; i < totalSamples; ++i) {
        float s = 0.5f * std::sin(2.0f * 3.14159265358979323846f * 1000.0f * (float)i / (float)sampleRate);
        inL[i] = s;
        inR[i] = s;
    }

    std::vector<float> outL(totalSamples, 0.0f);
    std::vector<float> outR(totalSamples, 0.0f);

    const int blockSize = 256;
    for (int i = 0; i < totalSamples; i += blockSize) {
        int chunk = std::min(blockSize, totalSamples - i);
        engine.processBlock(&inL[i], &inR[i], &outL[i], &outR[i], chunk);
    }

    // Dump the last 1 second (48000 samples)
    std::ofstream out(filename, std::ios::binary);
    int start = sampleRate * 2;
    int len = sampleRate;
    out.write(reinterpret_cast<const char*>(&outL[start]), len * sizeof(float));
    out.close();
    std::cout << "Dumped " << len << " samples to " << filename << "\n";
}

int main() {
    // 1. Mod 0.0, Decay 60s
    runContinuousSine(0.0f, 0.0f, 60.0f, "sine_mod0_decay60.bin");
    // 2. Mod 1.0, Rate 1.0, Decay 60s
    runContinuousSine(1.0f, 1.0f, 60.0f, "sine_mod1_decay60.bin");
    // 3. Mod 1.0, Rate 1.0, Decay 1.5s
    runContinuousSine(1.0f, 1.0f, 1.5f, "sine_mod1_decay1.5.bin");
    return 0;
}
