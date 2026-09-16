#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <memory>

#include "../Source/DSP/UniversalEngine.h"
#include "../Source/AlgorithmPresets.h"

using namespace FDNReverb;

const double SAMPLE_RATE = 48000.0;

struct TestCaseMeta {
    std::string category;
    int algoIndex;
    std::string algoName;
    std::string condition;
    float freq;
    int numSamples;
    uint64_t byteOffset;
};

static const std::vector<std::string> ALGO_NAMES = {
    "Room1", "Room2", "Hall1", "Hall2", "Plate", "Spring", "Goldfoil", "Inchindown"
};

void applyPresetDefaults(DSPParams& p, int algo) {
    if (algo < 0 || algo >= NUM_ALGORITHMS) return;
    const auto& def = PRESET_DEFAULTS[algo];
    p.algorithmIndex = algo;
    p.roomSizeScale = def.roomSize;
    p.decayScale = def.decayTime;
    p.diffusion = def.diffusion;
    p.modAmount = def.modAmount;
    p.modRate = def.modRate;
    p.stereoWidth = def.stereoWidth;
    p.preDelayMs = def.preDelayMs;
    p.erLevel = def.erLevel;
    p.lateLevel = def.lateLevel;
    p.hfDamping = def.hfDamp;
    p.lfAbsorption = def.lfAbsorb;
    p.saturation = def.saturation;
    p.satTypeIdx = def.satType;
    p.scattering = def.scattering;
    p.erCrossoverMs = def.erCrossoverMs;
    p.lateDensity = def.lateDensity;
    p.asymmetry = def.asymmetry;
    p.clarityDB = def.clarityDB;
    p.airAbsorbScale = def.airAbsorbScale;
    p.dryDB = -100.0f; // 100% Wet for strict reverb analysis
    p.wetDB = 0.0f;
}

