# 波形・周波数別 高精度数学的検証レポート

各周波数および波形ごとに、4つの数学的指標（NED, ESPRIT, IACC, Mod）のシミュレーション実測値と許容値を厳密に比較した結果です。

## 周波数: 40 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 110.11 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 0.52 | $\le 3.00$ | PASS (合格) |
| Sine | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Sine | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Saw | ① NED (エコー密度到達 ms) | 70.05 | $\le 50.00$ | WARN (要改善) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 0.52 | $\le 3.00$ | PASS (合格) |
| Saw | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 70.05 | $\le 50.00$ | WARN (要改善) |
| Square | ② ESPRIT (共鳴突出度 dB) | 0.52 | $\le 3.00$ | PASS (合格) |
| Square | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 70.05 | $\le 50.00$ | WARN (要改善) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 0.52 | $\le 3.00$ | PASS (合格) |
| Sync | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 70.05 | $\le 50.00$ | WARN (要改善) |
| FM | ② ESPRIT (共鳴突出度 dB) | 0.52 | $\le 3.00$ | PASS (合格) |
| FM | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |

## 周波数: 80 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 110.21 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 0.55 | $\le 3.00$ | PASS (合格) |
| Sine | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Sine | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Saw | ① NED (エコー密度到達 ms) | 70.11 | $\le 50.00$ | WARN (要改善) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 0.55 | $\le 3.00$ | PASS (合格) |
| Saw | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 70.11 | $\le 50.00$ | WARN (要改善) |
| Square | ② ESPRIT (共鳴突出度 dB) | 0.55 | $\le 3.00$ | PASS (合格) |
| Square | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 70.11 | $\le 50.00$ | WARN (要改善) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 0.55 | $\le 3.00$ | PASS (合格) |
| Sync | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 70.11 | $\le 50.00$ | WARN (要改善) |
| FM | ② ESPRIT (共鳴突出度 dB) | 0.55 | $\le 3.00$ | PASS (合格) |
| FM | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |

## 周波数: 160 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 110.43 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 0.59 | $\le 3.00$ | PASS (合格) |
| Sine | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Sine | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Saw | ① NED (エコー密度到達 ms) | 70.21 | $\le 50.00$ | WARN (要改善) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 0.59 | $\le 3.00$ | PASS (合格) |
| Saw | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 70.21 | $\le 50.00$ | WARN (要改善) |
| Square | ② ESPRIT (共鳴突出度 dB) | 0.59 | $\le 3.00$ | PASS (合格) |
| Square | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 70.21 | $\le 50.00$ | WARN (要改善) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 0.59 | $\le 3.00$ | PASS (合格) |
| Sync | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 70.21 | $\le 50.00$ | WARN (要改善) |
| FM | ② ESPRIT (共鳴突出度 dB) | 0.59 | $\le 3.00$ | PASS (合格) |
| FM | ③ IACC (ステレオ相関) | 0.12 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |

## 周波数: 320 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 110.85 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 0.69 | $\le 3.00$ | PASS (合格) |
| Sine | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Sine | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Saw | ① NED (エコー密度到達 ms) | 70.43 | $\le 50.00$ | WARN (要改善) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 0.69 | $\le 3.00$ | PASS (合格) |
| Saw | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 70.43 | $\le 50.00$ | WARN (要改善) |
| Square | ② ESPRIT (共鳴突出度 dB) | 0.69 | $\le 3.00$ | PASS (合格) |
| Square | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 70.43 | $\le 50.00$ | WARN (要改善) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 0.69 | $\le 3.00$ | PASS (合格) |
| Sync | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 70.43 | $\le 50.00$ | WARN (要改善) |
| FM | ② ESPRIT (共鳴突出度 dB) | 0.69 | $\le 3.00$ | PASS (合格) |
| FM | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.03 | $\le 0.10$ | PASS (合格) |

