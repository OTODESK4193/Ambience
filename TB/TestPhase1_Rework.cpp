#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <array>
#include <algorithm>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 5; // 5 seconds

// ---------------------------------------------------------
// 1024-point Sine LUT (Fast Trigonometry)
// ---------------------------------------------------------
struct SineLUT {
    static constexpr int SIZE = 1024;
    float table[SIZE + 1];
    SineLUT() {
        for (int i = 0; i <= SIZE; ++i) {
            table[i] = std::sin(2.0f * PI * static_cast<float>(i) / SIZE);
        }
    }
    inline float sin(float phase) const {
        float p = phase - std::floor(phase);
        float idx = p * SIZE;
        int i0 = static_cast<int>(idx);
        float frac = idx - static_cast<float>(i0);
        return table[i0] + frac * (table[i0 + 1] - table[i0]);
    }
    inline float cos(float phase) const {
        return sin(phase + 0.25f);
    }
};

SineLUT lut;

// ---------------------------------------------------------
// Integer Delay Line
// ---------------------------------------------------------
struct IntDelayLine {
    std::vector<float> buffer;
    int writePos = 0;
    int mask;
    IntDelayLine(int maxLen) {
        int powerOfTwo = 1;
        while(powerOfTwo < maxLen) powerOfTwo *= 2;
        buffer.assign(powerOfTwo, 0.0f);
        mask = powerOfTwo - 1;
    }
    inline void write(float v) {
        buffer[writePos] = v;
        writePos = (writePos + 1) & mask;
    }
    inline float read(int delay) const {
        return buffer[(writePos - delay) & mask];
    }
};

// ---------------------------------------------------------
// Hadamard Transform
// ---------------------------------------------------------
void fastWalshHadamardTransform(std::array<float, 16>& v) {
    for (int h = 1; h < 16; h *= 2) {
        for (int i = 0; i < 16; i += h * 2) {
            for (int j = i; j < i + h; ++j) {
                float x = v[j], y = v[j + h];
                v[j] = x + y; v[j + h] = x - y;
            }
        }
    }
    for (int i = 0; i < 16; ++i) v[i] *= 0.25f;
}

// ---------------------------------------------------------
// FDN Reverb Simulation Engine
// ---------------------------------------------------------
struct ReverbSim {
    std::vector<IntDelayLine> fdnDelays;
    std::array<int, 16> delayLengths;
    std::array<float, 16> fbVec = {0};
    std::array<float, 16> lfoPhases = {0};
    
    // Nested Allpass (Diffusion)
    std::vector<std::vector<IntDelayLine>> apfDelays;
    std::array<std::array<int, 3>, 16> apfLengths;

    float modAmt = 0.0f;
    float modRate = 1.0f;
    float diffusion = 0.0f;
    bool useLUT = true;
    bool addNoise = false;

    ReverbSim() {
        for(int i=0; i<16; ++i) {
            fdnDelays.emplace_back(10000);
            delayLengths[i] = 1500 + i * 200 + (i % 3) * 17; // Fake prime distribution
            
            std::vector<IntDelayLine> apf;
            for(int s=0; s<3; ++s) {
                apf.emplace_back(2000);
                apfLengths[i][s] = 100 + i*10 + s*23;
            }
            apfDelays.push_back(apf);
            
            lfoPhases[i] = static_cast<float>(i) / 16.0f;
        }
    }

