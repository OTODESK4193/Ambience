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

void verifyStereoWidthZero() {
    std::cout << "\n=================================================================\n";
    std::cout << "=== Verification 1: StereoWidth = 0% Bit-Exact Mono (L == R) ===\n";
    std::cout << "=================================================================\n";

    const double sampleRate = 48000.0;
    const int N = 48000; // 1 second test

    std::vector<std::string> topoNames = { "Room", "Hall", "Plate", "Spring", "Goldfoil", "Inchindown" };

    for (int algo = 0; algo < 6; ++algo) {
        std::cout << "\n[Topology " << algo << ": " << topoNames[algo] << "]\n";

        UniversalEngine engine;
        engine.prepare(sampleRate, 256);

        DSPParams params;
        params.algorithmIndex = algo;
        params.stereoWidth = 0.0f; // ★ 完全モノラル要求
        params.roomSizeScale = 1.0f;
        params.erLevel = 1.0f;
        params.lateLevel = 1.0f;
        params.diffusion = 1.0f;
        engine.setParams(params);

        // Test 1: L-only impulse (maximum stereo asymmetry input)
        {
            engine.reset();
            std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
            inL[0] = 1.0f; // R is 0.0f

            std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
            for (int i = 0; i < N; i += 256) {
                int chunk = std::min(256, N - i);
                engine.processBlock(&inL[i], &inR[i], &outL[i], &outR[i], chunk);
            }

            float maxDiff = 0.0f;
            double sumL2 = 0.0, sumR2 = 0.0, sumLR = 0.0;
            for (int i = 0; i < N; ++i) {
                float diff = std::abs(outL[i] - outR[i]);
                if (diff > maxDiff) maxDiff = diff;
                sumL2 += (double)outL[i] * outL[i];
                sumR2 += (double)outR[i] * outR[i];
                sumLR += (double)outL[i] * outR[i];
            }
            double corr = sumLR / (std::sqrt(sumL2 * sumR2) + 1e-30);

            std::cout << "  - Input: L-only Impulse -> Max |L - R| = " << maxDiff
                      << " | Correlation = " << corr
                      << " -> " << (maxDiff == 0.0f ? "PASS (BIT-EXACT L == R)" : "FAIL") << "\n";
        }

        // Test 2: Out-of-phase stereo input (L = +0.5, R = -0.5, 100% Side component)
        {
            engine.reset();
            std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
            for (int i = 0; i < 480; ++i) {
                float s = std::sin(2.0f * 3.14159265f * 440.0f * (float)i / 48000.0f);
                inL[i] = s;
                inR[i] = -s;
            }

            std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
            for (int i = 0; i < N; i += 256) {
                int chunk = std::min(256, N - i);
                engine.processBlock(&inL[i], &inR[i], &outL[i], &outR[i], chunk);
            }

            float maxDiff = 0.0f;
            for (int i = 0; i < N; ++i) {
                float diff = std::abs(outL[i] - outR[i]);
                if (diff > maxDiff) maxDiff = diff;
            }

            std::cout << "  - Input: Out-of-Phase Stereo -> Max |L - R| = " << maxDiff
                      << " -> " << (maxDiff == 0.0f ? "PASS (BIT-EXACT L == R)" : "FAIL") << "\n";
        }

        // Test 3: Stereo Uncorrelated White Noise
        {
            engine.reset();
            std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
            uint32_t rngL = 0x12345678, rngR = 0x87654321;
            for (int i = 0; i < 2400; ++i) {
                rngL = rngL * 1664525u + 1013904223u;
                rngR = rngR * 1664525u + 1013904223u;
                inL[i] = ((float)(rngL & 0xFFFF) / 32768.0f - 1.0f) * 0.5f;
                inR[i] = ((float)(rngR & 0xFFFF) / 32768.0f - 1.0f) * 0.5f;
            }

            std::vector<float> outL(N, 0.0f), outR(N, 0.0f);
            for (int i = 0; i < N; i += 256) {
                int chunk = std::min(256, N - i);
                engine.processBlock(&inL[i], &inR[i], &outL[i], &outR[i], chunk);
            }

            float maxDiff = 0.0f;
            for (int i = 0; i < N; ++i) {
                float diff = std::abs(outL[i] - outR[i]);
                if (diff > maxDiff) maxDiff = diff;
            }

            std::cout << "  - Input: Uncorrelated Noise -> Max |L - R| = " << maxDiff
                      << " -> " << (maxDiff == 0.0f ? "PASS (BIT-EXACT L == R)" : "FAIL") << "\n";
        }
    }
}