## 周波数: 640 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 111.71 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 0.88 | $\le 3.00$ | PASS (合格) |
| Sine | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Sine | ④ MOD (周期性指数) | 0.04 | $\le 0.10$ | PASS (合格) |
| Saw | ① NED (エコー密度到達 ms) | 70.85 | $\le 50.00$ | WARN (要改善) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 0.88 | $\le 3.00$ | PASS (合格) |
| Saw | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.04 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 70.85 | $\le 50.00$ | WARN (要改善) |
| Square | ② ESPRIT (共鳴突出度 dB) | 0.88 | $\le 3.00$ | PASS (合格) |
| Square | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.04 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 70.85 | $\le 50.00$ | WARN (要改善) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 0.88 | $\le 3.00$ | PASS (合格) |
| Sync | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.04 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 70.85 | $\le 50.00$ | WARN (要改善) |
| FM | ② ESPRIT (共鳴突出度 dB) | 0.88 | $\le 3.00$ | PASS (合格) |
| FM | ③ IACC (ステレオ相関) | 0.13 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.04 | $\le 0.10$ | PASS (合格) |

## 周波数: 1280 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 113.41 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 1.25 | $\le 3.00$ | PASS (合格) |
| Sine | ③ IACC (ステレオ相関) | 0.14 | $\le 0.30$ | PASS (合格) |
| Sine | ④ MOD (周期性指数) | 0.05 | $\le 0.10$ | PASS (合格) |
| Saw | ① NED (エコー密度到達 ms) | 71.71 | $\le 50.00$ | WARN (要改善) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 1.25 | $\le 3.00$ | PASS (合格) |
| Saw | ③ IACC (ステレオ相関) | 0.14 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.05 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 71.71 | $\le 50.00$ | WARN (要改善) |
| Square | ② ESPRIT (共鳴突出度 dB) | 1.25 | $\le 3.00$ | PASS (合格) |
| Square | ③ IACC (ステレオ相関) | 0.14 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.05 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 71.71 | $\le 50.00$ | WARN (要改善) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 1.25 | $\le 3.00$ | PASS (合格) |
| Sync | ③ IACC (ステレオ相関) | 0.14 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.05 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 71.71 | $\le 50.00$ | WARN (要改善) |
| FM | ② ESPRIT (共鳴突出度 dB) | 1.25 | $\le 3.00$ | PASS (合格) |
| FM | ③ IACC (ステレオ相関) | 0.14 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.05 | $\le 0.10$ | PASS (合格) |

## 周波数: 2560 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 116.83 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 5.88 | $\le 3.00$ | FAIL (致命的) |
| Sine | ③ IACC (ステレオ相関) | 0.31 | $\le 0.30$ | WARN (要改善) |
| Sine | ④ MOD (周期性指数) | 0.13 | $\le 0.10$ | WARN (要改善) |
| Saw | ① NED (エコー密度到達 ms) | 73.41 | $\le 50.00$ | WARN (要改善) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 2.60 | $\le 3.00$ | PASS (合格) |
| Saw | ③ IACC (ステレオ相関) | 0.17 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 73.41 | $\le 50.00$ | WARN (要改善) |
| Square | ② ESPRIT (共鳴突出度 dB) | 2.60 | $\le 3.00$ | PASS (合格) |
| Square | ③ IACC (ステレオ相関) | 0.17 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 73.41 | $\le 50.00$ | WARN (要改善) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 2.60 | $\le 3.00$ | PASS (合格) |
| Sync | ③ IACC (ステレオ相関) | 0.17 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 73.41 | $\le 50.00$ | WARN (要改善) |
| FM | ② ESPRIT (共鳴突出度 dB) | 2.60 | $\le 3.00$ | PASS (合格) |
| FM | ③ IACC (ステレオ相関) | 0.17 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |

## 周波数: 5120 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 123.65 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 7.75 | $\le 3.00$ | FAIL (致命的) |
| Sine | ③ IACC (ステレオ相関) | 0.37 | $\le 0.30$ | WARN (要改善) |
| Sine | ④ MOD (周期性指数) | 0.19 | $\le 0.10$ | FAIL (致命的) |
| Saw | ① NED (エコー密度到達 ms) | 76.83 | $\le 50.00$ | FAIL (致命的) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 3.19 | $\le 3.00$ | WARN (要改善) |
| Saw | ③ IACC (ステレオ相関) | 0.18 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 76.83 | $\le 50.00$ | FAIL (致命的) |
| Square | ② ESPRIT (共鳴突出度 dB) | 3.19 | $\le 3.00$ | WARN (要改善) |
| Square | ③ IACC (ステレオ相関) | 0.18 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 76.83 | $\le 50.00$ | FAIL (致命的) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 3.19 | $\le 3.00$ | WARN (要改善) |
| Sync | ③ IACC (ステレオ相関) | 0.18 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 76.83 | $\le 50.00$ | FAIL (致命的) |
| FM | ② ESPRIT (共鳴突出度 dB) | 3.19 | $\le 3.00$ | WARN (要改善) |
| FM | ③ IACC (ステレオ相関) | 0.18 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.06 | $\le 0.10$ | PASS (合格) |

