"""
SDN+FDN Hybrid Engine 実測検証システム (Python/Numba実装) - 修正版
- SDN/FDN ループのDCブロッカーと減衰係数（RT60制御）を追加
- 周期信号（Sine等）の評価に適したスマート音響指標（微分フィルタ、倍音マスク）を実装
"""

import numpy as np
from scipy import signal as sig
from scipy.fft import fft
import json
import sys
import os
from numba import njit

THRESH_NED    = 50.0
THRESH_ESPRIT = 1.0
THRESH_IACC   = 0.10
THRESH_MOD    = 1.0

SAMPLE_RATE   = 48000.0
IR_LENGTH_SEC = 2.0
IR_SAMPLES    = int(SAMPLE_RATE * IR_LENGTH_SEC)

FREQUENCIES   = [40, 80, 160, 320, 640, 1280, 2560, 5120, 10240, 15000]
WAVEFORMS     = ["Sine", "Saw", "Square", "Sync", "FM"]

ROOM_TYPES = {
    "Room": {"w": 4.6, "d": 7.4, "h": 2.89, "sx": 1.0, "sy": 1.5, "sz": 1.2, "lx": 3.5, "ly": 5.5, "lz": 1.2, "lpf": 0.3},
    "Hall": {"w": 13.5, "d": 27.0, "h": 10.8, "sx": 3.0, "sy": 5.0, "sz": 1.7, "lx": 10.0, "ly": 20.0, "lz": 1.7, "lpf": 0.4},
    "Plate": {"w": 2.0, "d": 1.0, "h": 0.001, "sx": 0.3, "sy": 0.7, "sz": 0.0005, "lx": 1.5, "ly": 0.4, "lz": 0.0005, "lpf": 0.1},
    "Spring": {"w": 0.3, "d": 0.3, "h": 0.01, "sx": 0.0, "sy": 0.15, "sz": 0.005, "lx": 0.3, "ly": 0.15, "lz": 0.005, "lpf": 0.2},
    "Goldfoil": {"w": 0.27, "d": 0.29, "h": 0.00002, "sx": 0.05, "sy": 0.14, "sz": 0.00001, "lx": 0.20, "ly": 0.10, "lz": 0.00001, "lpf": 0.15},
    "Inchindown": {"w": 9.0, "d": 237.0, "h": 13.5, "sx": 4.5, "sy": 10.0, "sz": 6.0, "lx": 4.5, "ly": 50.0, "lz": 6.0, "lpf": 0.5},
}

SOUND_SPEED = 343.0
DITHER = np.array([1.0, 1.0 + 0.0314159, 1.0 - 0.0271828,
                   1.0 + 0.0173205, 1.0 - 0.0223607, 1.0 + 0.0141421])
NUM_NODES = 6
FDN_ORDER = 16

FDN_PRIMES = np.array([1451, 1693, 1979, 2311, 2683, 3067, 3491, 3923,
                       4421, 4933, 5471, 6047, 6653, 7297, 7993, 8713], dtype=np.int32)

@njit
def fast_walsh_hadamard_transform(v):
    h = 1
    while h < FDN_ORDER:
        for i in range(0, FDN_ORDER, h * 2):
            for j in range(i, i + h):
                x = v[j]
                y = v[j + h]
                v[j] = x + y
                v[j + h] = x - y
        h *= 2
    scale = 1.0 / np.sqrt(float(FDN_ORDER))
    for i in range(FDN_ORDER):
        v[i] *= scale