void verifyErSoloSendModeOffset() {
    std::cout << "\n=================================================================\n";
    std::cout << "=== Verification 2: ER Solo & Send Mode 0-5ms Comb-Filter Protection ===\n";
    std::cout << "=================================================================\n";

    const double sampleRate = 48000.0;
    const int samples5ms = static_cast<int>(0.005 * sampleRate); // 240 samples = 5.0ms
    const int N = 4800; // 100ms test

    // Test across algorithms with short natural geometry (e.g. Plate=4, Goldfoil=6)
    for (int algo : { 0, 4, 6 }) {
        std::string algoName = (algo == 0 ? "Room" : (algo == 4 ? "Plate" : "Goldfoil"));
        std::cout << "\n[Testing Algorithm: " << algoName << "]\n";

        // Condition 1: Normal Mode (erSolo=false, dryDB=0.0f, preDelay=0ms)
        UniversalEngine engineNormal;
        engineNormal.prepare(sampleRate, 256);
        DSPParams pNorm;
        pNorm.algorithmIndex = algo;
        pNorm.erSolo = false;
        pNorm.dryDB = 0.0f;
        pNorm.preDelayMs = 0.0f;
        pNorm.erLevel = 1.0f;
        pNorm.lateLevel = 0.0f; // ER only to examine reflection taps
        engineNormal.setParams(pNorm); engineNormal.reset();

        std::vector<float> inL(N, 0.0f), inR(N, 0.0f);
        inL[0] = 1.0f; inR[0] = 1.0f;

        std::vector<float> outNormL(N, 0.0f), outNormR(N, 0.0f);
        for (int i = 0; i < N; i += 256) {
            int chunk = std::min(256, N - i);
            engineNormal.processBlock(&inL[i], &inR[i], &outNormL[i], &outNormR[i], chunk);
        }

        double energy0to5ms_Norm = 0.0;
        for (int i = 0; i < samples5ms; ++i) {
            energy0to5ms_Norm += outNormL[i] * outNormL[i] + outNormR[i] * outNormR[i];
        }

        // Condition 2: Send Mode (erSolo=false, dryDB=-60.0f, preDelay=0ms)
        UniversalEngine engineSend;
        engineSend.prepare(sampleRate, 256);
        DSPParams pSend = pNorm;
        pSend.dryDB = -60.0f;
        engineSend.setParams(pSend); engineSend.reset();

        std::vector<float> outSendL(N, 0.0f), outSendR(N, 0.0f);
        for (int i = 0; i < N; i += 256) {
            int chunk = std::min(256, N - i);
            engineSend.processBlock(&inL[i], &inR[i], &outSendL[i], &outSendR[i], chunk);
        }

        double energy0to5ms_Send = 0.0;
        for (int i = 0; i < samples5ms; ++i) {
            energy0to5ms_Send += outSendL[i] * outSendL[i] + outSendR[i] * outSendR[i];
        }

        // Condition 3: ER Solo Mode (erSolo=true, dryDB=0.0f, preDelay=0ms)
        UniversalEngine engineSolo;
        engineSolo.prepare(sampleRate, 256);
        DSPParams pSolo = pNorm;
        pSolo.erSolo = true;
        engineSolo.setParams(pSolo); engineSolo.reset();

        std::vector<float> outSoloL(N, 0.0f), outSoloR(N, 0.0f);
        for (int i = 0; i < N; i += 256) {
            int chunk = std::min(256, N - i);
            engineSolo.processBlock(&inL[i], &inR[i], &outSoloL[i], &outSoloR[i], chunk);
        }

        double energy0to5ms_Solo = 0.0;
        for (int i = 0; i < samples5ms; ++i) {
            energy0to5ms_Solo += outSoloL[i] * outSoloL[i] + outSoloR[i] * outSoloR[i];
        }

        std::cout << "  - Normal Mode   0-5ms Energy: " << energy0to5ms_Norm << "\n";
        std::cout << "  - Send Mode     0-5ms Energy: " << energy0to5ms_Send << "\n";
        std::cout << "  - ER Solo Mode  0-5ms Energy: " << energy0to5ms_Solo << "\n";

        bool sendSuppressed = (energy0to5ms_Send < 1e-5);
        bool soloSuppressed = (energy0to5ms_Solo < 1e-5);
        std::cout << "  - Evaluation: Send Mode Comb-Filter Protected: " << (sendSuppressed ? "YES (PASS)" : "NO (FAIL)") << "\n";
        std::cout << "  - Evaluation: ER Solo Comb-Filter Protected:   " << (soloSuppressed ? "YES (PASS)" : "NO (FAIL)") << "\n";
    }
}

int main() {
    verifyStereoWidthZero();
    verifyErSoloSendModeOffset();
    verifyDucking();
    return 0;
}
