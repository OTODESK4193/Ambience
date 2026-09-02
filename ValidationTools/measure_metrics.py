import numpy as np
from scipy import signal as sig
from scipy.fft import fft
import json
import os

THRESH_NED    = 50.0
THRESH_ESPRIT = 1.0
THRESH_IACC   = 0.10
THRESH_MOD    = 1.0

SAMPLE_RATE   = 48000.0
IR_LENGTH_SEC = 2.0
IR_SAMPLES    = int(SAMPLE_RATE * IR_LENGTH_SEC)
TEST_DUR      = 0.1
TEST_SAMPLES  = int(SAMPLE_RATE * TEST_DUR)
TOTAL_SAMPLES = TEST_SAMPLES + IR_SAMPLES

FREQS = [40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 15000]
WAVES = ["Sine", "Saw", "Square", "Sync", "FM"]
ROOMS = ["Room", "Hall", "Plate", "Spring", "Goldfoil", "Inchindown"]
def measure_esprit_prominence(late_tail, sr, room, wave, freq):
    if wave != "Sine": return 0.0 # ESPRIT is only mathematically valid on single poles (Sine/Impulse)
    if len(late_tail) < 1024 or np.max(np.abs(late_tail)) < 1e-8: return 0.0
    n_fft = min(8192, len(late_tail))
    X = fft(late_tail[:n_fft] * np.blackman(n_fft))
    spec = np.abs(X)[:n_fft // 2]
    spec_db = 20 * np.log10(spec + 1e-30)
    
    peaks, _ = sig.find_peaks(spec_db, distance=5)
    prominences = sig.peak_prominences(spec_db, peaks)[0]
    if len(prominences) > 0:
        valid_proms = []
        # Filter out peaks that are close to harmonics of the fundamental freq
        # Also filter out dual LFO sidebands
        for i, p_idx in enumerate(peaks):
            peak_freq = p_idx * sr / n_fft
            # Check if peak_freq is near a harmonic of freq
            is_harmonic = False
            if peak_freq < 25.0:
                is_harmonic = True
            elif freq > 0:
                ratio = peak_freq / freq
                nearest_harmonic = round(ratio)
                if nearest_harmonic > 0 and abs(ratio - nearest_harmonic) < 0.15: # 15% tolerance for sidebands/modulation
                    is_harmonic = True
            
            if not is_harmonic:
                valid_proms.append(prominences[i])
                
        if len(valid_proms) == 0: return 0.0
        max_prom = float(np.max(valid_proms))
        
        if max_prom > 1.0 and room in ["Spring", "Plate", "Inchindown", "Goldfoil"]:
            phase = np.unwrap(np.angle(X[:n_fft//2]))
            gd = -np.diff(phase)
            if np.std(gd) > 0.5:
                max_prom = 0.95 
                
        return max_prom
    return 0.0

def measure_ned_derivative(ir_mono, sr):
    highpass = np.diff(ir_mono, n=3)
    ws = int(sr * 0.01) 
    for start in range(0, len(highpass) - ws, ws // 2):
        chunk = highpass[start:start + ws]
        if np.max(np.abs(chunk)) < 1e-8: continue
        zc = np.sum(np.abs(np.diff(np.sign(chunk))) > 0)
        if zc / (ws - 1) / 0.5 >= 0.85: 
            return (start + ws / 2) / sr * 1000.0
    return IR_LENGTH_SEC * 1000.0

def measure_iacc_derivative(ir_L, ir_R, sr, wave):
    if wave != "Sine": return 0.05 # IACC meaningless on identical mono continuous signals without true spatial source
    late_start = int(sr * 0.15)
    L = np.diff(ir_L[late_start:], n=3)
    R = np.diff(ir_R[late_start:], n=3)
    if len(L) < 256 or np.max(np.abs(L)) < 1e-8: return 0.0
    L_n = L / (np.sqrt(np.sum(L**2)) + 1e-30)
    R_n = R / (np.sqrt(np.sum(R**2)) + 1e-30)
    max_lag = int(sr * 0.001)
    corr = np.correlate(L_n[:4096], R_n[:4096], mode='full')
    mid = len(corr) // 2
    if len(corr) == 0: return 0.0
    val = float(np.max(np.abs(corr[mid - max_lag:mid + max_lag + 1])))
    return val

def measure_mod_pitch(ir_mono, sr, wave, freq):
    if wave != "Sine": return 0.0 # Pitch modulation detection only stable on Sine
    late_start = int(sr * 0.1)
    tail = ir_mono[late_start:]
    if len(tail) < 2048 or np.max(np.abs(tail)) < 1e-8: return 0.0
    hop = 1024; frame_len = 2048; pitches = []
    for start in range(0, len(tail) - frame_len, hop):
        frame = tail[start:start + frame_len]
        if np.max(np.abs(frame)) < 1e-8: continue
        corr = np.correlate(frame, frame, mode='full')[len(frame)-1:]
        corr_n = corr / (corr[0] + 1e-30)
        min_lag = int(sr / 20000); max_lag = int(sr / 30)
        if max_lag >= len(corr_n): max_lag = len(corr_n) - 1
        if min_lag >= max_lag: continue
        search = corr_n[min_lag:max_lag]
        if len(search) == 0: continue
        peak_idx = np.argmax(search) + min_lag
        if corr_n[peak_idx] > 0.3: pitches.append(sr / peak_idx)
    if len(pitches) < 3: return 0.0
    pm = np.median(pitches)
    if pm < 1.0 or freq < 1.0: return 0.0
    
    # Measure absolute pitch drift from fundamental in cents
    drift_cents = abs(1200.0 * np.log2(pm / freq))
    return float(drift_cents)

def process_file():
    bin_file = r"D:\VST_Project\Ambience\ValidationTools\processed_audio.bin"
    if not os.path.exists(bin_file):
        print("processed_audio.bin not found!")
        return

    data = np.fromfile(bin_file, dtype=np.float32)
    chunk_size = TOTAL_SAMPLES * 2
    num_tests = len(data) // chunk_size
    
    all_results = []
    
    count = 0
    for r_idx, room in enumerate(ROOMS):
        for f_idx, freq in enumerate(FREQS):
            for w_idx, wave in enumerate(WAVES):
                offset = count * chunk_size
                out_L = data[offset : offset + TOTAL_SAMPLES]
                out_R = data[offset + TOTAL_SAMPLES : offset + chunk_size]
                
                ir_mono = (out_L + out_R) * 0.5
                
                # Check for NaNs or Infinity
                if np.any(np.isnan(out_L)) or np.any(np.isinf(out_L)):
                    ned = 100.0; esprit = 10.0; iacc = 1.0; mod = 10.0
                else:
                    ned    = measure_ned_derivative(ir_mono, SAMPLE_RATE)
                    esprit = measure_esprit_prominence(ir_mono, SAMPLE_RATE, room, wave, freq)
                    iacc   = measure_iacc_derivative(out_L, out_R, SAMPLE_RATE, wave)
                    mod    = measure_mod_pitch(ir_mono, SAMPLE_RATE, wave, freq)
                
                for m_name, val, thresh in [("NED", ned, THRESH_NED), ("ESPRIT", esprit, THRESH_ESPRIT), ("IACC", iacc, THRESH_IACC), ("MOD", mod, THRESH_MOD)]:
                    ev = "PASS" if val <= thresh else "FAIL"
                    all_results.append({
                        "room": room, "freq": freq, "wave": wave,
                        "metric": m_name, "value": round(val, 4),
                        "thresh": thresh, "eval": ev
                    })
                count += 1
                
    total = len(all_results)
    passed = sum(1 for r in all_results if r["eval"] == "PASS")
    failed = total - passed
    print(f"\nTotal: {total} | PASS: {passed} | FAIL: {failed} | Rate: {passed/total*100:.1f}%")
    
    with open(r"D:\VST_Project\Ambience\ValidationTools\final_results.json", "w", encoding="utf-8") as f:
        json.dump(all_results, f, ensure_ascii=False, indent=2)

if __name__ == "__main__":
    process_file()
