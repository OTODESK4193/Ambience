#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <array>
#include <chrono>
#include <numeric>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; 

// --- DSP Utilities ---
struct SineLUT {
    static constexpr int SIZE = 1024;
    float table[SIZE + 1];
    SineLUT() { for (int i = 0; i <= SIZE; ++i) table[i] = std::sin(2.0f * PI * i / SIZE); }
    inline float sin(float phase) const {
        float p = phase - std::floor(phase);
        if(p < 0) p += 1.0f;
        return table[static_cast<int>(p * SIZE)];
    }
    inline float cos(float phase) const { return sin(phase + 0.25f); }
};
SineLUT lut;

struct BiquadBPF {
    float b0, b1, b2, a1, a2, x1=0, x2=0, y1=0, y2=0;
    void set(float fc, float q) {
        float w0 = 2.0f * PI * fc / FS;
        float alpha = std::sin(w0) / (2.0f * q);
        float a0 = 1.0f + alpha;
        b0 = alpha / a0; b1 = 0.0f; b2 = -alpha / a0;
        a1 = -2.0f * std::cos(w0) / a0; a2 = (1.0f - alpha) / a0;
    }
    inline float process(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

struct IntDelayLine {
    std::vector<float> buffer;
    int writePos = 0, mask;
    IntDelayLine(int maxLen) {
        int p = 1; while(p < maxLen) p *= 2;
        buffer.assign(p, 0.0f); mask = p - 1;
    }
    inline void write(float v) { buffer[writePos] = v; writePos = (writePos + 1) & mask; }
    inline float read(int delay) const { return buffer[(writePos - delay) & mask]; }
};

// --- [A] V1.2.1B001 (Time-Varying Matrix FDN) ---
void processFDN(const std::vector<float>& in, std::vector<float>& out) {
    std::vector<IntDelayLine> fdnDelays;
    std::array<int, 16> delayLengths;
    std::array<float, 16> fbVec = {0}, lfoPhases = {0};
    for(int i=0; i<16; ++i) {
        fdnDelays.emplace_back(16384);
        delayLengths[i] = 1000 + i * 233 + (i % 5) * 17;
        lfoPhases[i] = static_cast<float>(i) / 16.0f;
    }
    for (int n = 0; n < in.size(); ++n) {
        std::array<float, 16> currentFb = fbVec;
        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    float x = currentFb[j], y = currentFb[j + h];
                    currentFb[j] = x + y; currentFb[j + h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) currentFb[i] *= 0.25f;

        float maxTheta = 1.0f * (PI / 4.0f); 
        float phaseInc = 1.0f / FS;
        for (int i = 0; i < 8; ++i) {
            lfoPhases[i] += phaseInc;
            float theta = lut.sin(lfoPhases[i]) * maxTheta;
            float c = lut.cos(theta / (2.0f * PI));
            float s = lut.sin(theta / (2.0f * PI));
            float v1 = currentFb[2 * i], v2 = currentFb[2 * i + 1];
            currentFb[2 * i] = v1 * c + v2 * s;
            currentFb[2 * i + 1] = -v1 * s + v2 * c;
        }

        float outSum = 0;
        for (int i = 0; i < 16; ++i) {
            float d = fdnDelays[i].read(delayLengths[i]);
            d *= 0.98f; 
            fdnDelays[i].write(in[n] + currentFb[i]);
            fbVec[i] = d;
            outSum += d;
        }
        out[n] = outSum * 0.125f;
    }
}

// --- [B & C] Time-Varying Velvet Noise (TV-VN) ---
void processTV_VN(const std::vector<float>& in, std::vector<float>& out, float diff, float mod) {
    int pulseDensity = 1000 + static_cast<int>(diff * 15000); // 1,000 to 16,000 p/s
    int tailSamples = FS * 2;
    std::vector<float> vnA(tailSamples, 0.0f);
    std::vector<float> vnB(tailSamples, 0.0f);
    
    auto generateVN = [&](std::vector<float>& seq, uint32_t seed) {
        int gridSmp = std::max(1, FS / pulseDensity); 
        for(int i=0; i<tailSamples; i+=gridSmp) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            float jitter = static_cast<float>(seed) * 2.328e-10f * 0.5f + 0.5f; 
            int pos = i + static_cast<int>(jitter * gridSmp);
            if(pos >= tailSamples) pos = tailSamples - 1;
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            float sign = (seed & 1) ? 1.0f : -1.0f;
            float env = std::exp(-static_cast<float>(pos) / (FS * 0.5f)); 
            seq[pos] += sign * env * (1.0f / std::sqrt((float)pulseDensity/4000.0f)); 
        }
    };
    generateVN(vnA, 12345);
    generateVN(vnB, 98765);

    float lfoPhase = 0.0f;
    float phaseInc = 1.0f / FS; // 1Hz LFO
    float modMix = mod;

    for(int n=0; n<in.size(); ++n) {
        lfoPhase += phaseInc;
        float angle = lfoPhase * PI * 0.5f * modMix; // Slowly crossfade between A and B
        float gainA = std::cos(angle);
        float gainB = std::sin(angle);

        if(in[n] > 0.1f) {
            for(int i=0; i<tailSamples; ++i) {
                if (n+i < out.size()) {
                    float v = (vnA[i] * gainA + vnB[i] * gainB);
                    out[n+i] += in[n] * v;
                }
            }
        }
    }
}

// --- Metrics ---
struct Metrics {
    float roughness;
    float metallic; // Modal variance
    float density;
    float pitchShift; // Doppler variation
    float modIndex;   // Time-variance (Chorus effect)
};

Metrics analyzeOutput(const std::vector<float>& out, float fc) {
    Metrics m = {0,0,0,0,0};
    BiquadBPF bpf; bpf.set(fc, 1.414f);
    
    std::vector<float> bandOut(NUM_SAMPLES, 0.0f);
    std::vector<float> env(NUM_SAMPLES, 0.0f);
    double sumSq = 0;

    for(int n=0; n<NUM_SAMPLES; ++n) {
        float v = bpf.process(out[n]);
        bandOut[n] = v;
        if(n > FS) {
            sumSq += v*v;
            env[n] = std::abs(v);
        }
    }
    
    // 1. Roughness
    double diffSqSum = 0;
    for(int i=FS+1; i<NUM_SAMPLES; ++i) {
        double diff = env[i] - env[i-1];
        diffSqSum += diff * diff;
    }
    double rms = std::sqrt(sumSq / (NUM_SAMPLES - FS));
    m.roughness = (rms > 0) ? (std::sqrt(diffSqSum / (NUM_SAMPLES - FS)) / rms * 100.0) : 0;
    
    // 2. Metallic (Spectral/Modal Variance within band)
    // We use the variance of the envelope as a proxy for modal beating (lower = less metallic beating)
    double envVar = 0;
    for(int i=FS; i<NUM_SAMPLES; ++i) {
        envVar += (env[i] - rms)*(env[i] - rms);
    }
    m.metallic = (rms > 0) ? (std::sqrt(envVar / (NUM_SAMPLES - FS)) / rms * 100.0) : 0;

    // 3. Density
    int denseCount = 0;
    int window = FS / 20; 
    for(int n=FS; n<FS*2; n+=window) {
        double wSumSq = 0;
        for(int k=0; k<window; ++k) wSumSq += bandOut[n+k]*bandOut[n+k];
        double wStdDev = std::sqrt(wSumSq / window);
        int localPeaks = 0;
        for(int k=0; k<window; ++k) if(std::abs(bandOut[n+k]) > wStdDev) localPeaks++;
        float localDensity = (localPeaks / static_cast<float>(window)) / 0.317f; 
        if(localDensity > 1.0f) localDensity = 1.0f;
        denseCount += localDensity * 100.0f; 
    }
    m.density = denseCount / (FS / window); 

    // 4. Pitch Shift (ZCR variance)
    std::vector<int> zcs;
    int lastZc = 0;
    for(int n=FS; n<NUM_SAMPLES; ++n) {
        if(bandOut[n] * bandOut[n-1] < 0) {
            if(lastZc > 0) zcs.push_back(n - lastZc);
            lastZc = n;
        }
    }
    if(zcs.size() > 10) {
        double mZ = 0; for(int z : zcs) mZ += z; mZ /= zcs.size();
        double vZ = 0; for(int z : zcs) vZ += (z - mZ)*(z - mZ); vZ /= zcs.size();
        m.pitchShift = std::sqrt(vZ) / mZ * 100.0;
    }

    return m;
}

int main() {
    float freqs[10] = {31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    
    std::vector<float> in(NUM_SAMPLES, 0.0f); in[0] = 1.0f;
    std::vector<float> outA(NUM_SAMPLES, 0.0f);
    std::vector<float> outB(NUM_SAMPLES, 0.0f);
    std::vector<float> outC(NUM_SAMPLES, 0.0f);
    
    auto t1 = std::chrono::high_resolution_clock::now();
    processFDN(in, outA);
    auto t2 = std::chrono::high_resolution_clock::now();
    int cpuA = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    t1 = std::chrono::high_resolution_clock::now();
    processTV_VN(in, outB, 0.0f, 0.0f); // B: Diff=0, Mod=0
    t2 = std::chrono::high_resolution_clock::now();
    int cpuB = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    t1 = std::chrono::high_resolution_clock::now();
    processTV_VN(in, outC, 1.0f, 1.0f); // C: Diff=1, Mod=1
    t2 = std::chrono::high_resolution_clock::now();
    int cpuC = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    std::cout << "--- 妥協なき全帯域 完全比較テスト ---\n\n";
    std::cout << "**【CPU負荷】**\n";
    std::cout << "- A. 旧 FDN (V1.2.1B001): " << cpuA << " ms\n";
    std::cout << "- B. 新 TV-VN (Diff:0, Mod:0): " << cpuB << " ms\n";
    std::cout << "- C. 新 TV-VN (Diff:1, Mod:1): " << cpuC << " ms\n\n";

    std::cout << "| Freq (Hz) | ① Noise (ザラつき) (低=良)<br>旧FDN vs TV-VN(C) | ② Metallic (金属的うなり) (低=良)<br>旧FDN vs TV-VN(C) | ③ Diffusion (%)<br>Diff 0(B) vs Diff 1(C) | ④ Pitch Shift (%)<br>旧FDN vs TV-VN(C) |\n";
    std::cout << "|-----------|------------------------------------------------|---------------------------------------------------|--------------------------------------|---------------------------------------|\n";
    
    for(int i=0; i<10; ++i) {
        Metrics mA = analyzeOutput(outA, freqs[i]);
        Metrics mB = analyzeOutput(outB, freqs[i]);
        Metrics mC = analyzeOutput(outC, freqs[i]);
        
        std::cout << "| " << std::fixed << std::setprecision(0) << freqs[i] << " | "
                  << std::fixed << std::setprecision(2) << mA.roughness << " vs " << mC.roughness << " | "
                  << std::fixed << std::setprecision(2) << mA.metallic << " vs " << mC.metallic << " | "
                  << std::fixed << std::setprecision(1) << mB.density << "% vs " << mC.density << "% | "
                  << std::fixed << std::setprecision(2) << mA.pitchShift << " vs " << mC.pitchShift << " |\n";
    }

    return 0;
}
