#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>

// Include the ACTUAL SDN Engine header
#include "../Source/DSP/SDNEngine.h"

using namespace FDNReverb;

const double SAMPLE_RATE = 48000.0;
const int IR_SAMPLES = (int)(SAMPLE_RATE * 2.0);
const double TEST_DUR = 0.1;

struct RoomParam {
    std::string name;
    float w, d, h;
    float sx, sy, sz;
    float lx, ly, lz;
};

std::vector<RoomParam> ROOMS = {
    {"Room", 4.6f, 7.4f, 2.89f, 1.0f, 1.5f, 1.2f, 3.5f, 5.5f, 1.2f},
    {"Hall", 13.5f, 27.0f, 10.8f, 3.0f, 5.0f, 1.7f, 10.0f, 20.0f, 1.7f},
    {"Plate", 2.0f, 1.0f, 0.001f, 0.3f, 0.7f, 0.0005f, 1.5f, 0.4f, 0.0005f},
    {"Spring", 0.3f, 0.3f, 0.01f, 0.0f, 0.15f, 0.005f, 0.3f, 0.15f, 0.005f},
    {"Goldfoil", 0.27f, 0.29f, 0.00002f, 0.05f, 0.14f, 0.00001f, 0.20f, 0.10f, 0.00001f},
    {"Inchindown", 9.0f, 237.0f, 13.5f, 4.5f, 10.0f, 6.0f, 4.5f, 50.0f, 6.0f}
};

std::vector<float> FREQS = {40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 15000};
std::vector<std::string> WAVES = {"Sine", "Saw", "Square", "Sync", "FM"};

void generate_signal(const std::string& wave, float freq, std::vector<float>& sig) {
    int n = (int)(SAMPLE_RATE * TEST_DUR);
    sig.resize(n);
    for (int i = 0; i < n; ++i) {
        float t = i / (float)SAMPLE_RATE;
        float v = 0.0f;
        if (wave == "Sine") {
            v = std::sin(2.0f * 3.14159265f * freq * t);
        } else if (wave == "Saw") {
            v = 2.0f * std::fmod(freq * t, 1.0f) - 1.0f;
        } else if (wave == "Square") {
            v = std::sin(2.0f * 3.14159265f * freq * t) > 0 ? 1.0f : -1.0f;
        } else if (wave == "Sync") {
            v = std::sin(2.0f * 3.14159265f * freq * t) * std::sin(2.0f * 3.14159265f * freq * 2.7f * t);
        } else if (wave == "FM") {
            v = std::sin(2.0f * 3.14159265f * freq * t + 3.0f * std::sin(2.0f * 3.14159265f * freq * 1.414f * t));
        }
        sig[i] = v * 0.5f; // -6dBFS
    }
}

// Dummy DelayMemoryPool for standalone compilation
namespace FDNReverb {
    class DelayMemoryPool {
    public:
        std::vector<float> buffer;
        size_t offset = 0;
        void allocate(size_t size) { buffer.resize(size, 0.0f); offset = 0; }
        float* requestMemory(size_t size, int& outMask) {
            size_t p2 = 1; while (p2 < size) p2 *= 2;
            float* ptr = buffer.data() + offset;
            outMask = p2 - 1;
            offset += p2;
            return ptr;
        }
        void clear() { std::fill(buffer.begin(), buffer.end(), 0.0f); }
    };
}

int main() {
    std::cout << "SDNEngine C++ Runner started...\n";
    
    // Allocate memory
    FDNReverb::DelayMemoryPool pool;
    pool.allocate(48000 * 20); // 20 seconds total buffer
    
    SDNShoebox3D sdn;
    sdn.prepare(SAMPLE_RATE, 256);
    
    // Init delays
    int mask;
    for(int i=0; i<SDNShoebox3D::NUM_NODES; ++i) {
        float* ptr = pool.requestMemory(48000 * 2, mask);
        sdn.delayLines[i].init(ptr, mask);
    }

    std::ofstream outfile("ValidationTools/processed_audio.bin", std::ios::binary);
    
    int count = 0;
    for (const auto& room : ROOMS) {
        sdn.updateGeometry(room.w, room.d, room.h, room.sx, room.sy, room.sz, room.lx, room.ly, room.lz);
        
        for (float freq : FREQS) {
            for (const auto& wave : WAVES) {
                sdn.reset();
                pool.clear();
                
                std::vector<float> sig;
                generate_signal(wave, freq, sig);
                
                int total_len = sig.size() + IR_SAMPLES;
                std::vector<float> outL(total_len, 0.0f);
                std::vector<float> outR(total_len, 0.0f);
                
                for (int i = 0; i < total_len; ++i) {
                    float inVal = (i < sig.size()) ? sig[i] : 0.0f;
                    // Impulse to trigger full broadband tail for density measurement
                    if (i == 0) inVal += 0.5f; 
                    
                    float oL = 0, oR = 0;
                    sdn.processOneSample(inVal, inVal, oL, oR);
                    outL[i] = oL;
                    outR[i] = oR;
                }
                
                // Write to binary file
                outfile.write(reinterpret_cast<const char*>(outL.data()), total_len * sizeof(float));
                outfile.write(reinterpret_cast<const char*>(outR.data()), total_len * sizeof(float));
                
                count++;
                if (count % 50 == 0) std::cout << "Processed " << count << "/1200\n";
            }
        }
    }
    
    outfile.close();
    std::cout << "Done writing 1200 test cases to processed_audio.bin\n";
    return 0;
}
