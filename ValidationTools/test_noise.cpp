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

void verifyDucking() {
    std::cout << "\n=== Ducking Invariance & Linearity Verification ===\n";
    UniversalEngine engine1, engine2;
    engine1.prepare(48000.0, 256);
    engine2.prepare(48000.0, 256);

    DSPParams p1, p2;
    p1.algorithmIndex = 0; p2.algorithmIndex = 0;
    p1.duckingAmount = 0.0f; p2.duckingAmount = 0.0f;
    p1.duckingThreshDB = -60.0f; // Extreme min
    p2.duckingThreshDB = 0.0f;   // Extreme max

    engine1.setParams(p1); engine1.reset();
    engine2.setParams(p2); engine2.reset();

    const int N = 48000;
    std::vector<float> inL(N), inR(N), out1L(N), out1R(N), out2L(N), out2R(N);
    for (int i = 0; i < N; ++i) {
        float s = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * (float)i / 48000.0f);
        inL[i] = s; inR[i] = s;
    }

    for (int i = 0; i < N; i += 256) {
        int chunk = std::min(256, N - i);
        engine1.processBlock(&inL[i], &inR[i], &out1L[i], &out1R[i], chunk);
        engine2.processBlock(&inL[i], &inR[i], &out2L[i], &out2R[i], chunk);
    }

    // Measure maximum absolute difference between out1 and out2
    float maxDiff = 0.0f;
    int firstDiffIdx = -1;
    for (int i = 0; i < N; ++i) {
        float diff = std::abs(out1L[i] - out2L[i]);
        if (diff > 1e-6f && firstDiffIdx < 0) {
            firstDiffIdx = i;
            std::cout << "First diff at sample " << i << ": out1=" << out1L[i] << ", out2=" << out2L[i] << "\n";
        }
        maxDiff = std::max(maxDiff, diff);
    }
    std::cout << "1. DuckingAmt=0 THRESH Invariance: Max Difference = " << maxDiff
              << " (" << (maxDiff == 0.0f ? "PERFECT 100% BIT-EXACT MATCH" : "FAILED") << ")\n";

    // 2. Ducking Linearity & Clean Reduction test (DuckingAmount = 12dB)
    UniversalEngine engineDuck;
    engineDuck.prepare(48000.0, 256);
    DSPParams pDuck;
    pDuck.algorithmIndex = 0;
    pDuck.duckingAmount = 12.0f; // -12dB reduction
    pDuck.duckingThreshDB = -20.0f;
    pDuck.duckingAttackMs = 10.0f;
    pDuck.duckingRelMs = 200.0f;
    engineDuck.setParams(pDuck); engineDuck.reset();

    std::vector<float> outDL(N), outDR(N);
    for (int i = 0; i < N; i += 256) {
        int chunk = std::min(256, N - i);
        engineDuck.processBlock(&inL[i], &inR[i], &outDL[i], &outDR[i], chunk);
    }

    // Compare late tail RMS (steady state under ducking vs no ducking)
    double sumSqNoDuck = 0.0, sumSqDuck = 0.0;
    for (int i = N/2; i < N; ++i) {
        sumSqNoDuck += out1L[i] * out1L[i];
        sumSqDuck += outDL[i] * outDL[i];
    }
    double rmsNoDuck = std::sqrt(sumSqNoDuck / (N/2));
    double rmsDuck = std::sqrt(sumSqDuck / (N/2));
    double duckDb = 20.0 * std::log10(rmsDuck / (rmsNoDuck + 1e-12));

    std::cout << "2. Clean Ducking Reduction: " << duckDb << " dB (Target ~ -12dB)\n";
}

int main() {
    runContinuousSine(0.0f, 0.0f, 60.0f, "sine_mod0_decay60.bin");
    runContinuousSine(1.0f, 1.0f, 60.0f, "sine_mod1_decay60.bin");
    runContinuousSine(1.0f, 1.0f, 1.5f, "sine_mod1_decay1.5.bin");
    verifyDucking();
    return 0;
}
