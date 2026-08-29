import numpy as np
from scipy import signal
import struct
import sys

FS = 48000
NUM_SAMPLES = FS * 3

def load_raw(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    return np.array(struct.unpack('f' * NUM_SAMPLES, data))

def get_metrics_band(sig, fc):
    w0 = 2.0 * np.pi * fc / FS
    b, a = signal.iirpeak(w0 / np.pi, 1.414)
    band_sig = signal.lfilter(b, a, sig)
    
    tail_sig = band_sig[int(FS * 1.5):]
    if np.sum(np.abs(tail_sig)) < 1e-6: return 0, 0
    
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
    # Only 0 to 150ms
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

print("### 【①ノイズ】＆【②金属音】全波形×全帯域")
print("| 波形 | Freq(Hz) | ① Noise(ザラつき/低=良) (旧 vs 新) | ② SFM(金属音無さ/高=良) (旧 vs 新) |")
print("|---|---|---|---|")
for w in waves:
    y_old = load_raw(f"test_matrix_build/old_{w}.raw")
    y_new = load_raw(f"test_matrix_build/new_{w}.raw")
    for fc in freqs:
        r_old, s_old = get_metrics_band(y_old, fc)
        r_new, s_new = get_metrics_band(y_new, fc)
        
        n_res = "改善" if r_new < r_old else "悪化"
        m_res = "未達"
        if s_old > 0:
            imp = (s_new - s_old) / s_old * 100.0
            if imp >= 10.0: m_res = f"+{imp:.1f}%クリア!"
            else: m_res = f"{imp:.1f}%"
        
        print(f"| {w} | {fc} | {r_old:.2f} vs {r_new:.2f} ({n_res}) | {s_old:.2f} vs {s_new:.2f} ({m_res}) |")

print("\n### 【③ Diffusion】(Sine波 150ms 初期エコー密度)")
d_old = get_diffusion(load_raw("test_matrix_build/old_Sine.raw"))
d_new1 = get_diffusion(load_raw("test_matrix_build/new_Sine.raw"))
d_new0 = get_diffusion(load_raw("test_matrix_build/new_Sine_Diff0.raw"))
print(f"- 新V1.2.1 Diff=0 (Sparse) : {d_new0:.1f}%")
print(f"- 新V1.2.1 Diff=1 (Dense)  : {d_new1:.1f}%")
print("- ※旧V1.2.0は Diff ノブ連動なし")

print("\n### 【④ ModAmt/Rate】(Sine波 ピッチ揺らぎ量)")
m_old = get_mod(load_raw("test_matrix_build/old_Sine.raw"))
m_new1 = get_mod(load_raw("test_matrix_build/new_Sine.raw"))
m_new0 = get_mod(load_raw("test_matrix_build/new_Sine_Mod0.raw"))
print(f"- 旧V1.2.0 (ノイズ変調) : {m_old:.2f}%")
print(f"- 新V1.2.1 Mod=0 (揺れ無) : {m_new0:.2f}%")
print(f"- 新V1.2.1 Mod=1 (多相正弦): {m_new1:.2f}%")

print("\n### 【⑤ CPU 負荷】(C++ 3秒処理時間)")
with open("test_matrix_build/cpu_results.txt", "r") as f:
    lines = f.readlines()
    print(f"- 旧V1.2.0 : {float(lines[0]):.2f} ms")
    print(f"- 新V1.2.1 : {float(lines[1]):.2f} ms")
