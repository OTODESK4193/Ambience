#pragma once

#include <vector>
#include <cmath>
#include <complex>
#include <numeric>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>

namespace FDNReverb {
namespace TestHarness {

constexpr double PI = 3.14159265358979323846;

class SignalGenerator {
public:
    static std::vector<float> generateSine(double freq, double sampleRate, size_t numSamples) {
        std::vector<float> out(numSamples);
        double phaseInc = 2.0 * PI * freq / sampleRate;
        for (size_t i = 0; i < numSamples; ++i) {
            out[i] = static_cast<float>(std::sin(i * phaseInc));
        }
        return out;
    }

    static std::vector<float> generateTriangle(double freq, double sampleRate, size_t numSamples) {
        std::vector<float> out(numSamples);
        double period = sampleRate / freq;
        for (size_t i = 0; i < numSamples; ++i) {
            double phase = std::fmod(i, period) / period;
            out[i] = static_cast<float>(2.0 * std::abs(2.0 * phase - 1.0) - 1.0);
        }
        return out;
    }

    static std::vector<float> generateSquare(double freq, double sampleRate, size_t numSamples) {
        std::vector<float> out(numSamples);
        double period = sampleRate / freq;
        for (size_t i = 0; i < numSamples; ++i) {
            double phase = std::fmod(i, period) / period;
            out[i] = (phase < 0.5) ? 1.0f : -1.0f;
        }
        return out;
    }

    static std::vector<float> generateSawtooth(double freq, double sampleRate, size_t numSamples) {
        std::vector<float> out(numSamples);
        double period = sampleRate / freq;
        for (size_t i = 0; i < numSamples; ++i) {
            double phase = std::fmod(i, period) / period;
            out[i] = static_cast<float>(2.0 * phase - 1.0);
        }
        return out;
    }