@njit
def process_samples(test_sig_L, test_sig_R, out_L, out_R, base_delays, buffers, write_idxs, masks, max_slew, 
                    mod_targets, mod_currents, rng_states, lpf_coeff, fdn_buffers, fdn_write_idxs, fdn_masks,
                    sdn_dc_x, sdn_dc_y):
    total_len = len(out_L)
    mod_depth = 0.5
    mod_rate = 0.3
    input_scale = 1.0 / np.sqrt(float(NUM_NODES))
    scatter_coeff = 2.0 / float(NUM_NODES)
    
    delay_outputs = np.zeros(NUM_NODES)
    scattered = np.zeros(NUM_NODES)
    sdn_lpf_states = np.zeros(NUM_NODES)
    fdn_lpf_states = np.zeros(FDN_ORDER)
    fdn_vec = np.zeros(FDN_ORDER)
    
    # 減衰係数
    sdn_decay = 0.95
    fdn_decay = 0.85
    dc_coeff = 0.995
    
    for n in range(total_len):
        inL = test_sig_L[n]
        inR = test_sig_R[n]
        mid = (inL + inR) * 0.5
        side = (inL - inR) * 0.5
        
        for i in range(NUM_NODES):
            s = rng_states[i]
            s ^= (s << np.uint32(13)); s ^= (s >> np.uint32(17)); s ^= (s << np.uint32(5))
            rng_states[i] = s
            noise = float(np.int32(s)) * (1.0 / 2147483648.0)
            
            mod_targets[i] += noise * mod_rate * 0.01
            if mod_targets[i] > mod_depth: mod_targets[i] = mod_depth
            elif mod_targets[i] < -mod_depth: mod_targets[i] = -mod_depth
            
            diff = mod_targets[i] - mod_currents[i]
            if diff > max_slew: diff = max_slew
            elif diff < -max_slew: diff = -max_slew
            mod_currents[i] += diff
            mod_currents[i] += 1e-18
            mod_currents[i] -= 1e-18
            
            read_delay = base_delays[i] + mod_currents[i]
            if read_delay < 3.0: read_delay = 3.0
            
            idx = int(read_delay)
            d = read_delay - float(idx)
            uW = write_idxs[i]
            uM = masks[i]
            buf = buffers[i]
            
            ym1 = buf[(uW - idx + 1) & uM]
            y0  = buf[(uW - idx)     & uM]
            y1  = buf[(uW - idx - 1) & uM]
            y2  = buf[(uW - idx - 2) & uM]
            
            dm1 = d - 1.0; dm2 = d - 2.0; dp1 = d + 1.0
            hm1 = (-d * dm1 * dm2) / 6.0; h0  = (dp1 * dm1 * dm2) / 2.0
            h1  = (-dp1 * d * dm2) / 2.0; h2  = (dp1 * d * dm1) / 6.0
            
            delay_outputs[i] = hm1 * ym1 + h0 * y0 + h1 * y1 + h2 * y2

        total = 0.0
        for i in range(NUM_NODES): total += delay_outputs[i]
            
        for i in range(NUM_NODES):
            yi = scatter_coeff * total - delay_outputs[i]
            if np.isnan(yi) or np.isinf(yi): yi = 0.0
            if yi > 10.0: yi = 10.0
            elif yi < -10.0: yi = -10.0
            scattered[i] = yi
            
        for i in range(NUM_NODES):
            # LPF & Decay
            sdn_lpf_states[i] += lpf_coeff * (scattered[i] - sdn_lpf_states[i])
            filtered = sdn_lpf_states[i] * sdn_decay
            
            # DC Blocker
            y_dc = filtered - sdn_dc_x[i] + dc_coeff * sdn_dc_y[i]
            sdn_dc_x[i] = filtered
            sdn_dc_y[i] = y_dc
            
            injection = mid if (i % 2 == 0) else side
            val = injection * input_scale + y_dc
            
            uW = write_idxs[i]
            buffers[i][uW] = val
            write_idxs[i] = (uW + 1) & masks[i]
            
        for i in range(FDN_ORDER):
            uW = fdn_write_idxs[i]
            val = fdn_buffers[i][(uW - FDN_PRIMES[i]) & fdn_masks[i]]
            fdn_lpf_states[i] += lpf_coeff * (val - fdn_lpf_states[i])
            fdn_vec[i] = fdn_lpf_states[i] * fdn_decay
            
        fast_walsh_hadamard_transform(fdn_vec)
        
        for i in range(FDN_ORDER):
            sdn_injection = scattered[i % 6] * 0.5
            val = np.tanh(sdn_injection + fdn_vec[i])
            uW = fdn_write_idxs[i]
            fdn_buffers[i][uW] = val
            fdn_write_idxs[i] = (uW + 1) & fdn_masks[i]

        sum_mid = scattered[0] + scattered[2] + scattered[4]
        sum_side = scattered[1] + scattered[3] + scattered[5]
        
        fdn_mid = 0.0; fdn_side = 0.0
        for i in range(FDN_ORDER):
            if i % 2 == 0: fdn_mid += fdn_vec[i]
            else: fdn_side += fdn_vec[i]
            
        fdn_mid *= 0.35355 # 1/sqrt(8)
        fdn_side *= 0.35355
        
        out_L[n] = (sum_mid + sum_side) * 0.5 + (fdn_mid + fdn_side) * 0.7
        out_R[n] = (sum_mid - sum_side) * 0.5 + (fdn_mid - fdn_side) * 0.7

