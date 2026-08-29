#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <array>
#include <chrono>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; 

// --- Biquad Filter (Bandpass) ---
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

// --- Approach 1: High-Density DVN (HD-DVN) ---
void processHD_DVN(const std::vector<float>& in, std::vector<float>& out, int density) {
    uint32_t seed = 123456789;
    int tailSamples = FS * 2;
    std::vector<float> vn(tailSamples, 0.0f);
    int gridSmp = std::max(1, FS / density); 
    
    for(int i=0; i<tailSamples; i+=gridSmp) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float jitter = static_cast<float>(seed) * 2.3283064365386963e-10f * 0.5f + 0.5f; 
        int pos = i + static_cast<int>(jitter * gridSmp);
        if(pos >= tailSamples) pos = tailSamples - 1;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float sign = (seed & 1) ? 1.0f : -1.0f;
        float env = std::exp(-static_cast<float>(pos) / (FS * 0.5f)); 
        vn[pos] += sign * env * (1.0f / std::sqrt((float)density/4000.0f)); // Normalize gain
    }
    
    // FIR Convolution
    for(int n=0; n<in.size(); ++n) {
        if(in[n] > 0.1f) {
            for(int i=0; i<tailSamples; ++i) {
                if (n+i < out.size()) out[n+i] += in[n] * vn[i];
            }
        }
    }
}

// --- Approach 2: Spectral Smoothed DVN (SS-DVN) ---
struct SimpleAllpass {
    float delayBuffer[256] = {0};
    int wp = 0, length = 0;
    float gain = 0.5f;
    void init(int len, float g) { length = len; gain = g; }
    float process(float x) {
        int rp = (wp - length + 256) % 256;
        float d = delayBuffer[rp];
        float w = x + gain * d;
        delayBuffer[wp] = w;
        wp = (wp + 1) % 256;
        return d - gain * w;
    }
};

void processSS_DVN(const std::vector<float>& in, std::vector<float>& out) {
    // 1. Generate base 4000-density DVN
    std::vector<float> baseOut(NUM_SAMPLES, 0.0f);
    processHD_DVN(in, baseOut, 4000);
    
    // 2. Apply short high-frequency Allpass smoothing (Phase randomization)
    SimpleAllpass ap1, ap2;
    ap1.init(17, 0.6f); // Short prime delays
    ap2.init(37, 0.5f);
    
    for(int n=0; n<NUM_SAMPLES; ++n) {
        float x = baseOut[n];
        // Only smooth high frequencies (very basic HPF approach to split)
        // Here we just apply APF to everything for testing the HF phase smoothing impact
        x = ap1.process(x);
        x = ap2.process(x);
        out[n] = x;
    }
}

// --- Metrics Analysis ---
struct Metrics {
    float roughness;
    float sfm;
    float density;
};

Metrics analyzeOutput(const std::vector<float>& out, float fc) {
    Metrics m = {0,0,0};
    BiquadBPF bpf; bpf.set(fc, 1.414f);
    
    double sumSq = 0;
    std::vector<float> env(NUM_SAMPLES, 0.0f);
    double logSum = 0;
    int sfmCount = 0;
    std::vector<float> bandOut(NUM_SAMPLES, 0.0f);

    for(int n=0; n<NUM_SAMPLES; ++n) {
        float v = bpf.process(out[n]);
        bandOut[n] = v;
        if(n > FS) {
            float vsq = v*v;
            sumSq += vsq;
            env[n] = std::abs(v);
            if(vsq > 1e-12f) { logSum += std::log(vsq); sfmCount++; }
        }
    }
    
    // 1. Roughness
    double diffSqSum = 0, diffSum = 0;
    for(int i=FS+1; i<NUM_SAMPLES; ++i) {
        double diff = env[i] - env[i-1];
        diffSqSum += diff * diff; diffSum += std::abs(diff);
    }
    int envLen = NUM_SAMPLES - FS;
    double meanDiff = diffSum / envLen;
    double varDiff = (diffSqSum / envLen) - (meanDiff * meanDiff);
    double rms = std::sqrt(sumSq / envLen);
    m.roughness = (rms > 0) ? (std::sqrt(std::max(0.0, varDiff)) / rms * 100.0) : 0;
    
    // 2. SFM
    if(sfmCount > 0 && rms > 0) {
        double geomMean = std::exp(logSum / sfmCount);
        double arithMean = sumSq / sfmCount;
        m.sfm = (arithMean > 0) ? (geomMean / arithMean) : 0;
    }

    // 3. Echo Density
    int denseCount = 0;
    int window = FS / 20; 
    for(int n=FS; n<FS*2; n+=window) {
        double wSumSq = 0;
        for(int k=0; k<window; ++k) wSumSq += bandOut[n+k]*bandOut[n+k];
        double wStdDev = std::sqrt(wSumSq / window);
        int localPeaks = 0;
        for(int k=0; k<window; ++k) if(std::abs(bandOut[n+k]) > wStdDev) localPeaks++;
        float localDensity = (localPeaks / static_cast<float>(window)) / 0.317f; 
        if (localDensity > 1.0f) localDensity = 1.0f;
        denseCount += localDensity * 100.0f; 
    }
    m.density = denseCount / (FS / window); 

    return m;
}

void testApproach(std::string name, int mode, int density = 4000) {
    std::vector<float> in(NUM_SAMPLES, 0.0f); in[0] = 1.0f;
    std::vector<float> out(NUM_SAMPLES, 0.0f);
    
    auto t1 = std::chrono::high_resolution_clock::now();
    if(mode == 1) processHD_DVN(in, out, density);
    else if(mode == 2) processSS_DVN(in, out);
    auto t2 = std::chrono::high_resolution_clock::now();
    int cpuMs = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
    
    Metrics m8 = analyzeOutput(out, 8000.0f);
    Metrics m16 = analyzeOutput(out, 16000.0f);
    
    std::cout << "| " << name << " | " << cpuMs << "ms | "
              << std::fixed << std::setprecision(2) << m8.roughness << " / " << m16.roughness << " | "
              << std::fixed << std::setprecision(3) << m8.sfm << " / " << m16.sfm << " | "
              << std::fixed << std::setprecision(1) << m8.density << " / " << m16.density << " |\n";
}

int main() {
    std::cout << "--- 超高域 (8kHz / 16kHz) 最強アルゴリズム決定戦 ---\n\n";
    std::cout << "| アプローチ | CPU負荷 | Noise/Roughness (8k / 16k) | Metallic (SFM) (8k / 16k) | Diffusion (8k / 16k) |\n";
    std::cout << "|------------|---------|----------------------------|---------------------------|----------------------|\n";
    
    // ベースライン (4000密度)
    testApproach("A. DVN (4,000 pulses)  ", 1, 4000);
    
    // 候補1: 高密度 (HD-DVN)
    testApproach("B1. HD-DVN (16,000 p/s)", 1, 16000);
    testApproach("B2. HD-DVN (32,000 p/s)", 1, 32000);
    
    // 候補2: 位相平滑化 (SS-DVN)
    testApproach("C. SS-DVN (4,000 + APF)", 2, 4000);

    return 0;
}
