#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <array>
#include <numeric>
#include <algorithm>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 4; 

// --- Filters ---
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

struct SineLUT {
    static constexpr int SIZE = 1024;
    float table[SIZE + 1];
    SineLUT() { for (int i = 0; i <= SIZE; ++i) table[i] = std::sin(2.0f * PI * i / SIZE); }
    inline float sin(float phase) const {
        float p = phase - std::floor(phase);
        if(p < 0) p += 1.0f;
        float idx = p * SIZE;
        int i0 = static_cast<int>(idx);
        return table[i0] + (idx - i0) * (table[i0 + 1] - table[i0]);
    }
    inline float cos(float phase) const { return sin(phase + 0.25f); }
};
SineLUT lut;

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

// --- Algo 1: V1.2.1B001 (Time-Varying Matrix FDN) ---
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
            if(lfoPhases[i] > 1.0f) lfoPhases[i] -= 1.0f;
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
            d *= 0.98f; // Decay
            fdnDelays[i].write(in[n] + currentFb[i]);
            fbVec[i] = d;
            outSum += d;
        }
        out[n] = outSum * 0.125f;
    }
}

// --- Algo 2: Dark Velvet Noise (FIR Approximation) ---
void processDVN(const std::vector<float>& in, std::vector<float>& out) {
    uint32_t seed = 987654321;
    // Pre-generate Velvet Noise sequence (4000 pulses/sec)
    int tailSamples = FS * 3;
    std::vector<float> vn(tailSamples, 0.0f);
    int pulseDensity = 4000;
    int gridSmp = FS / pulseDensity; 
    
    for(int i=0; i<tailSamples; i+=gridSmp) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float jitter = static_cast<float>(seed) * 2.3283064365386963e-10f * 0.5f + 0.5f; // 0 to 1
        int pos = i + static_cast<int>(jitter * gridSmp);
        if(pos >= tailSamples) pos = tailSamples - 1;
        
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float sign = (seed & 1) ? 1.0f : -1.0f;
        
        float env = std::exp(-static_cast<float>(pos) / (FS * 0.5f)); 
        vn[pos] = sign * env * 0.05f;
    }
    
    // Simple 1-pole Lowpass for "Dark" aspect
    float y1 = 0;
    for(int i=0; i<tailSamples; ++i) {
        y1 = y1 + 0.2f * (vn[i] - y1);
        vn[i] = y1;
    }

    // Convolution (FIR)
    for(int n=0; n<in.size(); ++n) {
        if(in[n] > 0.1f) {
            for(int i=0; i<tailSamples; ++i) {
                if (n+i < out.size()) out[n+i] += in[n] * vn[i];
            }
        }
    }
}

// --- Metrics ---
struct Metrics {
    float roughness; // 1. Noise/AM var
    float sfm;       // 2. Metallic Ringing (Spectral Flatness)
    float density;   // 3. Echo Density
    float doppler;   // 4. Pitch shift (%)
};