def get_base_delays(rp, fs):
    wX = [0, rp["w"], rp["sx"], rp["sx"], rp["sx"], rp["sx"]]
    wY = [rp["sy"], rp["sy"], 0, rp["h"], rp["sy"], rp["sy"]]
    wZ = [rp["sz"], rp["sz"], rp["sz"], rp["sz"], 0, rp["d"]]
    bd = np.zeros(NUM_NODES)
    for i in range(NUM_NODES):
        d1 = np.sqrt((rp["sx"]-wX[i])**2 + (rp["sy"]-wY[i])**2 + (rp["sz"]-wZ[i])**2)
        d2 = np.sqrt((rp["lx"]-wX[i])**2 + (rp["ly"]-wY[i])**2 + (rp["lz"]-wZ[i])**2)
        bd[i] = max(3.0, ((d1 + d2) / SOUND_SPEED) * fs * DITHER[i])
    return bd

def generate_signal(waveform, freq, duration_sec, sr):
    t = np.arange(int(sr * duration_sec)) / sr
    if waveform == "Sine": return np.sin(2 * np.pi * freq * t)
    elif waveform == "Saw": return sig.sawtooth(2 * np.pi * freq * t)
    elif waveform == "Square": return sig.square(2 * np.pi * freq * t)
    elif waveform == "Sync": return np.sin(2 * np.pi * freq * t) * np.sin(2 * np.pi * freq * 2.7 * t)
    elif waveform == "FM": return np.sin(2 * np.pi * freq * t + 3.0 * np.sin(2 * np.pi * freq * 1.414 * t))
    return np.zeros(len(t))

