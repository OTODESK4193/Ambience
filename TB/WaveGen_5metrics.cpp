#include <iostream>
#include <vector>
#include <cmath>
#include <array>
#include <fstream>
#include <chrono>

constexpr double PI = 3.14159265358979323846;
constexpr int FS = 48000;
constexpr int NUM_SAMPLES = FS * 3; 

struct DelayLine {
    std::vector<double> buffer;
    int writePos = 0, mask;
    DelayLine(int maxLen) {
        int p = 1; while(p < maxLen) p *= 2;
        buffer.assign(p, 0.0); mask = p - 1;
    }
    inline void write(double v) { buffer[writePos] = v; writePos = (writePos + 1) & mask; }
    inline double readLinear(double delay) const {
        int id = static_cast<int>(delay); double frac = delay - id;
        double y0 = buffer[(writePos - id) & mask]; double y1 = buffer[(writePos - id - 1) & mask];
        return y0 + frac * (y1 - y0);
    }
    inline double readHermite(double delay) const {
        int id = static_cast<int>(delay); double frac = delay - id;
        double y0 = buffer[(writePos - id + 1) & mask]; double y1 = buffer[(writePos - id) & mask];
        double y2 = buffer[(writePos - id - 1) & mask]; double y3 = buffer[(writePos - id - 2) & mask];
        double c0 = y1; double c1 = 0.5 * (y2 - y0);
        double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3; double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};

struct OnePole {
    double z = 0, a, b;
    void set(double fc) {
        b = std::exp(-2.0 * PI * fc / FS);
        a = 1.0 - b;
    }
    inline double process(double x) {
        z = x * a + z * b;
        return z;
    }
};

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

const std::array<int, 16> primesV120 = {1009, 1151, 1301, 1451, 1601, 1753, 1901, 2053, 2203, 2351, 2503, 2657, 2801, 2953, 3109, 3251};
const std::array<int, 3> nestedPrimesV120 = {73, 109, 163};

void processV120(const std::vector<double>& in, std::vector<float>& out) {
    std::vector<DelayLine> fdn(16, DelayLine(8192));
    std::vector<std::vector<DelayLine>> nested(16, std::vector<DelayLine>(3, DelayLine(1024)));
    std::array<double, 16> fb = {0};
    double decayCoeff = std::exp(-6.91 / (3.0 * FS));
    uint32_t seed = 12345;
    for(int n=0; n<NUM_SAMPLES; ++n) {
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
            double modTime = primesV120[i] + noise * 4.0; 
            double d = fdn[i].readLinear(modTime);
            for(int s=0; s<3; ++s) {
                double apD = nested[i][s].readLinear(nestedPrimesV120[s]);
                double apW = d + 0.618 * apD;
                nested[i][s].write(apW);
                d = apD - 0.618 * apW;
            }
            d *= decayCoeff;
            fdn[i].write(in[n] + fb[i]);
            fb[i] = d;
            outSum += d;
        }
        out[n] = outSum * 0.125;
    }
}

const std::array<double, 3> nestedFixedRatios = {1.0, 0.381966, 0.145898}; 

