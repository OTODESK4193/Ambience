#include <JuceHeader.h>
#include "../Source/DSP/UniversalEngine.h"
#include "../Source/DSP/DSPParams.h"
#include "UniversalEngine_v121.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <chrono>
#include <string>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; // 1s tone + 2s tail

double getSample(int type, double freq, double t) {
    switch(type) {
        case 0: return std::sin(2.0 * PI * freq * t); // Sine
        case 1: return 2.0 * std::fmod(freq * t, 1.0) - 1.0; // Saw
        case 2: return (std::sin(2.0 * PI * freq * t) > 0) ? 1.0 : -1.0; // Square
        case 3: { // Sync
            double mPhase = std::fmod(freq * t, 1.0);
            return 2.0 * std::fmod((freq * 2.727) * mPhase / freq, 1.0) - 1.0;
        }
        case 4: return std::sin(2.0 * PI * freq * t + 3.0 * std::sin(2.0 * PI * (freq * 2.0) * t)); // FM
        default: return 0.0;
    }
}

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
    baseParams.modAmount = 0.50f;
    baseParams.modRate = 0.5f;
    baseParams.wetDB = 0.0f;
    baseParams.dryDB = -100.0f; // 100% wet
    baseParams.preDelayMs = 0.0f;
    baseParams.roomSizeScale = 0.0f;
    baseParams.loCutHz = 20.0f;
    baseParams.hiCutHz = 20000.0f;

    std::vector<std::string> waveNames = {"Sine", "Saw", "Square", "Sync", "FM"};
    std::vector<double> freqs = {31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

    std::vector<float> inBuffer(NUM_SAMPLES);
    std::vector<float> outBuffer(NUM_SAMPLES);

    // CPU measurement with 1kHz Sine
    for(int n=0; n<NUM_SAMPLES; ++n) inBuffer[n] = static_cast<float>(getSample(0, 1000.0, (double)n/FS) * getEnv(n) * 0.5);
    
    engineOld.reset(); engineOld.setParams(baseParams);
    auto t0 = std::chrono::high_resolution_clock::now();
    runEngine(engineOld, inBuffer, outBuffer);
    auto t1 = std::chrono::high_resolution_clock::now();
    double cpuOld = std::chrono::duration<double, std::milli>(t1 - t0).count();

    engineNew.reset(); engineNew.setParams(baseParams);
    auto t2 = std::chrono::high_resolution_clock::now();
    runEngine(engineNew, inBuffer, outBuffer);
    auto t3 = std::chrono::high_resolution_clock::now();
    double cpuNew = std::chrono::duration<double, std::milli>(t3 - t2).count();

    std::ofstream cpuFile("cpu_noise_test.txt");
    cpuFile << cpuOld << "\n" << cpuNew << "\n";
    cpuFile.close();

    // 5 Waves x 10 Bands = 50 Items Test
    for (int w = 0; w < 5; ++w) {
        for (int f = 0; f < 10; ++f) {
            double freq = freqs[f];
            for (int n = 0; n < NUM_SAMPLES; ++n) {
                double t = (double)n / FS;
                double env = getEnv(n) * 0.5;
                inBuffer[n] = static_cast<float>(getSample(w, freq, t) * env);
            }

            std::string prefixOld = "noise_old_" + waveNames[w] + "_" + std::to_string((int)freq);
            std::string prefixNew = "noise_new_" + waveNames[w] + "_" + std::to_string((int)freq);

            // Process Old V1.2.0
            engineOld.reset(); engineOld.setParams(baseParams);
            runEngine(engineOld, inBuffer, outBuffer);
            writeRaw(prefixOld + ".raw", outBuffer);

            // Process New V1.2.1 Update
            engineNew.reset(); engineNew.setParams(baseParams);
            runEngine(engineNew, inBuffer, outBuffer);
            writeRaw(prefixNew + ".raw", outBuffer);
        }
    }

    std::cout << "50-Item Noise Benchmark completed successfully.\n";
    return 0;
}
