import numpy as np
from scipy import signal
import struct

FS = 48000
NUM_SAMPLES = FS * 3

def load_raw(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    return np.array(struct.unpack('f' * NUM_SAMPLES, data))

def get_metrics(sig):
    # Analyze the tail only (from 1.5s to 3.0s)
    # The source stopped at 1.0s, so 1.5s ensures we only measure the pure reverb tail without source bleeding.
    tail_sig = sig[int(FS * 1.5):]
    if np.sum(np.abs(tail_sig)) == 0: return 0, 0
    
    # Noise (Roughness): AM fluctuation index of envelope
    env = np.abs(signal.hilbert(tail_sig))
    diff_env = np.diff(env)
    roughness = np.sqrt(np.mean(diff_env**2)) / (np.mean(env) + 1e-9) * 100.0
    
    # Metallic (SFM): Full spectrum flatness
    f, Pxx = signal.periodogram(tail_sig, fs=FS)
    # limit to 100Hz - 16kHz for relevant metallic ringing
    idx = np.where((f >= 100) & (f <= 16000))[0]
    Pxx_band = Pxx[idx] + 1e-12
    geom_mean = np.exp(np.mean(np.log(Pxx_band)))
    arith_mean = np.mean(Pxx_band)
    sfm = geom_mean / arith_mean * 100.0 
    
    return roughness, sfm

waves = ["Sine", "Saw", "Square", "Sync", "FM"]

print("| テスト波形 | ① テール・ノイズ(ザラつき) (低いほど良) <br> 旧V1.2.0 vs 新V1.2.1 | ② テール・金属音無さ(SFM) (高いほど良) <br> 旧V1.2.0 vs 新V1.2.1 |")
print("|------------|---------------------------------------------------------|---------------------------------------------------------|")

for w in waves:
    y_old = load_raw(f"test_matrix_build/old_{w}.raw")
    y_new = load_raw(f"test_matrix_build/new_{w}.raw")
    
    r_old, s_old = get_metrics(y_old)
    r_new, s_new = get_metrics(y_new)
    
    noise_res = " (悪化)"
    if r_new < r_old: noise_res = " (改善)"
    
    metal_res = ""
    if s_old > 0:
        imp = ((s_new - s_old) / s_old) * 100.0
        if imp >= 10.0: metal_res = f" [+{imp:.1f}% クリア!]"
        elif imp > 0: metal_res = f" [+{imp:.1f}% 未達]"
        else: metal_res = f" [{imp:.1f}% 悪化]"
        
    print(f"| **{w}** | {r_old:.2f} vs {r_new:.2f}{noise_res} | {s_old:.2f} vs {s_new:.2f}{metal_res} |")