## 周波数: 10240 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 137.31 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 11.51 | $\le 3.00$ | FAIL (致命的) |
| Sine | ③ IACC (ステレオ相関) | 0.49 | $\le 0.30$ | FAIL (致命的) |
| Sine | ④ MOD (周期性指数) | 0.30 | $\le 0.10$ | FAIL (致命的) |
| Saw | ① NED (エコー密度到達 ms) | 83.65 | $\le 50.00$ | FAIL (致命的) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 4.39 | $\le 3.00$ | WARN (要改善) |
| Saw | ③ IACC (ステレオ相関) | 0.22 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.08 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 83.65 | $\le 50.00$ | FAIL (致命的) |
| Square | ② ESPRIT (共鳴突出度 dB) | 4.39 | $\le 3.00$ | WARN (要改善) |
| Square | ③ IACC (ステレオ相関) | 0.22 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.08 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 83.65 | $\le 50.00$ | FAIL (致命的) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 4.39 | $\le 3.00$ | WARN (要改善) |
| Sync | ③ IACC (ステレオ相関) | 0.22 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.08 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 83.65 | $\le 50.00$ | FAIL (致命的) |
| FM | ② ESPRIT (共鳴突出度 dB) | 4.39 | $\le 3.00$ | WARN (要改善) |
| FM | ③ IACC (ステレオ相関) | 0.22 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.08 | $\le 0.10$ | PASS (合格) |

## 周波数: 15000 Hz

| 波形 | 検証項目 | 実測値 | 許容値 | 判定 |
| :--- | :--- | :--- | :--- | :--- |
| Sine | ① NED (エコー密度到達 ms) | 150.00 | $\le 50.00$ | FAIL (致命的) |
| Sine | ② ESPRIT (共鳴突出度 dB) | 15.00 | $\le 3.00$ | FAIL (致命的) |
| Sine | ③ IACC (ステレオ相関) | 0.60 | $\le 0.30$ | FAIL (致命的) |
| Sine | ④ MOD (周期性指数) | 0.40 | $\le 0.10$ | FAIL (致命的) |
| Saw | ① NED (エコー密度到達 ms) | 90.00 | $\le 50.00$ | FAIL (致命的) |
| Saw | ② ESPRIT (共鳴突出度 dB) | 5.50 | $\le 3.00$ | FAIL (致命的) |
| Saw | ③ IACC (ステレオ相関) | 0.25 | $\le 0.30$ | PASS (合格) |
| Saw | ④ MOD (周期性指数) | 0.09 | $\le 0.10$ | PASS (合格) |
| Square | ① NED (エコー密度到達 ms) | 90.00 | $\le 50.00$ | FAIL (致命的) |
| Square | ② ESPRIT (共鳴突出度 dB) | 5.50 | $\le 3.00$ | FAIL (致命的) |
| Square | ③ IACC (ステレオ相関) | 0.25 | $\le 0.30$ | PASS (合格) |
| Square | ④ MOD (周期性指数) | 0.09 | $\le 0.10$ | PASS (合格) |
| Sync | ① NED (エコー密度到達 ms) | 90.00 | $\le 50.00$ | FAIL (致命的) |
| Sync | ② ESPRIT (共鳴突出度 dB) | 5.50 | $\le 3.00$ | FAIL (致命的) |
| Sync | ③ IACC (ステレオ相関) | 0.25 | $\le 0.30$ | PASS (合格) |
| Sync | ④ MOD (周期性指数) | 0.09 | $\le 0.10$ | PASS (合格) |
| FM | ① NED (エコー密度到達 ms) | 90.00 | $\le 50.00$ | FAIL (致命的) |
| FM | ② ESPRIT (共鳴突出度 dB) | 5.50 | $\le 3.00$ | FAIL (致命的) |
| FM | ③ IACC (ステレオ相関) | 0.25 | $\le 0.30$ | PASS (合格) |
| FM | ④ MOD (周期性指数) | 0.09 | $\le 0.10$ | PASS (合格) |

