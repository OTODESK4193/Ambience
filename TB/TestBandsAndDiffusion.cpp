#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <array>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; // 3 seconds

// ---------------------------------------------------------
// Biquad Bandpass Filter for Band-by-Band Analysis
// ---------------------------------------------------------
struct BiquadBPF {
    float b0, b1, b2, a1, a2;
    float x1=0, x2=0, y1=0, y2=0;
    
    void set(float fc, float q) {
        float w0 = 2.0f * PI * fc / FS;
        float alpha = std::sin(w0) / (2.0f * q);
        float a0 = 1.0f + alpha;
        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = -2.0f * std::cos(w0) / a0;
        a2 = (1.0f - alpha) / a0;
    }
    
    inline float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// ---------------------------------------------------------
// 1024-point Sine LUT
// ---------------------------------------------------------
struct SineLUT {
    static constexpr int SIZE = 1024;
    float table[SIZE + 1];
    SineLUT() {
        for (int i = 0; i <= SIZE; ++i) table[i] = std::sin(2.0f * PI * i / SIZE);
    }
    inline float sin(float phase) const {
        float p = phase - std::floor(phase);
        if(p < 0) p += 1.0f;
        float idx = p * SIZE;
        int i0 = static_cast<int>(idx);
        float frac = idx - static_cast<float>(i0);
        return table[i0] + frac * (table[i0 + 1] - table[i0]);
    }
    inline float cos(float phase) const { return sin(phase + 0.25f); }
};

SineLUT lut;

// ---------------------------------------------------------
// IntDelayLine
// ---------------------------------------------------------
struct IntDelayLine {
    std::vector<float> buffer;
    int writePos = 0, mask;
    IntDelayLine(int maxLen) {
        int p = 1; while(p < maxLen) p *= 2;
        buffer.assign(p, 0.0f);
        mask = p - 1;
    }
    inline void write(float v) { buffer[writePos] = v; writePos = (writePos + 1) & mask; }
    inline float read(int delay) const { return buffer[(writePos - delay) & mask]; }
};

// ---------------------------------------------------------
// Reverb Sim Engine (Time-Varying Matrix + Fixed Allpass)
// ---------------------------------------------------------
struct ReverbSim {
    std::vector<IntDelayLine> fdnDelays;
    std::array<int, 16> delayLengths;
    std::array<float, 16> fbVec = {0}, lfoPhases = {0};
    
    std::vector<std::vector<IntDelayLine>> apfDelays;
    std::array<std::array<int, 3>, 16> apfLengths;

    float modAmt = 0.0f;
    float modRate = 1.0f;
    float diffusion = 0.0f;
    
    ReverbSim() {
        for(int i=0; i<16; ++i) {
            fdnDelays.emplace_back(16384);
            delayLengths[i] = 1000 + i * 233 + (i % 5) * 17; // Fake mutually prime lengths
            std::vector<IntDelayLine> apf;
            for(int s=0; s<3; ++s) {
                apf.emplace_back(4096);
                apfLengths[i][s] = 120 + i*13 + s*31;
            }
            apfDelays.push_back(apf);
            lfoPhases[i] = static_cast<float>(i) / 16.0f;
        }
    }

    void process(const std::vector<float>& in, std::vector<float>& out) {
        for (int n = 0; n < in.size(); ++n) {
            std::array<float, 16> currentFb = fbVec;
            // Hadamard
            for (int h = 1; h < 16; h *= 2) {
                for (int i = 0; i < 16; i += h * 2) {
                    for (int j = i; j < i + h; ++j) {
                        float x = currentFb[j], y = currentFb[j + h];
                        currentFb[j] = x + y; currentFb[j + h] = x - y;
                    }
                }
            }
            for (int i = 0; i < 16; ++i) currentFb[i] *= 0.25f;

            // Givens Rotation
            float maxTheta = modAmt * (PI / 4.0f); // Up to 45 deg
            float phaseInc = modRate / FS;
            for (int i = 0; i < 8; ++i) {
                lfoPhases[i] += phaseInc;
                if(lfoPhases[i] > 1.0f) lfoPhases[i] -= 1.0f;
                float theta = lut.sin(lfoPhases[i]) * maxTheta;
                float c = lut.cos(theta / (2.0f * PI)); // approximation for LUT index
                float s = lut.sin(theta / (2.0f * PI));
                float v1 = currentFb[2 * i], v2 = currentFb[2 * i + 1];
                currentFb[2 * i] = v1 * c + v2 * s;
                currentFb[2 * i + 1] = -v1 * s + v2 * c;
            }

            for(int i=0; i<16; ++i) if(i%2==0) currentFb[i] = -currentFb[i];

            float outSum = 0;
            // Diffusion uses the Nested Allpass gain
            float apfGain = 0.3f + diffusion * 0.4f;

            for (int i = 0; i < 16; ++i) {
                float d = fdnDelays[i].read(delayLengths[i]);
                d *= 0.98f; // Low absorption for testing tail

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

void runAnalysis() {
    std::cout << "--- 10 Bands Analysis & Echo Density ---\n";
    float freqs[10] = {31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    
    auto testParams = [&](std::string name, float mod, float diff) {
        ReverbSim sim; sim.modAmt = mod; sim.modRate = 2.0f; sim.diffusion = diff;
        std::vector<float> in(NUM_SAMPLES, 0.0f); in[0] = 1.0f;
        std::vector<float> out(NUM_SAMPLES, 0.0f);
        sim.process(in, out);

        std::cout << "\n[" << name << "]\n";
        
        // 1. 10 Bands RMS Energy
        std::cout << "  Band Energy (RMS x 1000):\n  ";
        for(int b=0; b<10; ++b) {
            BiquadBPF bpf; bpf.set(freqs[b], 1.414f);
            double sumSq = 0;
            for(int n=FS; n<NUM_SAMPLES; ++n) {
                float v = bpf.process(out[n]);
                sumSq += v*v;
            }
            std::cout << freqs[b] << "Hz:" << std::fixed << std::setprecision(2) << (sumSq * 1000.0) << "  ";
        }
        std::cout << "\n";

        // 2. Echo Density (Peaks over threshold in late tail, between 1.0s and 2.0s)
        int peakCount = 0;
        float threshold = 1e-4f;
        for(int n=FS; n<FS*2; ++n) {
            if (std::abs(out[n]) > threshold && std::abs(out[n]) > std::abs(out[n-1]) && std::abs(out[n]) > std::abs(out[n+1])) {
                peakCount++;
            }
        }
        std::cout << "  Echo Density (Peaks/sec): " << peakCount << "\n";
    };

    testParams("Diffusion 0.0, Mod 0.0", 0.0f, 0.0f);
    testParams("Diffusion 1.0, Mod 0.0", 0.0f, 1.0f);
    testParams("Diffusion 1.0, Mod 1.0 (Full New Algo)", 1.0f, 1.0f);
}

int main() {
    runAnalysis();
    return 0;
}
