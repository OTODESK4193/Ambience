import numpy as np
from scipy import signal
import struct
import os

FS = 48000
NUM_SAMPLES = FS * 3

def load_raw(filename):
    if not os.path.exists(filename):
        print(f"Error: {filename} does not exist.")
        return np.zeros(NUM_SAMPLES)
    with open(filename, 'rb') as f:
        data = f.read()
    return np.array(struct.unpack('f' * NUM_SAMPLES, data))

def get_metrics_band(sig, fc):
    w0 = 2.0 * np.pi * fc / FS
    b, a = signal.iirpeak(w0 / np.pi, 1.414)
    band_sig = signal.lfilter(b, a, sig)
    
    tail_sig = band_sig[int(FS * 1.5):]
    if np.sum(np.abs(tail_sig)) < 1e-7: return 0.0, 0.0
    
    env = np.abs(signal.hilbert(tail_sig))
    diff_env = np.diff(env)
    roughness = np.sqrt(np.mean(diff_env**2)) / (np.mean(env) + 1e-9) * 100.0
    
    f, Pxx = signal.periodogram(tail_sig, fs=FS)
    idx = np.where((f >= fc/1.414) & (f <= fc*1.414))[0]
    if len(idx) == 0: return roughness, 0.0
    
    Pxx_band = Pxx[idx] + 1e-12
    geom_mean = np.exp(np.mean(np.log(Pxx_band)))
    arith_mean = np.mean(Pxx_band)
    sfm = geom_mean / arith_mean * 100.0 
    
    return roughness, sfm

def get_diffusion(sig):
    # Analyze first 150ms of attack
    er = sig[0 : int(FS * 0.15)]
    w = int(FS * 0.02)
    k_list = []
    for i in range(0, len(er)-w, w):
        chunk = er[i:i+w]
        std = np.std(chunk)
        if std > 1e-9:
            k4 = np.mean((chunk - np.mean(chunk))**4)
            k_list.append(k4 / (std**4))
    if len(k_list) == 0: return 0.0
    d = 3.0 / np.mean(k_list) * 100.0
    return min(d, 100.0)
    
def get_mod(sig):
    tail = sig[int(FS * 1.5):]
    zc = np.where(np.diff(np.signbit(tail)))[0]
    if len(zc) < 2: return 0.0
    intervals = np.diff(zc)
    return np.std(intervals) / np.mean(intervals) * 100.0

waves = ["Sine", "Saw", "Square", "Sync", "FM"]
freqs = [31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]

print("================================================================================")
print("  Ambience V1.2.0 現行DSPエンジン 実測ベンチマーク結果（5項目・全波形・全帯域）")
print("================================================================================\n")

print("### 【① ノイズ (Roughness)】＆【② 金属音 (SFM)】全波形 × 全10帯域 実測値")
print("| テスト波形 | 周波数 (Hz) | ① ノイズ (Roughness) [低=良] | ② 金属音 (SFM) [高=良] |")
print("|:---|:---|:---|:---|")

for w in waves:
    y = load_raw(f"v120_{w}.raw")
    for fc in freqs:
        r, s = get_metrics_band(y, fc)
        print(f"| **{w}** | {fc:<9} | {r:.3f} | {s:.3f}% |")

print("\n### 【③ Diffusion】(Sine波 150ms 初期エコー密度)")
d_diff0 = get_diffusion(load_raw("v120_Sine_Diff0.raw"))
d_diff1 = get_diffusion(load_raw("v120_Sine_Diff1.raw"))
print(f"- Diffusion = 0.0 (最小) : **{d_diff0:.1f}%**")
print(f"- Diffusion = 1.0 (最大) : **{d_diff1:.1f}%**")
print(f"- 密度変化量 (ΔDensity)  : **{abs(d_diff1 - d_diff0):.1f}%**")

print("\n### 【④ ModAmt / Rate】(Sine波 ピッチゼロクロス変動分散)")
m_mod0 = get_mod(load_raw("v120_Sine_Mod0.raw"))
m_mod1 = get_mod(load_raw("v120_Sine_Mod1.raw"))
print(f"- ModAmount = 0.0 (モジュレーションOFF) : **{m_mod0:.3f}%**")
print(f"- ModAmount = 1.0 (モジュレーション最大) : **{m_mod1:.3f}%**")
print(f"- 変調深度変化量 (ΔModDepth)            : **{abs(m_mod1 - m_mod0):.3f}%**")

print("\n### 【⑤ CPU 負荷】(3秒間ステレオ処理時間)")
if os.path.exists("cpu_v120.txt"):
    with open("cpu_v120.txt", "r") as f:
        cpu_val = float(f.read().strip())
    print(f"- V1.2.0 実測処理時間 : **{cpu_val:.2f} ms** (オーディオ 3000ms に対する負荷率: **{(cpu_val/3000.0)*100.0:.2f}%**)")
else:
    print("- CPUデータなし")