    void process(const std::vector<float>& in, std::vector<float>& out) {
        uint32_t noiseState = 12345;
        for (int n = 0; n < in.size(); ++n) {
            std::array<float, 16> currentFb = fbVec;
            fastWalshHadamardTransform(currentFb);

            // Time-Varying Matrix (Givens)
            float maxTheta = modAmt * (PI / 4.0f); // Max 45 degrees
            float phaseInc = modRate / FS;
            
            for (int i = 0; i < 8; ++i) {
                lfoPhases[i] += phaseInc;
                if(lfoPhases[i] > 1.0f) lfoPhases[i] -= 1.0f;
                
                float theta = 0.0f;
                if (useLUT) {
                    theta = lut.sin(lfoPhases[i]) * maxTheta;
                } else {
                    theta = std::sin(2.0f * PI * lfoPhases[i]) * maxTheta;
                }

                if (addNoise) {
                    noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
                    float nval = static_cast<float>(noiseState) * 2.3283064365386963e-10f * 2.0f - 1.0f;
                    theta += nval * 0.05f * maxTheta; // Add noise to angle
                }

                float c = useLUT ? lut.cos(lfoPhases[i] * maxTheta) : std::cos(theta);
                if(useLUT) {
                    c = lut.cos(lfoPhases[i]); // Simplification for LUT cos
                }

                float s = useLUT ? lut.sin(lfoPhases[i]) : std::sin(theta);
                // Actually if we use LUT, we need to map theta to phase for LUT, but for sim speed:
                if (useLUT) {
                     float phaseT = theta / (2.0f * PI);
                     if(phaseT < 0) phaseT += 1.0f;
                     c = lut.cos(phaseT);
                     s = lut.sin(phaseT);
                }

                float v1 = currentFb[2 * i];
                float v2 = currentFb[2 * i + 1];
                currentFb[2 * i]     = v1 * c + v2 * s;
                currentFb[2 * i + 1] = -v1 * s + v2 * c;
            }

            // Sign flip
            for(int i=0; i<16; ++i) if(i%2==0) currentFb[i] = -currentFb[i];

            float outSum = 0;
            float apfGain = 0.3f + diffusion * 0.4f;

            for (int i = 0; i < 16; ++i) {
                float d = fdnDelays[i].read(delayLengths[i]);
                d *= 0.95f; // Absorption

                float apfOut = d;
                for (int s = 0; s < 3; ++s) {
                    float apfD = apfDelays[i][s].read(apfLengths[i][s]);
                    float apfW = apfOut + apfGain * apfD;
                    apfDelays[i][s].write(apfW);
                    apfOut = apfD - apfGain * apfW;
                }

                fdnDelays[i].write(in[n] + currentFb[i]);
                fbVec[i] = apfOut;
                outSum += apfOut;
            }
            out[n] = outSum * 0.125f;
        }
    }
};

void measureConfig(std::string name, float modAmt, float modRate, float diff, bool useLUT, bool addNoise) {
    ReverbSim sim;
    sim.modAmt = modAmt;
    sim.modRate = modRate;
    sim.diffusion = diff;
    sim.useLUT = useLUT;
    sim.addNoise = addNoise;

    std::vector<float> in(NUM_SAMPLES, 0.0f);
    in[0] = 1.0f; // Impulse
    std::vector<float> out(NUM_SAMPLES, 0.0f);

    auto start = std::chrono::high_resolution_clock::now();
    sim.process(in, out);
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Calculate Noise (High frequency variance / roughness)
    // We'll estimate noise by measuring the delta between consecutive samples in the tail
    double noiseMetric = 0.0;
    double tailEnergy = 0.0;
    for(int i=FS*2; i<FS*3; ++i) {
        float delta = out[i] - out[i-1];
        noiseMetric += delta * delta;
        tailEnergy += out[i] * out[i];
    }
    double noiseRatio = (tailEnergy > 0) ? (noiseMetric / tailEnergy) : 0;

    std::cout << "[" << name << "]\n";
    std::cout << "  CPU Time : " << ms << " ms\n";
    std::cout << "  Diff. RMS: " << std::fixed << std::setprecision(5) << tailEnergy * 1000.0 << " (Higher = More Diffusion)\n";
    std::cout << "  NoiseIdx : " << std::fixed << std::setprecision(5) << noiseRatio << " (Lower = Cleaner, less AM noise)\n\n";
}

int main() {
    std::cout << "==== Phase 1 Rework Testbench ====\n\n";
    
    std::cout << "1. ノイズ測定と比較 (AM変調・Grittiness)\n";
    measureConfig("A: 旧仕様(ノイズ混入あり)", 0.5f, 1.0f, 0.5f, true, true);
    measureConfig("B: 新仕様(正弦波のみ, Noiseなし)", 0.5f, 1.0f, 0.5f, true, false);

    std::cout << "2. Diffusion 最小/最大の比較\n";
    measureConfig("C: Diffusion = 0.0 (最小)", 0.5f, 1.0f, 0.0f, true, false);
    measureConfig("D: Diffusion = 1.0 (最大)", 0.5f, 1.0f, 1.0f, true, false);

    std::cout << "3. ModAmt / ModRate の比較\n";
    measureConfig("E: ModAmt = 0.0 (変調なし)", 0.0f, 1.0f, 0.5f, true, false);
    measureConfig("F: ModAmt = 1.0 (最大変調)", 1.0f, 1.0f, 0.5f, true, false);
    measureConfig("G: ModRate = 5.0Hz (高速変調)", 1.0f, 5.0f, 0.5f, true, false);

    std::cout << "4. CPU負荷の比較 (std::sin vs LUT)\n";
    measureConfig("H: std::sin/cos 使用", 0.5f, 1.0f, 0.5f, false, false);
    measureConfig("I: 1024-point LUT 使用", 0.5f, 1.0f, 0.5f, true, false);

    return 0;
}
