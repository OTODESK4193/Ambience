| # | カテゴリ | アルゴリズム | 条件 / 周波数 | 主要測定値 (全数値) | 評価判定 |
|---|---|---|---|---|---|
| 1 | Impulse | Room1 | Dirac_Impulse | R²=0.9726, Cep=24.21dB, Rip=4.64dB, Jitter=0.434 | Comb: **WARN**, Tail: **PASS** |
| 2 | Impulse | Room2 | Dirac_Impulse | R²=0.9994, Cep=22.08dB, Rip=5.79dB, Jitter=0.125 | Comb: **WARN**, Tail: **PASS** |
| 3 | Impulse | Hall1 | Dirac_Impulse | R²=0.9987, Cep=19.41dB, Rip=5.55dB, Jitter=0.169 | Comb: **WARN**, Tail: **PASS** |
| 4 | Impulse | Hall2 | Dirac_Impulse | R²=0.9991, Cep=16.13dB, Rip=5.61dB, Jitter=0.164 | Comb: **WARN**, Tail: **PASS** |
| 5 | Impulse | Plate | Dirac_Impulse | R²=0.9923, Cep=14.63dB, Rip=5.53dB, Jitter=0.306 | Comb: **WARN**, Tail: **PASS** |
| 6 | Impulse | Spring | Dirac_Impulse | R²=0.8970, Cep=23.75dB, Rip=5.68dB, Jitter=1.806 | Comb: **WARN**, Tail: **FAIL** |
| 7 | Impulse | Goldfoil | Dirac_Impulse | R²=0.9725, Cep=14.13dB, Rip=5.72dB, Jitter=0.748 | Comb: **WARN**, Tail: **PASS** |
| 8 | Impulse | Inchindown | Dirac_Impulse | R²=0.7990, Cep=21.86dB, Rip=5.35dB, Jitter=1.672 | Comb: **WARN**, Tail: **FAIL** |
| 9 | Silence | Room1 | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 10 | Silence | Room2 | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 11 | Silence | Hall1 | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 12 | Silence | Hall2 | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 13 | Silence | Plate | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 14 | Silence | Spring | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 15 | Silence | Goldfoil | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 16 | Silence | Inchindown | Cold_Silence | RMS=-300.0dBFS, Peak=-300.0dBFS, DC=0.0e+00, Denorm=0, NaN/Inf=0 | Noise: **PASS** |
| 17 | PostBurst | Room1 | NoiseBurst_Recovery | TailRMS=-300.0dBFS, Peak=-300.0dBFS, Denorm=0 | Recovery: **PASS** |
| 18 | PostBurst | Room2 | NoiseBurst_Recovery | TailRMS=-93.6dBFS, Peak=-76.2dBFS, Denorm=0 | Recovery: **PASS** |
| 19 | PostBurst | Hall1 | NoiseBurst_Recovery | TailRMS=-70.5dBFS, Peak=-55.4dBFS, Denorm=0 | Recovery: **PASS** |
| 20 | PostBurst | Hall2 | NoiseBurst_Recovery | TailRMS=-69.9dBFS, Peak=-55.6dBFS, Denorm=0 | Recovery: **WARN** |
| 21 | PostBurst | Plate | NoiseBurst_Recovery | TailRMS=-87.7dBFS, Peak=-73.5dBFS, Denorm=0 | Recovery: **PASS** |
| 22 | PostBurst | Spring | NoiseBurst_Recovery | TailRMS=-41.9dBFS, Peak=-29.1dBFS, Denorm=0 | Recovery: **WARN** |
| 23 | PostBurst | Goldfoil | NoiseBurst_Recovery | TailRMS=-42.0dBFS, Peak=-28.9dBFS, Denorm=0 | Recovery: **WARN** |
| 24 | PostBurst | Inchindown | NoiseBurst_Recovery | TailRMS=-36.5dBFS, Peak=-24.1dBFS, Denorm=0 | Recovery: **WARN** |
| 25 | Stress | Room1 | Max_Decay | Peak=-4.0dBFS, TailRMS=-37.9dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 26 | Stress | Room1 | MaxSize_MaxDecay | Peak=-5.9dBFS, TailRMS=-51.1dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 27 | Stress | Room1 | Max_Nonlinearity | Peak=-1.9dBFS, TailRMS=-47.9dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 28 | Stress | Room1 | Zero_Diffusion | Peak=-6.2dBFS, TailRMS=-94.2dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 29 | Stress | Room2 | Max_Decay | Peak=-6.8dBFS, TailRMS=-26.0dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 30 | Stress | Room2 | MaxSize_MaxDecay | Peak=-8.4dBFS, TailRMS=-26.0dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 31 | Stress | Room2 | Max_Nonlinearity | Peak=-5.4dBFS, TailRMS=-23.7dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 32 | Stress | Room2 | Zero_Diffusion | Peak=-8.3dBFS, TailRMS=-34.2dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 33 | Stress | Hall1 | Max_Decay | Peak=-10.3dBFS, TailRMS=-24.6dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 34 | Stress | Hall1 | MaxSize_MaxDecay | Peak=-7.9dBFS, TailRMS=-24.7dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 35 | Stress | Hall1 | Max_Nonlinearity | Peak=-5.6dBFS, TailRMS=-21.9dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 36 | Stress | Hall1 | Zero_Diffusion | Peak=-10.3dBFS, TailRMS=-29.9dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 37 | Stress | Hall2 | Max_Decay | Peak=-8.4dBFS, TailRMS=-25.5dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 38 | Stress | Hall2 | MaxSize_MaxDecay | Peak=-9.2dBFS, TailRMS=-21.6dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 39 | Stress | Hall2 | Max_Nonlinearity | Peak=-8.6dBFS, TailRMS=-19.2dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 40 | Stress | Hall2 | Zero_Diffusion | Peak=-10.2dBFS, TailRMS=-31.2dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 41 | Stress | Plate | Max_Decay | Peak=-3.4dBFS, TailRMS=-28.2dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 42 | Stress | Plate | MaxSize_MaxDecay | Peak=-6.6dBFS, TailRMS=-24.3dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 43 | Stress | Plate | Max_Nonlinearity | Peak=-0.5dBFS, TailRMS=-21.4dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 44 | Stress | Plate | Zero_Diffusion | Peak=-0.6dBFS, TailRMS=-41.0dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 45 | Stress | Spring | Max_Decay | Peak=-10.3dBFS, TailRMS=-23.5dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 46 | Stress | Spring | MaxSize_MaxDecay | Peak=-10.5dBFS, TailRMS=-26.4dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 47 | Stress | Spring | Max_Nonlinearity | Peak=-1.8dBFS, TailRMS=-23.2dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 48 | Stress | Spring | Zero_Diffusion | Peak=-10.1dBFS, TailRMS=-27.1dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 49 | Stress | Goldfoil | Max_Decay | Peak=-2.1dBFS, TailRMS=-29.0dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 50 | Stress | Goldfoil | MaxSize_MaxDecay | Peak=-4.5dBFS, TailRMS=-26.9dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 51 | Stress | Goldfoil | Max_Nonlinearity | Peak=-0.5dBFS, TailRMS=-23.0dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 52 | Stress | Goldfoil | Zero_Diffusion | Peak=-6.0dBFS, TailRMS=-31.5dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 53 | Stress | Inchindown | Max_Decay | Peak=-10.4dBFS, TailRMS=-21.6dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 54 | Stress | Inchindown | MaxSize_MaxDecay | Peak=-9.8dBFS, TailRMS=-20.8dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 55 | Stress | Inchindown | Max_Nonlinearity | Peak=-1.9dBFS, TailRMS=-17.5dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 56 | Stress | Inchindown | Zero_Diffusion | Peak=-8.6dBFS, TailRMS=-21.7dBFS, Blowout=False, NaN=0, Inf=0 | Stress: **PASS** |
| 57 | TonalPulse | Room1 | Pulse_100Hz 100Hz | R²=0.9277, Cep=20.04dB, Rip=5.28dB, Jitter=0.409 | Tonal: **PASS** |
| 58 | TonalPulse | Room2 | Pulse_100Hz 100Hz | R²=0.9966, Cep=18.65dB, Rip=5.95dB, Jitter=0.850 | Tonal: **PASS** |
| 59 | TonalPulse | Hall1 | Pulse_100Hz 100Hz | R²=0.9792, Cep=18.32dB, Rip=5.57dB, Jitter=0.927 | Tonal: **PASS** |
| 60 | TonalPulse | Hall2 | Pulse_100Hz 100Hz | R²=0.9932, Cep=19.44dB, Rip=5.96dB, Jitter=0.906 | Tonal: **PASS** |
| 61 | TonalPulse | Plate | Pulse_100Hz 100Hz | R²=0.9980, Cep=20.59dB, Rip=5.58dB, Jitter=0.509 | Tonal: **PASS** |
| 62 | TonalPulse | Spring | Pulse_100Hz 100Hz | R²=0.9280, Cep=17.44dB, Rip=5.60dB, Jitter=1.675 | Tonal: **PASS** |
| 63 | TonalPulse | Goldfoil | Pulse_100Hz 100Hz | R²=0.9398, Cep=22.21dB, Rip=5.99dB, Jitter=1.560 | Tonal: **PASS** |
| 64 | TonalPulse | Inchindown | Pulse_100Hz 100Hz | R²=0.6772, Cep=18.31dB, Rip=5.08dB, Jitter=2.242 | Tonal: **PASS** |
| 65 | TonalPulse | Room1 | Pulse_1000Hz 1000Hz | R²=0.9931, Cep=19.08dB, Rip=5.28dB, Jitter=0.048 | Tonal: **PASS** |
| 66 | TonalPulse | Room2 | Pulse_1000Hz 1000Hz | R²=0.9927, Cep=22.93dB, Rip=5.71dB, Jitter=0.830 | Tonal: **PASS** |
| 67 | TonalPulse | Hall1 | Pulse_1000Hz 1000Hz | R²=0.9951, Cep=20.02dB, Rip=5.50dB, Jitter=0.773 | Tonal: **PASS** |
| 68 | TonalPulse | Hall2 | Pulse_1000Hz 1000Hz | R²=0.9919, Cep=17.42dB, Rip=5.61dB, Jitter=0.951 | Tonal: **PASS** |
| 69 | TonalPulse | Plate | Pulse_1000Hz 1000Hz | R²=0.9986, Cep=15.32dB, Rip=5.47dB, Jitter=0.441 | Tonal: **PASS** |
| 70 | TonalPulse | Spring | Pulse_1000Hz 1000Hz | R²=0.6820, Cep=23.46dB, Rip=5.66dB, Jitter=2.308 | Tonal: **ACCEPTABLE** |
| 71 | TonalPulse | Goldfoil | Pulse_1000Hz 1000Hz | R²=0.8946, Cep=21.60dB, Rip=5.60dB, Jitter=1.464 | Tonal: **PASS** |
| 72 | TonalPulse | Inchindown | Pulse_1000Hz 1000Hz | R²=0.7098, Cep=17.43dB, Rip=5.70dB, Jitter=1.977 | Tonal: **PASS** |
| 73 | TonalPulse | Room1 | Pulse_5000Hz 5000Hz | R²=0.8556, Cep=23.05dB, Rip=5.46dB, Jitter=0.599 | Tonal: **PASS** |
| 74 | TonalPulse | Room2 | Pulse_5000Hz 5000Hz | R²=0.9946, Cep=26.81dB, Rip=5.89dB, Jitter=0.794 | Tonal: **PASS** |
| 75 | TonalPulse | Hall1 | Pulse_5000Hz 5000Hz | R²=0.9945, Cep=21.54dB, Rip=5.53dB, Jitter=0.811 | Tonal: **PASS** |
| 76 | TonalPulse | Hall2 | Pulse_5000Hz 5000Hz | R²=0.9801, Cep=22.21dB, Rip=5.74dB, Jitter=0.908 | Tonal: **PASS** |
| 77 | TonalPulse | Plate | Pulse_5000Hz 5000Hz | R²=0.9973, Cep=14.60dB, Rip=5.59dB, Jitter=0.214 | Tonal: **PASS** |
| 78 | TonalPulse | Spring | Pulse_5000Hz 5000Hz | R²=0.9693, Cep=20.53dB, Rip=5.40dB, Jitter=1.020 | Tonal: **PASS** |
| 79 | TonalPulse | Goldfoil | Pulse_5000Hz 5000Hz | R²=0.9987, Cep=15.71dB, Rip=5.68dB, Jitter=0.322 | Tonal: **PASS** |
| 80 | TonalPulse | Inchindown | Pulse_5000Hz 5000Hz | R²=0.6465, Cep=20.67dB, Rip=5.60dB, Jitter=2.750 | Tonal: **PASS** |