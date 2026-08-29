#include <iostream>
#include <vector>
#include <cmath>
#include <array>
#include <fstream>

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

const std::array<int, 16> primesV120 = {1009, 1151, 1301, 1451, 1601, 1753, 1901, 2053, 2203, 2351, 2503, 2657, 2801, 2953, 3109, 3251};
const std::array<int, 3> nestedPrimesV120 = {73, 109, 163};

void processV120(std::vector<float>& out) {
    std::vector<DelayLine> fdn(16, DelayLine(8192));
    std::vector<std::vector<DelayLine>> nested(16, std::vector<DelayLine>(3, DelayLine(1024)));
    std::array<double, 16> fb = {0};
    double decayCoeff = std::exp(-6.91 / (3.0 * FS));
    uint32_t seed = 12345;
    for(int n=0; n<NUM_SAMPLES; ++n) {
        double in_sample = (n==0) ? 1.0 : 0.0;
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
            fdn[i].write(in_sample + fb[i]);
            fb[i] = d;
            outSum += d;
        }
        out[n] = outSum * 0.125;
    }
}

const std::array<int, 16> narrowCoprime = {1499, 1549, 1597, 1657, 1709, 1759, 1811, 1861, 1913, 1973, 2027, 2081, 2131, 2179, 2239, 2287};
const std::array<double, 16> signFlipping = {1, -1, 1, -1, -1, 1, -1, 1, 1, 1, -1, -1, -1, -1, 1, 1};
const std::array<double, 3> nestedFixedRatios = {1.0, 0.333, 0.111};

void processV121_HighEnd(std::vector<float>& out, double diffusion) {
    std::vector<DelayLine> fdn(16, DelayLine(8192));
    std::vector<std::vector<DelayLine>> nested(16, std::vector<DelayLine>(3, DelayLine(1024)));
    std::array<double, 16> fb = {0};
    double decayCoeff = std::exp(-6.91 / (3.0 * FS));
    double apGain = diffusion * 0.7; 
    double lfoPhase = 0;
    
    for(int n=0; n<NUM_SAMPLES; ++n) {
        double in_sample = (n==0) ? 1.0 : 0.0;
        lfoPhase += (2.0 * PI * 0.3) / FS; 

        for (int h = 1; h < 16; h *= 2) {
            for (int i = 0; i < 16; i += h * 2) {
                for (int j = i; j < i + h; ++j) {
                    double x = fb[j], y = fb[j+h];
                    fb[j] = x + y; fb[j+h] = x - y;
                }
            }
        }
        for (int i = 0; i < 16; ++i) fb[i] *= 0.25 * signFlipping[i];

        double outSum = 0;
        for(int i=0; i<16; ++i) {
            double d = fdn[i].readHermite(narrowCoprime[i]);
            double baseAllpass = 150.0 + i * 5.0; 
            for(int s=0; s<3; ++s) {
                double fixedLen = baseAllpass * nestedFixedRatios[s];
                double modLfo = std::sin(lfoPhase + i*0.4 + s*1.3);
                double modTime = fixedLen + modLfo * 12.0; 
                double apD = nested[i][s].readHermite(modTime);
                double apW = d + apGain * apD;
                nested[i][s].write(apW);
                d = apD - apGain * apW;
            }
            d *= decayCoeff;
            fdn[i].write(in_sample + fb[i]);
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
    std::vector<float> outOld(NUM_SAMPLES, 0.0f); 
    std::vector<float> outNew1(NUM_SAMPLES, 0.0f); 
    std::vector<float> outNew0(NUM_SAMPLES, 0.0f); 
    
    processV120(outOld);
    processV121_HighEnd(outNew1, 1.0);
    processV121_HighEnd(outNew0, 0.0);
    
    writeRaw("out_old.raw", outOld);
    writeRaw("out_new1.raw", outNew1);
    writeRaw("out_new0.raw", outNew0);
    return 0;
}
