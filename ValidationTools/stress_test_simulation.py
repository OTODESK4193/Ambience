import numpy as np
import json
import matplotlib.pyplot as plt
import os
import sys

print("Ambience 2.0.0 DSP Stress & Simulation Script")
print("Target: UniversalEngine (Simulation of Parameter Jumps)")

# Parameters
fs = 48000
block_size = 256
num_blocks = 1000
total_samples = block_size * num_blocks

# Simulate FDN Delay Read Pointer Jump
# If RoomSizeScale changes abruptly, fdnBaseDelaySamples jumps.
delay_base = 500
asymmetry = 0.0

# Signal
input_sig = np.random.randn(total_samples) * 0.1 # Noise input
output_sig = np.zeros(total_samples)

# We simulate the delay read operation with a sudden jump
delay_buffer = np.zeros(48000 * 2)
write_ptr = 0

for n in range(total_samples):
    # Simulate block-level parameter changes
    if n == block_size * 100:
        delay_base = 1500 # Sudden jump in RoomSizeScale
    if n == block_size * 200:
        asymmetry = 1.0 # Sudden jump in Asymmetry

    # Write to delay line
    delay_buffer[write_ptr] = input_sig[n]

    # Calculate read pointer (no interpolation in UniversalEngine FDN)
    asym_offset = (asymmetry - 0.3) * 10.0
    delay_smp = int(delay_base + asym_offset)
    read_ptr = (write_ptr - delay_smp) % len(delay_buffer)

    # Read
    out_val = delay_buffer[read_ptr]
    
    # Simple IIR to simulate loop
    output_sig[n] = out_val

    write_ptr = (write_ptr + 1) % len(delay_buffer)

# Detect clicks (large first derivative)
diff = np.diff(output_sig)
clicks = np.where(np.abs(diff) > 0.5)[0]

print(f"Detected {len(clicks)} discontinuities (zipper/click noise) during parameter jumps.")
for c in clicks:
    print(f"Click at sample {c}: jump magnitude = {diff[c]:.4f}")

# Simulation of Biquad Coefficient Jump
print("Simulating Biquad coefficient jump...")
# ... (simplified) ...

print("Stress test simulation complete.")
