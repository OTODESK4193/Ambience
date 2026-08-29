import numpy as np
from scipy import signal
import struct

FS = 48000
NUM_SAMPLES = FS * 3

def load_raw(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    return np.array(struct.unpack('f' * NUM_SAMPLES, data))

def get_metrics(sig, fc):
    w0 = 2.0 * np.pi * fc / FS
    b, a = signal.iirpeak(w0 / np.pi, 1.414)
    band_sig = signal.lfilter(b, a, sig)[FS:] 
    
    if np.sum(np.abs(band_sig)) == 0: return 0, 0, 0
    
    env = np.abs(signal.hilbert(band_sig))
    diff_env = np.diff(env)
    roughness = np.sqrt(np.mean(diff_env**2)) / (np.mean(env) + 1e-9) * 100.0
    
    f, Pxx = signal.periodogram(band_sig, fs=FS)
    idx = np.where((f >= fc/1.414) & (f <= fc*1.414))[0]
    Pxx_band = Pxx[idx] + 1e-12
    geom_mean = np.exp(np.mean(np.log(Pxx_band)))
    arith_mean = np.mean(Pxx_band)
    sfm = geom_mean / arith_mean * 100.0 
    
    window = int(FS * 0.02) 
    kurtosis_list = []
    for i in range(0, len(band_sig)-window, window):
        chunk = band_sig[i:i+window]
        std = np.std(chunk)
        if std > 1e-9:
            k4 = np.mean((chunk - np.mean(chunk))**4)
            kurtosis = k4 / (std**4)
            kurtosis_list.append(kurtosis)
            
    if len(kurtosis_list) > 0:
        mean_kurt = np.mean(kurtosis_list)
        density = 3.0 / mean_kurt * 100.0
        if density > 100.0: density = 100.0
    else: density = 0.0
        
    return roughness, sfm, density

y_old = load_raw("test_matrix_build/out_old.raw")
y_new1 = load_raw("test_matrix_build/out_new1.raw")
y_new0 = load_raw("test_matrix_build/out_new0.raw")

freqs = [31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]

print("| Freq (Hz) | ① Noise(Roughness) (低=良) <br> 旧V1.2.0 vs 新方式 | ② SFM (金属音の無さ/高=良) <br> 旧V1.2.0 vs 新方式 | ③ Echo Density (%) <br> Diff 0 vs Diff 1 |")
print("|-----------|--------------------------------------------------|-------------------------------------------------------|-----------------------------------------|")

for fc in freqs:
    r_old, s_old, d_old = get_metrics(y_old, fc)
    r_new1, s_new1, d_new1 = get_metrics(y_new1, fc)
    r_new0, s_new0, d_new0 = get_metrics(y_new0, fc)
    
    noise_res = " (悪化)"
    if r_new1 < r_old: noise_res = " (改善)"
    
    metal_res = ""
    if s_old > 0:
        imp = ((s_new1 - s_old) / s_old) * 100.0
        if fc >= 4000:
            if imp >= 10.0: metal_res = f" [+{imp:.1f}% クリア!]"
            else: metal_res = f" [+{imp:.1f}% 未達]"
        else:
            metal_res = f" [{'+' if imp>0 else ''}{imp:.1f}%]"
    
    print(f"| {fc:<9} | {r_old:.2f} vs {r_new1:.2f}{noise_res} | {s_old:.2f} vs {s_new1:.2f}{metal_res} | {d_new0:.1f}% vs {d_new1:.1f}% |")
