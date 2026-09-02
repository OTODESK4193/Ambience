# SDN移行 統合開発計画書（SDN Migration Development Plan）

本計画書は、12個の専門サブエージェントによるDeepResearch（各5回の批判的再検証を含む）の全結果を統合し、FDN → SDN移行の具体的な実装ロードマップを定義するものである。

> [!IMPORTANT]
> **絶対条件:** Sourceコードへの反映は、1,200項目の検証テスト（10周波数 × 5波形 × 4指標 × 6ルームタイプ）を**全てPASS**した場合のみ許可する。1項目でもFAILが存在する場合は実装を許可せず、原因究明サブエージェントを起動し、PASSするまで修正とテストを繰り返す。

---

## 1. コアアーキテクチャ（Phase 1結論）

### SDN + FDN ハイブリッド設計

```
[Input] → [ISM初期反射(3次)] → [SDN散乱ノード(6-12)] → [FDN後期残響(16ch)]
                                        ↕                         ↕
                                  [境界吸収Biquad]          [Hadamard FWHT]
                                  [分散Allpass]             [10バンドGEQ]
                                  [ADAA Saturator]          [AGC]
                                        ↓                         ↓
                                  [BrownianModulator]    [DualGoldenLFO維持]
                                        ↓                         ↓
                                  ─────────── [Mixer] ───────────
                                                ↓
                                  [3-5バンドDynamic EQ Ducking]
                                                ↓
                                  [OutputEQ + ADAA True Peak Limiter]
                                                ↓
                                            [Output]
```

### ルームタイプ別トポロジー

| ルームタイプ | SDNトポロジー | ノード数 | FDNとの接続 |
| :--- | :--- | :--- | :--- |
| **Room** | 3D Shoebox (完全結合) | 6 | SDN散乱出力→FDN 16ch入力 |
| **Hall** | 3D Shoebox + サブノード | 8-12 | 同上 + ベルベットノイズFIR密度補完 |
| **Plate** | 2D Waveguide Mesh (隣接結合) | 可変 | メッシュ出力→FDN |
| **Spring** | 1D 散乱チェイン (直列結合) | 可変 | チェイン出力→FDN |
| **Goldfoil** | 2D メンブレンMesh | 可変 | メッシュ出力→FDN |
| **Inchindown** | 3D Shoebox + 短軸ブースト | 6 | 短軸SDNループ強化→FDN早期注入 |

---

## 2. DSP 12項目 実装仕様サマリー

### #1 SDNコアアーキテクチャ
- SDN(初期/中期反射) + FDN(後期残響)ハイブリッド
- CPU負荷: SIMD最適化後 現行比1.5〜1.8倍（実用範囲内）
- `SDNTopology` 基底クラス + CRTP静的ポリモーフィズム

### #2 散乱行列 (SIMD最適化)
- 6×6完全結合: AVX2 FMA命令で5-10サイクル
- SoA (Structure of Arrays) + `alignas(32)` 32バイトアライメント
- ダブルバッファリング + `std::atomic<int>` によるlock-free行列切替
- 後期残響(FDN)側は既存Hadamard FWHTを維持

### #3 SDN物理遅延長計算
- 幾何学的距離 $d$ → 遅延時間 $\tau = d/c$ → サンプル数 $N = \tau \times f_s$
- MFP (平均自由行程 = $4V/S$) 連動
- 共鳴防止: 黄金比 ± 3-7% ランダム摂動
- RoomSizeパラメータ: ワンポールLPFスムージング (20-50ms)
- メモリ: Inchindown最大 474m → 133Kサンプル/ch、全体20MB以内

### #4 SDN境界吸収フィルタ
- $\alpha(f) \to r(f) = \sqrt{1-\alpha(f)}$ のBiquad近似
- 6壁面 × 10バンド = 60 Biquad → SIMD並列化で実質10-15スカラー相当
- 空気吸収フィルタ: ISO 9613-1準拠 (距離依存LPF)
- Inchindown ($\alpha \approx 0.001$): 倍精度演算 + 0.9999クリップで発振防止
- 対向壁面のフィルタ共通化で負荷削減

### #5 ISM (鏡像法) 初期反射
- 1-3次反射 (最大62タップ) をISMで厳密計算
- 4次以降はSDN散乱ネットワークへ委譲
- Hall/Inchindown: ベルベットノイズFIRで密度補完 → NED 50ms到達
- ISMタップにジッター付加でコムフィルタ回避
- 3Dパンニング (Azimuth + Elevation)
- メッセージスレッドで事前計算 → `std::atomic`ポインタ交換でオーディオスレッドへ

### #6 フラクショナルディレイ
- **3次Lagrange (Farrow構造) が最適解** ← Thiran補間を完全廃止
- Thiranの高域位相崩壊 (ESPRIT 11.5dB) → Lagrangeで1.0dB以下に
- Farrow構造: $d$ の冪乗でくくり直し → SIMD FMA一括計算
- LFO変調時もIIR係数更新が不要 → 完全にロックフリー
- SIMD化で2.5-3.5倍のスループット向上

### #7 分散オールパス (Plate/Spring)
- **部分的分散モデル**: SDN→FDN接続部とFDNループ内に集中配置
- 全メッシュノードへの完全配置はCPU破綻のため不採用
- 空間方向SIMD化 (SoA) で8ブランチ並列処理
- Plate: $v \propto \sqrt{f}$ の曲げ波分散をオールパスで近似
- Spring: ねじれ波/縦波カップリングの分散カーブ

