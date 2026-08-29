#include <JuceHeader.h>
#include "../Source/DSP/UniversalEngine.h"
#include "../Source/DSP/DSPParams.h"
#include "UniversalEngine_v121.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <chrono>

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

template<typename EngineT>
void runEngine(EngineT& engine, const std::vector<float>& in, std::vector<float>& out) {
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

    FDNReverb::UniversalEngine engineOld;
    engineOld.prepare(48000.0, 256);

    FDNReverb::V121::UniversalEngineUpdate engineNew;
    engineNew.prepare(48000.0, 256);

    FDNReverb::DSPParams baseParams;
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

    // CPU Measurement
    engineOld.reset(); engineOld.setParams(baseParams);
    auto t0 = std::chrono::high_resolution_clock::now();
    runEngine(engineOld, inputs[0], out);
    auto t1 = std::chrono::high_resolution_clock::now();
    double cpuOld = std::chrono::duration<double, std::milli>(t1 - t0).count();

    engineNew.reset(); engineNew.setParams(baseParams);
    auto t2 = std::chrono::high_resolution_clock::now();
    runEngine(engineNew, inputs[0], out);
    auto t3 = std::chrono::high_resolution_clock::now();
    double cpuNew = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::ofstream cpuFile("cpu_comparison.txt");
    cpuFile << cpuOld << "\n" << cpuNew << "\n";
    cpuFile.close();

    // Run all 5 waves under Base, Diff0, Diff1, Mod0, Mod1 for both Old and New
    for (int w = 0; w < 5; ++w) {
        const auto& wName = waveNames[w];
        const auto& in = inputs[w];

        // 1. Base
        engineOld.reset(); engineOld.setParams(baseParams);
        runEngine(engineOld, in, out); writeRaw("old_" + wName + "_base.raw", out);

        engineNew.reset(); engineNew.setParams(baseParams);
        runEngine(engineNew, in, out); writeRaw("new_" + wName + "_base.raw", out);

        // 2. Diff 0
        FDNReverb::DSPParams pD0 = baseParams; pD0.diffusion = 0.0f;
        engineOld.reset(); engineOld.setParams(pD0);
        runEngine(engineOld, in, out); writeRaw("old_" + wName + "_diff0.raw", out);

        engineNew.reset(); engineNew.setParams(pD0);
        runEngine(engineNew, in, out); writeRaw("new_" + wName + "_diff0.raw", out);

        // 3. Diff 1
        FDNReverb::DSPParams pD1 = baseParams; pD1.diffusion = 1.0f;
        engineOld.reset(); engineOld.setParams(pD1);
        runEngine(engineOld, in, out); writeRaw("old_" + wName + "_diff1.raw", out);

        engineNew.reset(); engineNew.setParams(pD1);
        runEngine(engineNew, in, out); writeRaw("new_" + wName + "_diff1.raw", out);

        // 4. Mod 0
        FDNReverb::DSPParams pM0 = baseParams; pM0.modAmount = 0.0f;
        engineOld.reset(); engineOld.setParams(pM0);
        runEngine(engineOld, in, out); writeRaw("old_" + wName + "_mod0.raw", out);

        engineNew.reset(); engineNew.setParams(pM0);
        runEngine(engineNew, in, out); writeRaw("new_" + wName + "_mod0.raw", out);

        // 5. Mod 1
        FDNReverb::DSPParams pM1 = baseParams; pM1.modAmount = 1.0f;
        engineOld.reset(); engineOld.setParams(pM1);
        runEngine(engineOld, in, out); writeRaw("old_" + wName + "_mod1.raw", out);

        engineNew.reset(); engineNew.setParams(pM1);
        runEngine(engineNew, in, out); writeRaw("new_" + wName + "_mod1.raw", out);
    }

    std::cout << "Full comparison test completed. CPU Old: " << cpuOld << " ms, CPU New: " << cpuNew << " ms\n";
    return 0;
}