Metrics analyzeOutput(const std::vector<float>& out, float fc) {
    Metrics m = {0,0,0,0};
    
    BiquadBPF bpf; bpf.set(fc, 1.414f);
    std::vector<float> bandOut(NUM_SAMPLES, 0.0f);
    
    // 1. & 2. Roughness & SFM
    double sumSq = 0;
    std::vector<float> env(NUM_SAMPLES, 0.0f);
    double logSum = 0;
    int sfmCount = 0;

    for(int n=0; n<NUM_SAMPLES; ++n) {
        float v = bpf.process(out[n]);
        bandOut[n] = v;
        if(n > FS) {
            float vsq = v*v;
            sumSq += vsq;
            env[n] = std::abs(v);
            if(vsq > 1e-12f) {
                logSum += std::log(vsq);
                sfmCount++;
            }
        }
    }
    
    double diffSqSum = 0, diffSum = 0;
    for(int i=FS+1; i<NUM_SAMPLES; ++i) {
        double diff = env[i] - env[i-1];
        diffSqSum += diff * diff;
        diffSum += std::abs(diff);
    }
    int envLen = NUM_SAMPLES - FS;
    double meanDiff = diffSum / envLen;
    double varDiff = (diffSqSum / envLen) - (meanDiff * meanDiff);
    double rms = std::sqrt(sumSq / envLen);
    m.roughness = (rms > 0) ? (std::sqrt(std::max(0.0, varDiff)) / rms * 100.0) : 0;
    
    if(sfmCount > 0 && rms > 0) {
        double geomMean = std::exp(logSum / sfmCount);
        double arithMean = sumSq / sfmCount;
        m.sfm = (arithMean > 0) ? (geomMean / arithMean) : 0;
    }

    // 3. Abel Echo Density (peaks over standard deviation in a sliding window)
    int denseCount = 0;
    int window = FS / 20; // 50ms window
    for(int n=FS; n<FS*2; n+=window) {
        double wSumSq = 0;
        for(int k=0; k<window; ++k) wSumSq += bandOut[n+k]*bandOut[n+k];
        double wStdDev = std::sqrt(wSumSq / window);
        int localPeaks = 0;
        for(int k=0; k<window; ++k) {
            if(std::abs(bandOut[n+k]) > wStdDev) localPeaks++;
        }
        // Perfect Gaussian noise has approx 31.7% of samples outside 1 stddev
        float localDensity = (localPeaks / static_cast<float>(window)) / 0.317f; 
        if (localDensity > 1.0f) localDensity = 1.0f;
        denseCount += localDensity * 100.0f; 
    }
    m.density = denseCount / (FS / window); // percentage 0-100%

    // 4. Doppler / Pitch variation (simplified zero-crossing distance variance)
    int zcCount = 0;
    std::vector<int> zcDistances;
    int lastZc = 0;
    for(int n=FS; n<FS*2; ++n) {
        if(bandOut[n] * bandOut[n-1] < 0) {
            if(lastZc > 0) zcDistances.push_back(n - lastZc);
            lastZc = n;
        }
    }
    if(zcDistances.size() > 2) {
        double dMean = 0; for(int d : zcDistances) dMean += d; dMean /= zcDistances.size();
        double dVar = 0; for(int d : zcDistances) dVar += (d - dMean)*(d - dMean); dVar /= zcDistances.size();
        m.doppler = std::sqrt(dVar) / dMean * 100.0; // CV of pitch period
    }

    return m;
}

int main() {
    std::cout << "Running Ultimate 4-Metric Benchmark...\n\n";
    float freqs[10] = {31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

    std::vector<float> in(NUM_SAMPLES, 0.0f); in[0] = 1.0f;
    std::vector<float> outFDN(NUM_SAMPLES, 0.0f);
    std::vector<float> outDVN(NUM_SAMPLES, 0.0f);

    processFDN(in, outFDN);
    processDVN(in, outDVN);

    std::cout << "| Freq (Hz) | [1] Noise (Roughness %)<br>FDN vs DVN | [2] Metallic (SFM 0-1)<br>FDN vs DVN | [3] Diffusion (%)<br>FDN vs DVN | [4] Pitch Shift (%)<br>FDN vs DVN |\n";
    std::cout << "|-----------|-------------------------------------|--------------------------------------|-----------------------------------|------------------------------------|\n";

    for(int i=0; i<10; ++i) {
        Metrics mF = analyzeOutput(outFDN, freqs[i]);
        Metrics mD = analyzeOutput(outDVN, freqs[i]);
        
        std::cout << "| " << std::fixed << std::setprecision(0) << freqs[i] << " | "
                  << std::fixed << std::setprecision(2) << mF.roughness << " vs " << mD.roughness << " | "
                  << std::fixed << std::setprecision(3) << mF.sfm << " vs " << mD.sfm << " | "
                  << std::fixed << std::setprecision(1) << mF.density << " vs " << mD.density << " | "
                  << std::fixed << std::setprecision(2) << mF.doppler << " vs " << mD.doppler << " |\n";
    }

    return 0;
}
