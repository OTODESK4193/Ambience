import numpy as np
from scipy import signal
import struct

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
    
    # Analyze the tail only (from 1.5s to 3.0s)
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

waves = ["Sine", "Saw", "Square", "Sync", "FM"]
freqs = [31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]

print("### 全波形 x 全10帯域 テール解析結果 (Noise & SFM)")
print("| 波形 | Freq (Hz) | ① Noise(ザラつき) (低=良) <br> 旧V1.2.0 vs 新V1.2.1 | ② SFM (金属音の無さ/高=良) <br> 旧V1.2.0 vs 新V1.2.1 |")
print("|-------|-----------|---------------------------------------------------------|---------------------------------------------------------|")

for w in waves:
    y_old = load_raw(f"test_matrix_build/old_{w}.raw")
    y_new = load_raw(f"test_matrix_build/new_{w}.raw")
    
    for fc in freqs:
        r_old, s_old = get_metrics_band(y_old, fc)
        r_new, s_new = get_metrics_band(y_new, fc)
        
        noise_res = " (悪化)"
        if r_new < r_old: noise_res = " (改善)"
        
        metal_res = ""
        if s_old > 0:
            imp = ((s_new - s_old) / s_old) * 100.0
            if imp >= 10.0: metal_res = f" [+{imp:.1f}% クリア!]"
            elif imp > 0: metal_res = f" [+{imp:.1f}% 未達]"
            else: metal_res = f" [{imp:.1f}% 悪化]"
            
        print(f"| **{w}** | {fc:<9} | {r_old:.2f} vs {r_new:.2f}{noise_res} | {s_old:.2f} vs {s_new:.2f}{metal_res} |")