    static std::vector<float> generateWhiteNoise(size_t numSamples) {
        std::vector<float> out(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            out[i] = (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
        }
        return out;
    }

    static std::vector<float> generatePinkNoise(size_t numSamples) {
        std::vector<float> out(numSamples);
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        for (size_t i = 0; i < numSamples; ++i) {
            float white = (static_cast<float>(std::rand()) / RAND_MAX) * 2.0f - 1.0f;
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            out[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            out[i] *= 0.11f; // Normalize roughly to [-1, 1]
            b6 = white * 0.115926f;
        }
        return out;
    }

    static std::vector<float> generateImpulse(size_t numSamples) {
        std::vector<float> out(numSamples, 0.0f);
        if (numSamples > 0) out[0] = 1.0f;
        return out;
    }

    static std::vector<float> generateSineSweep(double startFreq, double endFreq, double sampleRate, size_t numSamples) {
        std::vector<float> out(numSamples);
        double duration = static_cast<double>(numSamples) / sampleRate;
        for (size_t i = 0; i < numSamples; ++i) {
            double t = i / sampleRate;
            double phase = 2.0 * PI * startFreq * duration / std::log(endFreq / startFreq) * 
                           (std::pow(endFreq / startFreq, t / duration) - 1.0);
            out[i] = static_cast<float>(std::sin(phase));
        }
        return out;
    }
};

class SimpleFFT {
public:
    static void compute(std::vector<std::complex<float>>& data, bool inverse = false) {
        size_t n = data.size();
        if (n <= 1 || (n & (n - 1)) != 0) return; // Must be power of 2

        size_t j = 0;
        for (size_t i = 1; i < n; ++i) {
            size_t bit = n >> 1;
            while (j & bit) {
                j ^= bit;
                bit >>= 1;
            }
            j ^= bit;
            if (i < j) {
                std::swap(data[i], data[j]);
            }
        }

        for (size_t len = 2; len <= n; len <<= 1) {
            float angle = 2.0f * static_cast<float>(PI) / len * (inverse ? 1.0f : -1.0f);
            std::complex<float> wlen(std::cos(angle), std::sin(angle));
            for (size_t i = 0; i < n; i += len) {
                std::complex<float> w(1.0f, 0.0f);
                for (size_t k = 0; k < len / 2; ++k) {
                    std::complex<float> u = data[i + k];
                    std::complex<float> v = data[i + k + len / 2] * w;
                    data[i + k] = u + v;
                    data[i + k + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }

        if (inverse) {
            for (auto& x : data) {
                x /= static_cast<float>(n);
            }
        }
    }
};

class Analyzer {
public:
    static float getRMS(const std::vector<float>& data) {
        if (data.empty()) return -100.0f;
        float sumSq = 0.0f;
        for (float val : data) sumSq += val * val;
        float rms = std::sqrt(sumSq / data.size());
        return rms > 1e-10f ? 20.0f * std::log10(rms) : -100.0f;
    }

    static float getPeak(const std::vector<float>& data) {
        if (data.empty()) return -100.0f;
        float maxVal = 0.0f;
        for (float val : data) {
            if (std::abs(val) > maxVal) maxVal = std::abs(val);
        }
        return maxVal > 1e-10f ? 20.0f * std::log10(maxVal) : -100.0f;
    }

    static float getDCOffset(const std::vector<float>& data) {
        if (data.empty()) return 0.0f;
        float sum = 0.0f;
        for (float val : data) sum += val;
        return sum / data.size();
    }

    static bool detectClipping(const std::vector<float>& data, float threshold = 0.999f) {
        int clipCount = 0;
        for (float val : data) {
            if (std::abs(val) >= threshold) {
                clipCount++;
                if (clipCount >= 3) return true; // 連続または複数回のクリッピング
            } else {
                clipCount = 0;
            }
        }
        return false;
    }

    struct THDResult {
        float thd; // %
        float thd_n; // %
    };

    static THDResult measureTHD(const std::vector<float>& data, double fundamentalFreq, double sampleRate) {
        size_t n = 1;
        while (n < data.size()) n <<= 1;
        n >>= 1; // n is power of 2
        
        std::vector<std::complex<float>> fftData(n);
        for (size_t i = 0; i < n; ++i) fftData[i] = {data[i], 0.0f};
        
        SimpleFFT::compute(fftData);
        
        float fundamentalEnergy = 0.0f;
        float harmonicEnergy = 0.0f;
        float totalEnergy = 0.0f;
        
        int fundamentalBin = static_cast<int>(std::round(fundamentalFreq * n / sampleRate));
        
        for (size_t i = 1; i < n / 2; ++i) {
            float energy = std::norm(fftData[i]);
            totalEnergy += energy;
            
            // Allow small bin spread
            if (std::abs(static_cast<int>(i) - fundamentalBin) <= 2) {
                fundamentalEnergy += energy;
            } else {
                // Check if it's a harmonic
                bool isHarmonic = false;
                for (int h = 2; h < 20; ++h) {
                    if (std::abs(static_cast<int>(i) - fundamentalBin * h) <= 2) {
                        isHarmonic = true;
                        break;
                    }
                }
                if (isHarmonic) harmonicEnergy += energy;
            }
        }
        
        float noiseEnergy = totalEnergy - fundamentalEnergy - harmonicEnergy;
        if (noiseEnergy < 0.0f) noiseEnergy = 0.0f;
        
        THDResult res = {0.0f, 0.0f};
        if (fundamentalEnergy > 0.0f) {
            res.thd = std::sqrt(harmonicEnergy / fundamentalEnergy) * 100.0f;
            res.thd_n = std::sqrt((harmonicEnergy + noiseEnergy) / fundamentalEnergy) * 100.0f;
        }
        return res;
    }

    static float measureNoiseFloor(const std::vector<float>& data) {
        return getRMS(data);
    }

    struct Resonance {
        float frequency;
        float magnitude;
    };

    static std::vector<Resonance> detectMetallicResonance(const std::vector<float>& data, double sampleRate) {
        size_t n = 1;
        while (n < data.size()) n <<= 1;
        n >>= 1; 
        if (n > 65536) n = 65536; // Limit FFT size
        
        std::vector<std::complex<float>> fftData(n);
        for (size_t i = 0; i < n; ++i) {
            // Hanning window
            float window = 0.5f * (1.0f - std::cos(2.0f * PI * i / (n - 1)));
            fftData[i] = {data[i] * window, 0.0f};
        }
        
        SimpleFFT::compute(fftData);
        
        std::vector<float> mag(n / 2);
        float meanMag = 0.0f;
        for (size_t i = 0; i < n / 2; ++i) {
            mag[i] = std::abs(fftData[i]);
            meanMag += mag[i];
        }
        meanMag /= (n / 2);
        
        std::vector<Resonance> resonances;
        for (size_t i = 2; i < n / 2 - 2; ++i) {
            if (mag[i] > mag[i-1] && mag[i] > mag[i+1]) { // Local peak
                if (mag[i] > meanMag * 10.0f) { // Significant prominence
                    float freq = static_cast<float>(i * sampleRate / n);
                    if (freq > 500.0f) { // Usually high frequency peaks are metallic
                        resonances.push_back({freq, 20.0f * std::log10(mag[i] + 1e-12f)});
                    }
                }
            }
        }
        
        std::sort(resonances.begin(), resonances.end(), [](const Resonance& a, const Resonance& b) {
            return a.magnitude > b.magnitude;
        });
        
        if (resonances.size() > 5) resonances.resize(5); // Keep top 5
        return resonances;
    }

    static std::vector<float> get10BandEnergy(const std::vector<float>& data, double sampleRate) {
        size_t n = 1;
        while (n < data.size()) n <<= 1;
        n >>= 1;
        
        std::vector<std::complex<float>> fftData(n);
        for (size_t i = 0; i < n; ++i) fftData[i] = {data[i], 0.0f};
        
        SimpleFFT::compute(fftData);
        
        std::array<float, 10> centerFreqs = {31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};
        std::vector<float> bandsEnergy(10, 0.0f);
        
        for (size_t i = 1; i < n / 2; ++i) {
            float freq = static_cast<float>(i * sampleRate / n);
            float energy = std::norm(fftData[i]);
            
            // Find closest band
            int bestBand = 0;
            float minDist = std::abs(std::log2(freq / centerFreqs[0]));
            for (int b = 1; b < 10; ++b) {
                float dist = std::abs(std::log2(freq / centerFreqs[b]));
                if (dist < minDist) {
                    minDist = dist;
                    bestBand = b;
                }
            }
            bandsEnergy[bestBand] += energy;
        }
        
        for (float& e : bandsEnergy) {
            e = e > 0 ? 10.0f * std::log10(e) : -100.0f;
        }
        return bandsEnergy;
    }

    static float measureRT60(const std::vector<float>& impulseResponse, double sampleRate) {
        size_t len = impulseResponse.size();
        std::vector<float> energy(len);
        for (size_t i = 0; i < len; ++i) {
            energy[i] = impulseResponse[i] * impulseResponse[i];
        }
        
        std::vector<float> schroeder(len);
        float sum = 0.0f;
        for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
            sum += energy[i];
            schroeder[i] = sum;
        }
        
        float maxVal = schroeder[0];
        if (maxVal <= 0.0f) return 0.0f;
        
        for (size_t i = 0; i < len; ++i) {
            schroeder[i] = 10.0f * std::log10(schroeder[i] / maxVal + 1e-12f);
        }
        
        int t5_idx = -1, t25_idx = -1;
        for (size_t i = 0; i < len; ++i) {
            if (t5_idx == -1 && schroeder[i] <= -5.0f) t5_idx = i;
            if (t25_idx == -1 && schroeder[i] <= -25.0f) t25_idx = i;
        }
        
        if (t5_idx != -1 && t25_idx != -1 && t25_idx > t5_idx) {
            float t20 = static_cast<float>(t25_idx - t5_idx) / sampleRate;
            return t20 * 3.0f;
        }
        return 0.0f;
    }
};

} // namespace TestHarness
} // namespace FDNReverb
