#include "DSPParams.h"
#include "DSPConstants.h"
#include "UniversalEngine.h"
#include "AlgorithmPresets.h"
#include <iostream>
#include <vector>

using namespace FDNReverb;

int main() {
    UniversalEngine engine;
    DSPParams p;
    p.algorithmIndex = 0; // Room1 (12 taps)
    p.erLevel = 1.0f;
    p.erSolo = true;
    p.wetDB = 0.0f;
    
    engine.prepare(48000.0, 512);
    engine.setParams(p);
    
    // 入力信号: 最初の1サンプルだけ1.0のインパルス
    std::vector<float> inL(512, 0.0f);
    std::vector<float> inR(512, 0.0f);
    std::vector<float> outL(512, 0.0f);
    std::vector<float> outR(512, 0.0f);
    
    inL[0] = 1.0f;
    inR[0] = 1.0f;
    
    engine.processBlock(inL.data(), inR.data(), outL.data(), outR.data(), 512);
    
    float maxER = 0.0f;
    for (int i = 0; i < 512; ++i) {
        if (std::abs(outL[i]) > maxER) maxER = std::abs(outL[i]);
    }
    
    std::cout << "Max ER Level (L): " << maxER << std::endl;
    
    return 0;
}