int main() {
    _mm_setcsr(_mm_getcsr() | 0x8040); // Hardware FTZ & DAZ
    std::cout << "=== High-Quality Ambience Audio Quality Audit Runner ===\n";
    
    FDNReverb::UniversalEngine engine;
    engine.prepare(SAMPLE_RATE, 256);
    
    std::string binPath = "ValidationTools/quality_audit_audio.bin";
    std::string jsonPath = "ValidationTools/quality_audit_meta.json";
    
    std::ofstream binFile(binPath, std::ios::binary);
    if (!binFile.is_open()) {
        std::cerr << "Failed to open " << binPath << "\n";
        return 1;
    }
    
    std::vector<TestCaseMeta> allCases;
    uint64_t currentByteOffset = 0;
    
    auto runAndRecord = [&](const std::string& cat, int algo, const std::string& cond, float freq,
                            const std::vector<float>& inL, const std::vector<float>& inR,
                            const DSPParams& params) {
        engine.reset();
        DSPParams p = params;
        p.dryDB = -100.0f; // 100% Wet
        p.wetDB = 0.0f;
        engine.setParams(p);
        
        // ★ アルゴリズム切り替え時の Graceful Mute トランジション（16ms）を完走させるウォームアップ
        std::vector<float> warmZeros(1024, 0.0f);
        std::vector<float> warmOutL(1024, 0.0f);
        std::vector<float> warmOutR(1024, 0.0f);
        engine.processBlock(warmZeros.data(), warmZeros.data(), warmOutL.data(), warmOutR.data(), 1024);
        
        int totalSamples = static_cast<int>(inL.size());
        std::vector<float> outL(totalSamples, 0.0f);
        std::vector<float> outR(totalSamples, 0.0f);
        
        int blockSize = 256;
        for (int i = 0; i < totalSamples; i += blockSize) {
            int chunk = std::min(blockSize, totalSamples - i);
            engine.processBlock(inL.data() + i, inR.data() + i, outL.data() + i, outR.data() + i, chunk);
        }
        
        // Write stereo interleaved (L then R)
        binFile.write(reinterpret_cast<const char*>(outL.data()), totalSamples * sizeof(float));
        binFile.write(reinterpret_cast<const char*>(outR.data()), totalSamples * sizeof(float));
        
        TestCaseMeta meta;
        meta.category = cat;
        meta.algoIndex = algo;
        meta.algoName = ALGO_NAMES[algo];
        meta.condition = cond;
        meta.freq = freq;
        meta.numSamples = totalSamples;
        meta.byteOffset = currentByteOffset;
        allCases.push_back(meta);
        
        currentByteOffset += totalSamples * sizeof(float) * 2;
    };
    
    // ---------------------------------------------------------
    // 1. Group 1: Dirac Impulse Responses (4.0 seconds)
    // ---------------------------------------------------------
    std::cout << "[1/5] Generating Dirac Impulse Responses (8 algorithms)...\n";
    int irSamples = static_cast<int>(SAMPLE_RATE * 4.0);
    for (int algo = 0; algo < 8; ++algo) {
        DSPParams p;
        applyPresetDefaults(p, algo);
        
        std::vector<float> inL(irSamples, 0.0f);
        std::vector<float> inR(irSamples, 0.0f);
        inL[0] = 1.0f;
        inR[0] = 1.0f;
        
        runAndRecord("Impulse", algo, "Dirac_Impulse", 0.0f, inL, inR, p);
    }
    
    // ---------------------------------------------------------
    // 2. Group 2: Floor Noise & Denormals (Silence, 2.0 seconds)
    // ---------------------------------------------------------
    std::cout << "[2/5] Generating Silence for Floor Noise analysis (8 algorithms)...\n";
    int silenceSamples = static_cast<int>(SAMPLE_RATE * 2.0);
    for (int algo = 0; algo < 8; ++algo) {
        DSPParams p;
        applyPresetDefaults(p, algo);
        
        std::vector<float> inL(silenceSamples, 0.0f);
        std::vector<float> inR(silenceSamples, 0.0f);
        
        runAndRecord("Silence", algo, "Cold_Silence", 0.0f, inL, inR, p);
    }
    
    // ---------------------------------------------------------
    // 3. Group 3: Post-Burst Decay & Floor Recovery (4.0 seconds)
    // ---------------------------------------------------------
    std::cout << "[3/5] Generating Post-Burst Silence Recovery (8 algorithms)...\n";
    int burstTotal = static_cast<int>(SAMPLE_RATE * 4.0);
    int burstLen = static_cast<int>(SAMPLE_RATE * 0.1); // 100ms white noise
    for (int algo = 0; algo < 8; ++algo) {
        DSPParams p;
        applyPresetDefaults(p, algo);
        
        std::vector<float> inL(burstTotal, 0.0f);
        std::vector<float> inR(burstTotal, 0.0f);
        
        uint32_t seed = 123456789 + algo * 777;
        for (int i = 0; i < burstLen; ++i) {
            seed = seed * 1664525u + 1013904223u;
            float n = static_cast<float>(static_cast<int32_t>(seed)) * (1.0f / 2147483648.0f);
            inL[i] = n * 0.5f; // -6dBFS
            inR[i] = n * 0.5f;
        }
        
        runAndRecord("PostBurst", algo, "NoiseBurst_Recovery", 0.0f, inL, inR, p);
    }
    
    // ---------------------------------------------------------
    // 4. Group 4: Extreme Stress & Blowout Testing (5.0 seconds)
    // ---------------------------------------------------------
    std::cout << "[4/5] Running Extreme Stress & Blowout Cases (8 algos x 4 conditions)...\n";
    int stressTotal = static_cast<int>(SAMPLE_RATE * 5.0);
    int sigLen = static_cast<int>(SAMPLE_RATE * 0.5); // 500ms heavy input
    
    std::vector<float> stressInL(stressTotal, 0.0f);
    std::vector<float> stressInR(stressTotal, 0.0f);
    for (int i = 0; i < sigLen; ++i) {
        float t = i / static_cast<float>(SAMPLE_RATE);
        float v = 0.35f * std::sin(2.0f * 3.14159265f * 40.0f * t)
                + 0.35f * std::sin(2.0f * 3.14159265f * 1000.0f * t)
                + 0.30f * std::sin(2.0f * 3.14159265f * 5000.0f * t);
        stressInL[i] = v;
        stressInR[i] = v;
    }
    
    for (int algo = 0; algo < 8; ++algo) {
        // Stress 1: Max Decay (30s)
        {
            DSPParams p;
            applyPresetDefaults(p, algo);
            p.decayScale = 30.0f;
            runAndRecord("Stress", algo, "Max_Decay", 0.0f, stressInL, stressInR, p);
        }
        // Stress 2: Max RoomSize + Max Decay
        {
            DSPParams p;
            applyPresetDefaults(p, algo);
            p.roomSizeScale = 2.0f;
            p.decayScale = 30.0f;
            runAndRecord("Stress", algo, "MaxSize_MaxDecay", 0.0f, stressInL, stressInR, p);
        }
        // Stress 3: Max Nonlinearity (Max Drive Saturation + Max Size + Max Decay)
        {
            DSPParams p;
            applyPresetDefaults(p, algo);
            p.roomSizeScale = 2.0f;
            p.decayScale = 30.0f;
            p.saturation = 1.0f;
            p.erLevel = 1.0f;
            p.lateLevel = 1.0f;
            runAndRecord("Stress", algo, "Max_Nonlinearity", 0.0f, stressInL, stressInR, p);
        }
        // Stress 4: Zero Diffusion Stress
        {
            DSPParams p;
            applyPresetDefaults(p, algo);
            p.diffusion = 0.0f;
            p.decayScale = 10.0f;
            p.roomSizeScale = 1.5f;
            runAndRecord("Stress", algo, "Zero_Diffusion", 0.0f, stressInL, stressInR, p);
        }
    }
    
    // ---------------------------------------------------------
    // 5. Group 5: Tonal Pulse Decays (100Hz, 1kHz, 5kHz, 3.0 seconds)
    // ---------------------------------------------------------
    std::cout << "[5/5] Generating Tonal Pulses for 100Hz, 1kHz, 5kHz (8 algorithms)...\n";
    int pulseTotal = static_cast<int>(SAMPLE_RATE * 3.0);
    int pulseLen = static_cast<int>(SAMPLE_RATE * 0.05); // 50ms sine burst
    std::vector<float> testFreqs = {100.0f, 1000.0f, 5000.0f};
    
    for (float f : testFreqs) {
        std::vector<float> pInL(pulseTotal, 0.0f);
        std::vector<float> pInR(pulseTotal, 0.0f);
        for (int i = 0; i < pulseLen; ++i) {
            float t = i / static_cast<float>(SAMPLE_RATE);
            float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / pulseLen));
            float s = std::sin(2.0f * 3.14159265f * f * t) * w * 0.5f;
            pInL[i] = s;
            pInR[i] = s;
        }
        
        for (int algo = 0; algo < 8; ++algo) {
            DSPParams p;
            applyPresetDefaults(p, algo);
            runAndRecord("TonalPulse", algo, "Pulse_" + std::to_string(static_cast<int>(f)) + "Hz", f, pInL, pInR, p);
        }
    }
    
    binFile.close();
    std::cout << "All audio successfully written to " << binPath << " (" << currentByteOffset / (1024 * 1024) << " MB)\n";
    
    // Write JSON metadata
    std::ofstream jsonFile(jsonPath);
    jsonFile << "[\n";
    for (size_t i = 0; i < allCases.size(); ++i) {
        const auto& c = allCases[i];
        jsonFile << "  {\n";
        jsonFile << "    \"category\": \"" << c.category << "\",\n";
        jsonFile << "    \"algoIndex\": " << c.algoIndex << ",\n";
        jsonFile << "    \"algoName\": \"" << c.algoName << "\",\n";
        jsonFile << "    \"condition\": \"" << c.condition << "\",\n";
        jsonFile << "    \"freq\": " << c.freq << ",\n";
        jsonFile << "    \"numSamples\": " << c.numSamples << ",\n";
        jsonFile << "    \"byteOffset\": " << c.byteOffset << "\n";
        jsonFile << "  }" << (i + 1 < allCases.size() ? "," : "") << "\n";
    }
    jsonFile << "]\n";
    jsonFile.close();
    std::cout << "Metadata written to " << jsonPath << " (Total " << allCases.size() << " test cases)\n";
    
    return 0;
}