void processV121_v4(const std::vector<double>& in, std::vector<float>& out, double diffusion, double modAmt) {
    std::vector<DelayLine> fdn(16, DelayLine(16384));
    std::vector<std::vector<DelayLine>> nested(16, std::vector<DelayLine>(3, DelayLine(1024)));
    std::array<double, 16> fb = {0};
    std::vector<OnePole> lpf(16);
    for(int i=0; i<16; ++i) lpf[i].set(4500.0);
    
    double decayCoeff = std::exp(-6.91 / (3.0 * FS));
    double apGain = diffusion * 0.65; 
    double lfoTime = 0;
    
    std::array<double, 16> expDelays;
    for(int i=0; i<16; ++i) expDelays[i] = 1000.0 * std::pow(1.15, (double)i);
    
    // Diffusion Sparse Input
    std::array<double, 16> inGains = {0};
    for(int i=0; i<16; ++i) {
        if (diffusion < 0.1) {
            inGains[i] = (i % 4 == 0) ? 4.0 : 0.0;
        } else {
            inGains[i] = 1.0;
        }
    }
    
    for(int n=0; n<NUM_SAMPLES; ++n) {
        lfoTime += 1.0 / FS; 
        
        double sumFb = 0;
        for(int i=0; i<16; ++i) sumFb += fb[i];
        double householderSub = (2.0 / 16.0) * sumFb;
        for (int i = 0; i < 16; ++i) fb[i] = fb[i] - householderSub;

        double outSum = 0;
        for(int i=0; i<16; ++i) {
            double fracJitter = std::fmod((double)i * 1.6180339887, 1.0) * 13.0; 
            double mainTime = expDelays[i] + fracJitter;
            double d = fdn[i].readHermite(mainTime);

            double baseAllpass = 150.0 + i * 7.0; 
            for(int s=0; s<3; ++s) {
                double fixedLen = baseAllpass * nestedFixedRatios[s];
                double phaseOffset = (double)i * (2.0 * PI / 16.0);
                double modLfo = std::sin(2.0*PI*0.13*lfoTime + phaseOffset) * 0.5 + 
                                std::sin(2.0*PI*0.27*lfoTime + phaseOffset*2.0) * 0.3 + 
                                std::sin(2.0*PI*0.41*lfoTime + phaseOffset*3.0) * 0.2;
                double modTime = fixedLen + modLfo * (modAmt * 8.0); 
                
                double apD = nested[i][s].readHermite(modTime);
                double apW = d + apGain * apD;
                nested[i][s].write(apW);
                d = apD - apGain * apW;
            }
            
            d = lpf[i].process(d);
            d *= decayCoeff;
            fdn[i].write(in[n] * inGains[i] + fb[i]);
            fb[i] = d;
            outSum += d;
        }
        out[n] = outSum * 0.125;
    }
}

void writeRaw(const std::string& name, const std::vector<float>& data) {
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
}

int main() {
    std::vector<double> inSine(NUM_SAMPLES), inSaw(NUM_SAMPLES), inSquare(NUM_SAMPLES), inSync(NUM_SAMPLES), inFM(NUM_SAMPLES);
    for(int n=0; n<NUM_SAMPLES; ++n) {
        double t = (double)n / FS;
        double env = getEnv(n) * 0.5;
        inSine[n] = getSine(t) * env;
        inSaw[n] = getSaw(t) * env;
        inSquare[n] = getSquare(t) * env;
        inSync[n] = getSync(t) * env;
        inFM[n] = getFM(t) * env;
    }

    std::vector<float> out(NUM_SAMPLES);
    
    // CPU Time Measurement
    auto startV120 = std::chrono::high_resolution_clock::now();
    processV120(inSine, out); 
    auto endV120 = std::chrono::high_resolution_clock::now();
    
    auto startV121 = std::chrono::high_resolution_clock::now();
    processV121_v4(inSine, out, 1.0, 1.0); 
    auto endV121 = std::chrono::high_resolution_clock::now();
    
    double cpuV120 = std::chrono::duration<double, std::milli>(endV120 - startV120).count();
    double cpuV121 = std::chrono::duration<double, std::milli>(endV121 - startV121).count();
    
    std::ofstream cpuFile("cpu_results.txt");
    cpuFile << cpuV120 << "\n" << cpuV121 << "\n";
    cpuFile.close();
    
    // Generate all files
    writeRaw("old_Sine.raw", out);
    processV121_v4(inSine, out, 1.0, 1.0); writeRaw("new_Sine.raw", out);
    
    processV120(inSaw, out); writeRaw("old_Saw.raw", out);
    processV121_v4(inSaw, out, 1.0, 1.0); writeRaw("new_Saw.raw", out);
    
    processV120(inSquare, out); writeRaw("old_Square.raw", out);
    processV121_v4(inSquare, out, 1.0, 1.0); writeRaw("new_Square.raw", out);
    
    processV120(inSync, out); writeRaw("old_Sync.raw", out);
    processV121_v4(inSync, out, 1.0, 1.0); writeRaw("new_Sync.raw", out);
    
    processV120(inFM, out); writeRaw("old_FM.raw", out);
    processV121_v4(inFM, out, 1.0, 1.0); writeRaw("new_FM.raw", out);
    
    // Test Mod = 0
    processV121_v4(inSine, out, 1.0, 0.0); writeRaw("new_Sine_Mod0.raw", out);
    
    // Test Diff = 0
    processV121_v4(inSine, out, 0.0, 1.0); writeRaw("new_Sine_Diff0.raw", out);
    
    return 0;
}