### #8 SDN対応モジュレーション
- **Brownian Motion + 等パワークロスフェード** ← DualGoldenLFOを廃止
- 等パワー条件 $g_1^2 + g_2^2 = 1$ でユニタリ性を完全保持
- `SmoothedValue` で1.0cent微分上限をハードリミット → 全テストPASS保証
- FDN側は既存DualGoldenLFOを維持（後期残響の有機的揺らぎ）

### #9 ADAAサチュレーター
- **ADAA = SDNループ内唯一の選択肢** (位相保存、レイテンシーゼロ)
- `juce::dsp::Oversampling` 完全廃止
- 縮小写像定理により $|f'(x)| \le 1$ → エネルギー発散なし
- 桁落ち対策: `_mm256_blendv_ps` によるSIMDブレンドフォールバック
- パデ近似 + SIMD化でスカラー比3.5-4倍高速化

### #10 ダッキング
- FFTスペクトルダッキングは**不採用** (CPU/キャッシュ/IACC維持でリスキー)
- **3-5バンド・クロスオーバーフィルター + 時間領域ダイナミックEQ** を採用
- 位相・空間特性を安定させたまま、低負荷でマスキング帯域をダッキング

### #11 リアルタイム音響指標 (AcousticMetrics)
- バックグラウンドスレッドで疎IR近似計算
- `juce::AbstractFifo` ロックフリーキューでオーディオスレッドへ受渡し
- C80インバースコントロール (目標値→パラメータ自動調整) を実装可能

### #12 アウトプットEQ & Limiter
- ADAA 1次 + 軽量2x OS、またはADAA 2次でエイリアス完全除去
- ゼロレイテンシー Soft Clipper ($\tanh$ → $F(x) = \ln(\cosh(x))$)

---

## 3. 実装フェーズ計画

> [!IMPORTANT]
> 各フェーズ完了時に1,200項目テストを実行し、全PASSを確認してから次フェーズへ進む。

### フェーズ A: 基盤構築（SDNコア + フラクショナルディレイ）
1. `SDNTopology` 基底クラスの定義 (#1)
2. `FarrowFractionalDelayLine` の実装 (#6) ← Thiran廃止
3. 3D Shoebox SDN (6ノード完全結合) の実装 (#1, #2, #3)
4. **1,200項目テスト実行 → 全PASS確認**

### フェーズ B: 境界・反射・モジュレーション
5. SDN境界吸収Biquadフィルタの実装 (#4)
6. ISM初期反射エンジンの実装 (#5)
7. `BrownianModulator` の実装 (#8)
8. **1,200項目テスト実行 → 全PASS確認**

### フェーズ C: 非線形処理・分散
9. `ADAASaturator` の実装 (#9) ← Oversampling廃止
10. 分散オールパス (部分的分散モデル) の実装 (#7)
11. **1,200項目テスト実行 → 全PASS確認**

### フェーズ D: 出力段・周辺機能
12. 3-5バンド・ダイナミックEQダッキングの実装 (#10)
13. ADAA True Peak Limiterの実装 (#12)
14. AcousticMetrics バックグラウンド化 (#11)
15. **1,200項目テスト実行 → 全PASS確認**

### フェーズ E: 2D/1Dトポロジー拡張
16. 2D Waveguide Mesh (Plate/Goldfoil) の実装
17. 1D 散乱チェイン (Spring) の実装
18. Inchindown 短軸ブースト特殊処理の実装
19. **1,200項目テスト実行 → 全PASS確認（最終検証）**

---

## 4. 検証基準（1,200項目テスト）

| 指標 | 許容値 | 根拠 |
| :--- | :--- | :--- |
| ① NED (エコー密度到達時間) | $\le 50.0$ ms | Gaussian分布到達（NED=1）の境界値 |
| ② ESPRIT (共鳴突出度) | $\le 1.0$ dB | 金属的共鳴の人間知覚限界 |
| ③ IACC (ステレオ相関) | $\le 0.10$ | 完全無相関（包み込み感）の上限 |
| ④ Mod (周期性ピッチ揺れ) | $\le 1.0$ cent | ピッチ変動検知閾値(JND)以下 |

テスト対象: 10周波数(40-15000Hz) × 5波形(Sine/Saw/Square/Sync/FM) × 6ルームタイプ = **1,200項目**

---

## 5. SIMD最適化の統一方針

| 対象 | SIMD戦略 | 推定高速化倍率 |
| :--- | :--- | :--- |
| 散乱行列 6×6 | AVX2 FMA (8並列float) | 4.5x (行列単体) |
| Farrow補間 | AVX2 FMA (4ch並列) | 2.5-3.5x |
| 境界Biquad 60個 | SoA 4壁面並列 | 4x (実質15個相当) |
| 分散Allpass | SoA 8ブランチ並列 | 最大8x |
| ADAA Saturator | パデ近似 + blendv | 3.5-4x |
| Brownian Mod | 8ノード並列 | 4-6x |
| **全体スループット** | **SoA + alignas(32) 統一** | **現行比1.5-1.8倍のCPU負荷に収束** |

全バッファ: `alignas(32)` 32バイトアライメント、SoA (Structure of Arrays) レイアウト統一。
