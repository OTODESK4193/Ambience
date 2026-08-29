#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <array>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; 

// --- Biquad Bandpass for Metrics ---
struct BiquadBPF {
    double b0, b1, b2, a1, a2, x1=0, x2=0, y1=0, y2=0;
    void set(double fc, double q) {
        double w0 = 2.0 * PI * fc / FS;
        double alpha = std::sin(w0) / (2.0 * q);
        double a0 = 1.0 + alpha;
        b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0;
        a1 = -2.0 * std::cos(w0) / a0; a2 = (1.0 - alpha) / a0;
    }
    inline double process(double x) {
        double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// --- Delay Line (Hermite) ---
struct DelayLine {
    std::vector<double> buffer;
    int writePos = 0, mask;
    DelayLine(int maxLen) {
        int p = 1; while(p < maxLen) p *= 2;
        buffer.assign(p, 0.0); mask = p - 1;
    }
    inline void write(double v) { buffer[writePos] = v; writePos = (writePos + 1) & mask; }
    
    inline double readLinear(double delay) const {
        int id = static_cast<int>(delay);
        double frac = delay - id;
        double y0 = buffer[(writePos - id) & mask];
        double y1 = buffer[(writePos - id - 1) & mask];
        return y0 + frac * (y1 - y0);
    }
    
    inline double readHermite(double delay) const {
        int id = static_cast<int>(delay);
        double frac = delay - id;
        double y0 = buffer[(writePos - id + 1) & mask];
        double y1 = buffer[(writePos - id) & mask];
        double y2 = buffer[(writePos - id - 1) & mask];
        double y3 = buffer[(writePos - id - 2) & mask];
        double c0 = y1;
        double c1 = 0.5 * (y2 - y0);
        double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};

// [A] V1.2.0 (Commit d4df39b) Engine
const std::array<int, 16> primesV120 = {1009, 1151, 1301, 1451, 1601, 1753, 1901, 2053, 2203, 2351, 2503, 2657, 2801, 2953, 3109, 3251};
const std::array<int, 3> nestedPrimesV120 = {73, 109, 163};

void processV120(const std::vector<float>& in, std::vector<float>& out) {
    std::vector<DelayLine> fdn(16, DelayLine(8192));
    std::vector<std::vector<DelayLine>> nested(16, std::vector<DelayLine>(3, DelayLine(1024)));
    std::array<double, 16> fb = {0};
    double decayCoeff = std::exp(-6.91 / (3.0 * FS));
    uint32_t seed = 12345;
    
    for(int n=0; n<in.size(); ++n) {
        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    double x = fb[j], y = fb[j+h];
                    fb[j] = x + y; fb[j+h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) fb[i] *= 0.25;

        double outSum = 0;
        for(int i=0; i<16; ++i) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            double noise = (double)seed * 2.328e-10 * 2.0 - 1.0;
            double modTime = primesV120[i] + noise * 4.0; // Main delay mod

            double d = fdn[i].readLinear(modTime);

            for(int s=0; s<3; ++s) {
                double apD = nested[i][s].readLinear(nestedPrimesV120[s]);
                double apW = d + 0.618 * apD;
                nested[i][s].write(apW);
                d = apD - 0.618 * apW;
            }

            d *= decayCoeff;
            fdn[i].write((double)in[n] + fb[i]);
            fb[i] = d;
            outSum += d;
        }
        out[n] = outSum * 0.125;
    }
}

// [B] V1.2.1 High-End Reverb Approach (Research applied)
// Narrow Range Coprime Delays (1.5x range: 1500 to 2250)
const std::array<int, 16> narrowCoprime = {1499, 1549, 1597, 1657, 1709, 1759, 1811, 1861, 1913, 1973, 2027, 2081, 2131, 2179, 2239, 2287};
// Zero-sum Hadamard Sign Flipping
const std::array<double, 16> signFlipping = {1, -1, 1, -1, -1, 1, -1, 1, 1, 1, -1, -1, -1, -1, 1, 1};
// Fixed Incommensurate Ratios for Nested Allpass
const std::array<double, 3> nestedFixedRatios = {1.0, 0.333, 0.111};

void processV121_HighEnd(const std::vector<float>& in, std::vector<float>& out, double diffusion) {
    std::vector<DelayLine> fdn(16, DelayLine(8192));
    std::vector<std::vector<DelayLine>> nested(16, std::vector<DelayLine>(3, DelayLine(1024)));
    std::array<double, 16> fb = {0};
    double decayCoeff = std::exp(-6.91 / (3.0 * FS));
    
    // Diffusion to Gain scaling only (0.0 to 0.7 max)
    double apGain = diffusion * 0.7; 
    
    double lfoPhase = 0;
    
    for(int n=0; n<in.size(); ++n) {
        lfoPhase += (2.0 * PI * 0.3) / FS; // 0.3Hz slow triangle/sine

        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    double x = fb[j], y = fb[j+h];
                    fb[j] = x + y; fb[j+h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) fb[i] *= 0.25 * signFlipping[i]; // Apply zero-sum sign flipping

        double outSum = 0;
        for(int i=0; i<16; ++i) {
            // Main delay is FIXED (No modulation)
            double d = fdn[i].readHermite(narrowCoprime[i]);

            // Nested Allpass (Modulated slightly, Fixed length ratios)
            double baseAllpass = 150.0 + i * 5.0; // Base ~3ms
            for(int s=0; s<3; ++s) {
                double fixedLen = baseAllpass * nestedFixedRatios[s];
                
                // Micro Excursion (12 samples max) on Allpass only
                double modLfo = std::sin(lfoPhase + i*0.4 + s*1.3);
                double modTime = fixedLen + modLfo * 12.0; 

                double apD = nested[i][s].readHermite(modTime);
                double apW = d + apGain * apD;
                nested[i][s].write(apW);
                d = apD - apGain * apW;
            }

            d *= decayCoeff;
            fdn[i].write((double)in[n] + fb[i]);
            fb[i] = d;
            outSum += d;
        }
        out[n] = outSum * 0.125;
    }
}

// --- Metrics ---
struct Metrics {
    double roughness;
    double metallic; 
    double density;
};

Metrics analyzeOutput(const std::vector<float>& out, double fc) {
    Metrics m = {0,0,0};
    BiquadBPF bpf; bpf.set(fc, 2.0);
    
    std::vector<double> env(NUM_SAMPLES, 0.0);
    double sumSq = 0;

    for(int n=0; n<NUM_SAMPLES; ++n) {
        double v = bpf.process(out[n]);
        if(n > FS) {
            sumSq += v*v;
            env[n] = std::abs(v);
        }
    }
    
    double rms = std::sqrt(sumSq / (NUM_SAMPLES - FS));
    if (rms == 0) return m;

    double diffSqSum = 0;
    for(int i=FS+1; i<NUM_SAMPLES; ++i) {
        double diff = env[i] - env[i-1];
        diffSqSum += diff * diff;
    }
    m.roughness = std::sqrt(diffSqSum / (NUM_SAMPLES - FS)) / rms * 100.0;
    
    double envVar = 0;
    for(int i=FS; i<NUM_SAMPLES; ++i) {
        envVar += (env[i] - rms)*(env[i] - rms);
    }
    m.metallic = std::sqrt(envVar / (NUM_SAMPLES - FS)) / rms * 100.0;

    int denseCount = 0;
    int window = FS / 20; 
    for(int n=FS; n<FS*2; n+=window) {
        double wSumSq = 0;
        for(int k=0; k<window; ++k) wSumSq += std::pow(bpf.process(out[n+k]), 2.0);
        double wStdDev = std::sqrt(wSumSq / window);
        int localPeaks = 0;
        for(int k=0; k<window; ++k) if(std::abs(bpf.process(out[n+k])) > wStdDev) localPeaks++;
        double localDensity = (localPeaks / static_cast<double>(window)) / 0.317; 
        if(localDensity > 1.0) localDensity = 1.0;
        denseCount += localDensity * 100.0; 
    }
    m.density = denseCount / (FS / window); 

    return m;
}

int main() {
    double freqs[10] = {31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    std::vector<float> in(NUM_SAMPLES, 0.0f); in[0] = 1.0f;
    std::vector<float> outOld(NUM_SAMPLES, 0.0f); 
    std::vector<float> outNew_Diff1(NUM_SAMPLES, 0.0f); 
    std::vector<float> outNew_Diff0(NUM_SAMPLES, 0.0f); 

    processV120(in, outOld);
    processV121_HighEnd(in, outNew_Diff1, 1.0); // Diff 1.0 (Dense)
    processV121_HighEnd(in, outNew_Diff0, 0.0); // Diff 0.0 (Sparse)

    std::cout << "| Freq (Hz) | ① Noise(Roughness) (低=良) <br> 旧V1.2.0 vs Lexicon/Jot方式 | ② Metallic(Modal Var) (低=良) <br> 旧V1.2.0 vs Lexicon/Jot方式 | ③ Diffusion (%) <br> Lex/Jot方式 Diff 0 vs Diff 1 |\n";
    std::cout << "|-----------|---------------------------------------------------------|------------------------------------------------------------|----------------------------------------------|\n";
    
    for(int i=0; i<10; ++i) {
        Metrics mOld = analyzeOutput(outOld, freqs[i]);
        Metrics mNew1 = analyzeOutput(outNew_Diff1, freqs[i]);
        Metrics mNew0 = analyzeOutput(outNew_Diff0, freqs[i]);
        
        std::string metalResult = "";
        double imp = ((mOld.metallic - mNew1.metallic) / mOld.metallic) * 100.0;
        if (freqs[i] >= 4000) {
            if (imp >= 10.0) metalResult = " (クリア!)";
            else metalResult = " (未達)";
        }
        
        std::string noiseResult = "";
        if (mNew1.roughness < mOld.roughness) noiseResult = " (改善)";
        else noiseResult = " (悪化)";

        std::cout << "| " << std::fixed << std::setprecision(0) << freqs[i] << " | "
                  << std::fixed << std::setprecision(2) << mOld.roughness << " vs " << mNew1.roughness << noiseResult << " | "
                  << std::fixed << std::setprecision(2) << mOld.metallic << " vs " << mNew1.metallic << " [" << imp << "%]" << metalResult << " | "
                  << std::fixed << std::setprecision(1) << mNew0.density << "% vs " << mNew1.density << "% |\n";
    }
    return 0;
}
