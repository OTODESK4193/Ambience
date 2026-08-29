#include <JuceHeader.h>
#include "DSP/UniversalEngine.h"
#include "DSP/DSPParams.h"
#include "AlgorithmPresets.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <chrono>

using namespace FDNReverb;

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; // 1s tone + 2s tail

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
    juce::MessageManager::getInstance(); // Init JUCE

    UniversalEngine engine;
    engine.prepare(48000.0, 256);

    DSPParams baseParams;
    baseParams.algorithmIndex = 0;
    baseParams.decayScale = 1.0f;
    baseParams.diffusion = 0.70f;
    baseParams.modAmount = 0.25f;
    baseParams.modRate = 0.5f;
    baseParams.wetDB = 0.0f;
    baseParams.dryDB = -100.0f; // 100% wet
    baseParams.preDelayMs = 0.0f;
    baseParams.roomSizeScale = 0.0f;
    baseParams.loCutHz = 20.0f;
    baseParams.hiCutHz = 20000.0f;

    std::vector<float> inSine(NUM_SAMPLES), inSaw(NUM_SAMPLES), inSquare(NUM_SAMPLES), inSync(NUM_SAMPLES), inFM(NUM_SAMPLES);
    for(int n=0; n<NUM_SAMPLES; ++n) {
        double t = (double)n / FS;
        double env = getEnv(n) * 0.5;
        inSine[n] = static_cast<float>(getSine(t) * env);
        inSaw[n] = static_cast<float>(getSaw(t) * env);
        inSquare[n] = static_cast<float>(getSquare(t) * env);
        inSync[n] = static_cast<float>(getSync(t) * env);
        inFM[n] = static_cast<float>(getFM(t) * env);
    }

    std::vector<float> out(NUM_SAMPLES);

    // CPU Measurement
    engine.reset();
    engine.setParams(baseParams);
    auto startCpu = std::chrono::high_resolution_clock::now();
    runEngineTest(engine, inSine, out);
    auto endCpu = std::chrono::high_resolution_clock::now();
    double cpuMs = std::chrono::duration<double, std::milli>(endCpu - startCpu).count();
    
    std::ofstream cpuFile("cpu_v120.txt");
    cpuFile << cpuMs << "\n";
    cpuFile.close();

    // 1. Process standard 5 waveforms
    engine.reset(); engine.setParams(baseParams);
    runEngineTest(engine, inSine, out); writeRaw("v120_Sine.raw", out);

    engine.reset(); engine.setParams(baseParams);
    runEngineTest(engine, inSaw, out); writeRaw("v120_Saw.raw", out);

    engine.reset(); engine.setParams(baseParams);
    runEngineTest(engine, inSquare, out); writeRaw("v120_Square.raw", out);

    engine.reset(); engine.setParams(baseParams);
    runEngineTest(engine, inSync, out); writeRaw("v120_Sync.raw", out);

    engine.reset(); engine.setParams(baseParams);
    runEngineTest(engine, inFM, out); writeRaw("v120_FM.raw", out);

    // 2. Diffusion tests (Diff=0 vs Diff=1)
    DSPParams diff0Params = baseParams;
    diff0Params.diffusion = 0.0f;
    engine.reset(); engine.setParams(diff0Params);
    runEngineTest(engine, inSine, out); writeRaw("v120_Sine_Diff0.raw", out);

    DSPParams diff1Params = baseParams;
    diff1Params.diffusion = 1.0f;
    engine.reset(); engine.setParams(diff1Params);
    runEngineTest(engine, inSine, out); writeRaw("v120_Sine_Diff1.raw", out);

    // 3. Mod tests (Mod=0 vs Mod=1)
    DSPParams mod0Params = baseParams;
    mod0Params.modAmount = 0.0f;
    engine.reset(); engine.setParams(mod0Params);
    runEngineTest(engine, inSine, out); writeRaw("v120_Sine_Mod0.raw", out);

    DSPParams mod1Params = baseParams;
    mod1Params.modAmount = 1.0f;
    engine.reset(); engine.setParams(mod1Params);
    runEngineTest(engine, inSine, out); writeRaw("v120_Sine_Mod1.raw", out);

    std::cout << "RealEngine_Test finished successfully. CPU: " << cpuMs << " ms\n";
    return 0;
}
