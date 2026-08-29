#include <JuceHeader.h>
#include "DSP/UniversalEngine.h"
#include "DSP/DSPParams.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <chrono>

using namespace FDNReverb;

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; 

double getSine(double t) { return std::sin(2.0 * PI * 440.0 * t); }
double getSaw(double t) { return 2.0 * (std::fmod(440.0 * t, 1.0)) - 1.0; }
double getSquare(double t) { return (std::sin(2.0 * PI * 440.0 * t) > 0) ? 1.0 : -1.0; }
double getSync(double t) { 
    double masterPhase = std::fmod(440.0 * t, 1.0);
    return 2.0 * std::fmod(1200.0 * masterPhase / 440.0, 1.0) - 1.0; 
}
double getFM(double t) { return std::sin(2.0 * PI * 440.0 * t + 5.0 * std::sin(2.0 * PI * 880.0 * t)); }

double getEnv(int n) {
    if (n > FS) return 0.0;
    if (n < 480) return (double)n / 480.0; 
    if (n > FS - 480) return (double)(FS - n) / 480.0; 
    return 1.0;
}

void writeRaw(const std::string& name, const std::vector<float>& data) {
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
}

void runEngineTest(UniversalEngine& engine, const std::vector<float>& in, std::vector<float>& out) {
    out.assign(NUM_SAMPLES, 0.0f);
    std::vector<float> inR(NUM_SAMPLES, 0.0f);
    std::vector<float> outR(NUM_SAMPLES, 0.0f);
    for (int i = 0; i < NUM_SAMPLES; ++i) inR[i] = in[i];

    int blockSize = 256;
    for (int i = 0; i < NUM_SAMPLES; i += blockSize) {
        int samplesToProcess = std::min(blockSize, NUM_SAMPLES - i);
        engine.processBlock(&in[i], &inR[i], &out[i], &outR[i], samplesToProcess);
    }
}

int main() {
    juce::MessageManager::getInstance();

    UniversalEngine engineNew;
    engineNew.prepare(48000.0, 256);

    DSPParams baseParams;
    baseParams.algorithmIndex = 0;
    baseParams.decayScale = 1.0f;
    baseParams.diffusion = 0.70f;
    baseParams.modAmount = 0.25f;
    baseParams.modRate = 0.5f;
    baseParams.wetDB = 0.0f;
    baseParams.dryDB = -100.0f;
    baseParams.preDelayMs = 0.0f;
    baseParams.roomSizeScale = 0.0f;
    baseParams.loCutHz = 20.0f;
    baseParams.hiCutHz = 20000.0f;

    std::vector<std::string> waveNames = {"Sine", "Saw", "Square", "Sync", "FM"};
    std::vector<std::vector<float>> inputs(5, std::vector<float>(NUM_SAMPLES));
    for(int n=0; n<NUM_SAMPLES; ++n) {
        double t = (double)n / FS;
        double env = getEnv(n) * 0.5;
        inputs[0][n] = static_cast<float>(getSine(t) * env);
        inputs[1][n] = static_cast<float>(getSaw(t) * env);
        inputs[2][n] = static_cast<float>(getSquare(t) * env);
        inputs[3][n] = static_cast<float>(getSync(t) * env);
        inputs[4][n] = static_cast<float>(getFM(t) * env);
    }

    std::vector<float> out(NUM_SAMPLES);

    // CPU Measurement for New Engine
    engineNew.reset(); engineNew.setParams(baseParams);
    auto startCpu = std::chrono::high_resolution_clock::now();
    runEngineTest(engineNew, inputs[0], out);
    auto endCpu = std::chrono::high_resolution_clock::now();
    double cpuMs = std::chrono::duration<double, std::milli>(endCpu - startCpu).count();
    
    std::ofstream cpuFile("cpu_new.txt");
    cpuFile << cpuMs << "\n";
    cpuFile.close();

    // Process New V1.2.1 for all 5 waves under Base, Diff0, Diff1, Mod0, Mod1
    for (int w = 0; w < 5; ++w) {
        const auto& wName = waveNames[w];
        const auto& in = inputs[w];

        // Base
        engineNew.reset(); engineNew.setParams(baseParams);
        runEngineTest(engineNew, in, out); writeRaw("new_" + wName + "_base.raw", out);

        // Diff 0
        DSPParams pD0 = baseParams; pD0.diffusion = 0.0f;
        engineNew.reset(); engineNew.setParams(pD0);
        runEngineTest(engineNew, in, out); writeRaw("new_" + wName + "_diff0.raw", out);

        // Diff 1
        DSPParams pD1 = baseParams; pD1.diffusion = 1.0f;
        engineNew.reset(); engineNew.setParams(pD1);
        runEngineTest(engineNew, in, out); writeRaw("new_" + wName + "_diff1.raw", out);

        // Mod 0
        DSPParams pM0 = baseParams; pM0.modAmount = 0.0f;
        engineNew.reset(); engineNew.setParams(pM0);
        runEngineTest(engineNew, in, out); writeRaw("new_" + wName + "_mod0.raw", out);

        // Mod 1
        DSPParams pM1 = baseParams; pM1.modAmount = 1.0f;
        engineNew.reset(); engineNew.setParams(pM1);
        runEngineTest(engineNew, in, out); writeRaw("new_" + wName + "_mod1.raw", out);
    }

    std::cout << "New Engine test finished. CPU: " << cpuMs << " ms\n";
    return 0;
}