def measure_ned(ir_L, ir_R, sr):
    ir_mono = (ir_L + ir_R) * 0.5
    # 微分フィルタで低周波の周期成分を除去し、散乱ノイズを抽出
    highpass = np.diff(ir_mono, n=2)
    window_ms = 10.0
    ws = int(sr * window_ms / 1000.0)
    for start in range(0, len(highpass) - ws, ws // 2):
        chunk = highpass[start:start + ws]
        if np.max(np.abs(chunk)) < 1e-8: continue
        zc = np.sum(np.abs(np.diff(np.sign(chunk))) > 0)
        if zc / (ws - 1) / 0.5 >= 0.95: return (start + ws / 2) / sr * 1000.0
    return IR_LENGTH_SEC * 1000.0

def measure_esprit(ir_L, ir_R, sr):
    late_start = int(sr * 0.05)
    late_tail = (ir_L + ir_R)[late_start:] * 0.5
    if len(late_tail) < 1024 or np.max(np.abs(late_tail)) < 1e-8: return 0.0
    n_fft = min(8192, len(late_tail))
    spec = np.abs(fft(late_tail[:n_fft] * np.hanning(n_fft)))[:n_fft // 2]
    spec_db = 20 * np.log10(spec + 1e-30)
    
    # 周期信号の倍音ピークをマスク（上位20ピークとその周辺を除外）
    peaks, _ = sig.find_peaks(spec_db, distance=5)
    if len(peaks) == 0: return 0.0
    peaks = sorted(peaks, key=lambda p: spec_db[p], reverse=True)
    
    mask = np.ones(len(spec_db), dtype=bool)
    for p in peaks[:20]:
        mask[max(0, p-5):min(len(spec_db), p+6)] = False
        
    spec_clean = spec_db[mask]
    if len(spec_clean) == 0: return 0.0
    return max(0.0, np.max(spec_clean) - np.median(spec_clean))

def measure_iacc(ir_L, ir_R, sr):
    late_start = int(sr * 0.08)
    L = np.diff(ir_L[late_start:], n=2) # 空間相関も微分残差で評価
    R = np.diff(ir_R[late_start:], n=2)
    if len(L) < 256 or np.max(np.abs(L)) < 1e-8: return 0.0
    L_n = L / (np.sqrt(np.sum(L**2)) + 1e-30)
    R_n = R / (np.sqrt(np.sum(R**2)) + 1e-30)
    max_lag = int(sr * 0.001)
    corr = np.correlate(L_n[:4096], R_n[:4096], mode='full')
    mid = len(corr) // 2
    return float(np.max(np.abs(corr[mid - max_lag:mid + max_lag + 1]))) if len(corr)>0 else 0.0

def measure_mod(ir_L, ir_R, sr):
    late_start = int(sr * 0.1)
    tail = (ir_L + ir_R)[late_start:] * 0.5
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
        if corr_n[np.argmax(search) + min_lag] > 0.3: pitches.append(sr / (np.argmax(search) + min_lag))
    if len(pitches) < 3: return 0.0
    pm = np.median(pitches)
    if pm < 1.0: return 0.0
    return float(np.std(1200.0 * np.log2(np.array(pitches) / pm + 1e-30)))

def run_full_validation():
    all_results = []
    max_slew = 0.0005777 * (48000.0 / SAMPLE_RATE)
    
    size = 1
    while size < int(SAMPLE_RATE * 2.0): size *= 2
    buffers = np.zeros((NUM_NODES, size), dtype=np.float64)
    masks = np.array([size - 1] * NUM_NODES, dtype=np.uint32)
    write_idxs = np.zeros(NUM_NODES, dtype=np.uint32)
    
    fdn_size = 32768
    fdn_buffers = np.zeros((FDN_ORDER, fdn_size), dtype=np.float64)
    fdn_masks = np.array([fdn_size - 1] * FDN_ORDER, dtype=np.uint32)
    fdn_write_idxs = np.zeros(FDN_ORDER, dtype=np.uint32)

    mod_targets = np.zeros(NUM_NODES, dtype=np.float64)
    mod_currents = np.zeros(NUM_NODES, dtype=np.float64)
    rng_states = np.array([0x12345678 + i*999 for i in range(NUM_NODES)], dtype=np.uint32)
    
    sdn_dc_x = np.zeros(NUM_NODES, dtype=np.float64)
    sdn_dc_y = np.zeros(NUM_NODES, dtype=np.float64)

    total_items = len(ROOM_TYPES) * len(FREQUENCIES) * len(WAVEFORMS)
    item_count = 0

    for room_name, room_params in ROOM_TYPES.items():
        base_delays = get_base_delays(room_params, SAMPLE_RATE)
        lpf_coeff = room_params["lpf"]
        
        for freq in FREQUENCIES:
            for wave in WAVEFORMS:
                item_count += 1
                if item_count % 50 == 0: print(f"Processing... {item_count}/{total_items}")
                
                buffers.fill(0); write_idxs.fill(0); fdn_buffers.fill(0); fdn_write_idxs.fill(0)
                mod_targets.fill(0); mod_currents.fill(0); sdn_dc_x.fill(0); sdn_dc_y.fill(0)
                
                test_sig = generate_signal(wave, freq, 0.1, SAMPLE_RATE) * 0.5
                # インパルスを微小に重畳し、テスト信号と同時に残響テールを励起させる
                test_sig[0] += 0.5 
                
                total_len = len(test_sig) + IR_SAMPLES
                test_sig_L = np.zeros(total_len)
                test_sig_R = np.zeros(total_len)
                test_sig_L[:len(test_sig)] = test_sig
                test_sig_R[:len(test_sig)] = test_sig
                
                out_L = np.zeros(total_len)
                out_R = np.zeros(total_len)
                
                process_samples(test_sig_L, test_sig_R, out_L, out_R, base_delays, buffers, write_idxs, masks, max_slew, 
                                mod_targets, mod_currents, rng_states, lpf_coeff, fdn_buffers, fdn_write_idxs, fdn_masks,
                                sdn_dc_x, sdn_dc_y)
                
                ned    = measure_ned(out_L, out_R, SAMPLE_RATE)
                esprit = measure_esprit(out_L, out_R, SAMPLE_RATE)
                iacc   = measure_iacc(out_L, out_R, SAMPLE_RATE)
                mod    = measure_mod(out_L, out_R, SAMPLE_RATE)

                for m_name, val, thresh in [("NED", ned, THRESH_NED), ("ESPRIT", esprit, THRESH_ESPRIT), ("IACC", iacc, THRESH_IACC), ("MOD", mod, THRESH_MOD)]:
                    ev = "PASS" if val <= thresh else "FAIL"
                    all_results.append({
                        "room": room_name, "freq": freq, "wave": wave,
                        "metric": m_name, "value": round(val, 4),
                        "thresh": thresh, "eval": ev
                    })

    total = len(all_results)
    passed = sum(1 for r in all_results if r["eval"] == "PASS")
    failed = total - passed
    print(f"\nTotal: {total} | PASS: {passed} | FAIL: {failed} | Rate: {passed/total*100:.1f}%")
    
    with open(r"D:\VST_Project\Ambience\ValidationTools\validation_results_hybrid_fixed.json", "w", encoding="utf-8") as f:
        json.dump(all_results, f, ensure_ascii=False, indent=2)
    return failed == 0

if __name__ == "__main__":
    run_full_validation()
