import numpy as np
from scipy import signal as sig
from scipy.fft import fft

def measure_esprit_smart(sig_array, sr):
    n_fft = min(8192, len(sig_array))
    spec = np.abs(fft(sig_array[:n_fft] * np.blackman(n_fft)))[:n_fft // 2]
    spec_db = 20 * np.log10(spec + 1e-30)
    
    peaks, _ = sig.find_peaks(spec_db, distance=10)
    if len(peaks) == 0: return 0.0
    
    prominences = sig.peak_prominences(spec_db, peaks)[0]
    
    # Exclude the largest peak (which is the fundamental sine wave)
    if len(prominences) > 1:
        # Sort peaks by absolute amplitude
        sorted_peak_indices = np.argsort(spec_db[peaks])[::-1]
        
        # Remove the top 3 amplitude peaks from the prominence calculation
        valid_proms = []
        for i, p_idx in enumerate(sorted_peak_indices):
            if i >= 3:  # skip the 3 highest amplitude peaks (fundamental + harmonics)
                valid_proms.append(prominences[p_idx])
                
        if len(valid_proms) == 0: return 0.0
        return float(np.max(valid_proms))
    
    return 0.0

sr = 48000
t = np.arange(48000) / sr

# 1. Clean Modulated Sine (FM) - Should have 0 or very low prominence spurious peaks
sig_clean = np.sin(2 * np.pi * 440 * t + 0.1 * np.sin(2 * np.pi * 2 * t))
print("Clean Modulated Sine ESPRIT:", measure_esprit_smart(sig_clean, sr))

# 2. Sine + Metallic Ringing at 3000Hz (prominence = 5dB)
sig_bad = sig_clean + 0.05 * np.sin(2 * np.pi * 3000 * t) * np.exp(-t)
print("Metallic Ringing ESPRIT:", measure_esprit_smart(sig_bad, sr))
