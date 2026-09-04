# 【究極アップグレード統合実装計画書】Ambience 2.0.0
## DSP安定化・Mod音響工学刷新 ＆ GUI 8項目完全実装ロードマップ

本計画書は、徹底的な DeepResearch および公式 1,200 項目テスト（合格率 90.33%）の検証結果に基づき、**「17. Lockボタン」を除くすべての改善項目**を段階的かつ安全に実装するための完全な技術仕様・ロードマップである。

> [!IMPORTANT]
> **本計画書の運用原則:**
> 1. 本計画書はマスタープランとして固定保存され、ユーザーからの明示的な指示がない限り上書き・変更は行わない。
> 2. すべてのフェーズにおいて、コード変更後は必ず **公式 1,200 項目テスト（合格率 90.0% 以上維持）** および **ビルド検証** を実行し、1 ビットのデグレードも許容しない。
> 3. 最初は **DSP関連（Phase 1: 安定性・ジッパーノイズ根絶、Phase 2: Mod音質刷新）** から着手する。

---

## 全体ロードマップ概要

```
[Phase 1: DSP 安定化 ＆ ジッパーノイズ完全根絶]
  │  - 1.1 FDN 遅延線 Fractional 補間化 ＆ 1-pole スムージング
  │  - 1.2 10バンド GEQ 吸音 Biquad のデュアル状態クロスフェード
  │  - 1.3 ER バイパスのフェードアウト化
  ▼  (1,200項目テスト PASS 検証)
[Phase 2: Mod 音響工学刷新 ＆ 究極の透明感]
  │  - 2.1 BrownianModulator シード独立化 ＆ Ornstein-Uhlenbeck (OU) 過程化
  │  - 2.2 FDN ループ内変調 APF の 3次 Farrow / Allpass 補間化
  │  - 2.3 Bounded Safe Modulation のソフトリミッティング
  ▼  (1,200項目テスト PASS 検証)
[Phase 3: GUI 緊急バグ根絶 ＆ 基幹動作修復]
  │  - 3.1 テーマ機能のマルチインスタンスバグ完全根絶（グローバル変数の廃止）
  │  - 3.2 Panic ボタンの Graceful Mute 化（5〜10ms 高速フェードアウト）
  │  - 3.3 Send Mode のパラメータ記憶・復元 ＆ Pre-delay +5ms 位相保護
  ▼  (ビルド ＆ DAW 動作検証)
[Phase 4: プロ仕様 UX ＆ コントロール最適化]
  │  - 4.1 カスタム ArcKnob（ダブルクリック数値入力, Shift微調整, Skew最適化）
  │  - 4.2 Pro Mode トグルのイージング付きスライド＆フェードアニメーション
  │  - 4.3 VU メーター 300ms バリスティクス化 ＆ Gain Reduction メーター追加
  ▼  (UI 描画 ＆ 操作性検証)
[Phase 5: 次世代ビジュアライザー ＆ レスポンシブベクターUI]
  │  - 5.1 3種ビジュアライザーのバックグラウンド FFT スレッド化
  │  - 5.2 完全解像度非依存ベクターレスポンシブ UI（4K 最適化）
  ▼  (全検証完了 ＆ リリース)
```

---

## Phase 1: DSP 安定化 ＆ ジッパーノイズ完全根絶

ノブを激しく動かした際の「クリック音」「ジッパーノイズ」「過渡バンプ」を数理物理的に完全排除する。

### 1.1 FDN 遅延線の Fractional 補間化 ＆ 1-pole スムージング
* **対象ファイル**: `Source/DSP/UniversalEngine.h`, `Source/DSP/UniversalEngine.cpp`
* **現状の課題**: `UniversalEngine.cpp` の FDN ループ内で、遅延線の読み出しに `readInt`（整数丸め）が使われており、`ASYMMETRY` や `SIZE` の操作時にリードポインタが不連続にジャンプしてクリックが発生する。
* **実装仕様**:
  1. `fdnTargetDelaySamples[i]` に対し、サンプル単位または 16 サンプル単位の `1-pole` 低域通過スムーザーを導入：
     $$D_{\text{smooth}}[n] = D_{\text{smooth}}[n-1] + \alpha (D_{\text{target}} - D_{\text{smooth}}[n-1])$$
     （時定数 $\tau \approx 20\text{ms}$、$\alpha = 1 - e^{-1 / (\tau f_s)}$）
  2. `LinearDelayLine` に `readFractional(float delaySmp)` を実装し、1次エルミート補間または線形補間によって小数サンプルを滑らかに補間。
  3. `asymOffset` も同様に平滑化して加算。

### 1.2 10バンド GEQ 吸音 Biquad のデュアル状態クロスフェード
* **対象ファイル**: `Source/DSP/UniversalEngine.h`, `Source/DSP/UniversalEngine.cpp`
* **現状の課題**: `DECAY`, `DAMPING`, `AIR ABSORB` 等を変更すると、IIR (Biquad) フィルタ係数が一瞬で新しい値に上書きされ、内部状態変数との不整合により過渡応答（バンプ・ポップ音）が生じる。
* **実装仕様**:
  1. 各チャンネルに **A / B 2 系統の Biquad フィルタ状態変数** を用意。
  2. 係数更新要求が発生した際、非アクティブ側のフィルタに新係数を設定し、**30〜50ms の等パワークロスフェード**（$\cos / \sin$ カーブ）でアクティブフィルタを切り替える。
  3. これにより、IIR の極（Poles）のジャンプによる過渡エネルギー放出を 100% 抑制。

### 1.3 ER バイパスのフェードアウト化
* **対象ファイル**: `Source/DSP/UniversalEngine.cpp`
* **現状の課題**: `bypassER = (erLevel < 0.01f)` により、ER Level が閾値を下回ると突如演算がスキップされ、末尾が垂直切断されてクリックが発生する。
* **実装仕様**:
  1. `erGain` に対する `SmoothedValue` を常時適用。
  2. バイパス判定は `erSmoothedGain < 0.0001f` かつバッファエネルギー減衰完了後にのみ移行。

---

## Phase 2: Mod 音響工学刷新 ＆ 究極の透明感

人間が知覚するピッチ揺れ（ワーブル、コーラス感）をゼロにしつつ、定在波を破壊して最高峰の密度と透明な広がりを実現する。

### 2.1 BrownianModulator シード独立化 ＆ Ornstein-Uhlenbeck (OU) 過程化
* **対象ファイル**: `Source/DSP/SDNEngine.h`
* **現状の課題**: 全 6 ノードの `BrownianModulator` の乱数シードが同一（`0x12345678`）にハードコードされており、全ノードが完全に同期して揺れている（相関 1.0）。またハードクランプによる波形歪みがある。
* **実装仕様**:
  1. 各ノードに黄金比・素数ハッシュに基づく完全独立シードを付与：
     $$\text{seed}_i = \text{0x12345678} \oplus (i \times \text{0x9E3779B9})$$
  2. ハードクランプを廃止し、物理的平均回帰性を持つ **Ornstein-Uhlenbeck (OU) 過程** に刷新：
     $$x[n] = x[n-1] + \theta (0 - x[n-1]) \Delta t + \sigma \sqrt{\Delta t} \cdot \xi[n]$$
     （$\theta$: 平均回帰強度 0.5Hz、$\xi$: ホワイトノイズ）
  3. これにより、クランプ歪みなしで自然にゼロ近傍を揺らぎ、**IACC（ステレオ無相関度）が劇的に向上**。

### 2.2 FDN ループ内変調 APF の 3次 Farrow / Allpass 補間化
* **対象ファイル**: `Source/DSP/UniversalEngine.h`, `Source/DSP/UniversalEngine.cpp`
* **現状の課題**: FDN ループ内の 3段 Nested APF は `LinearDelayLine`（線形補間）を用いており、変調時に高域が減衰（LPF効果）して透明感が損なわれる。
* **実装仕様**:
  1. 変調 APF のディレイラインに **3次 Lagrange (Farrow 構造)** 補間を導入。
  2. 周波数特性の平坦性を 20kHz まで完全に維持し、モジュレーションを深く掛けても高域が曇らない圧倒的なエア感を達成。

### 2.3 Bounded Safe Modulation のソフトリミッティング
* **対象ファイル**: `Source/DSP/UniversalEngine.cpp`
* **現状の課題**: 変調深さがベース遅延の 40% を超えた際に `std::clamp` でハードクリップされ、高調波歪みが発生する。
* **実装仕様**:
  1. ハードクランプに代わり、滑らかな **$\tanh$ 型ソフトサチュレーション** を適用：
     $$D_{\text{safe}} = D_{\text{base}} + D_{\text{max}} \cdot \tanh\left(\frac{D_{\text{mod}}}{D_{\text{max}}}\right)$$
  2. これにより、極端な設定でも微分連続性が保たれ、クリップ歪みが皆無に。

---

## Phase 3: GUI 緊急バグ根絶 ＆ 基幹動作修復

DAW 上でのクラッシュやマルチトラック干渉、スピーカー破損リスクのあるクリックノイズを緊急修正する。

### 3.1 テーマ機能のマルチインスタンスバグ完全根絶
* **対象ファイル**: `Source/GUI/AmbienceUI.h`, `Source/GUI/AmbienceUI.cpp`, `Source/AmbienceLookAndFeel.h`, `Source/PluginEditor.h`, `Source/PluginEditor.cpp`
* **現状の課題**: `namespace AmbienceColors` にグローバル変数（`inline juce::Colour`）として色が定義されており、DAW 上で複数トラックにプラグインを挿すと、テーマ変更が全インスタンスに干渉・破壊を引き起こす。
* **実装仕様**:
  1. グローバル色変数を完全撤廃。
  2. `AmbienceTheme` 構造体を新設し、`AmbienceLookAndFeel` クラスのインスタンス変数として保持。
  3. 各 `PluginEditor` が独自の `AmbienceLookAndFeel` インスタンスを所有し、独立してテーマを管理。
  4. OS のダークモード / ライトモード自動検知フックを追加。

### 3.2 Panic ボタンの Graceful Mute 化（クリックノイズ根絶）
* **対象ファイル**: `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/DSP/UniversalEngine.h`, `Source/DSP/UniversalEngine.cpp`
* **現状の課題**: Panic ボタンを押すと即座にバッファがゼロクリアされ、垂直切断による「バチッ！」という強烈な衝撃波が発生する。
* **実装仕様**:
  1. `std::atomic<bool> panicRequested{false}` を新設。
  2. オーディオスレッド内でフラグを検知した際、**7ms（336サンプル @ 48kHz）のコサイン・フェードアウトエンベロープ** を出力バッファに乗算。
  3. フェードアウト完了（音量 0）の瞬間、オーディオスレッド安全に `engine.reset()` とバッファクリアを実行。
  4. クリックノイズ完全ゼロの安全な緊急ミュートを達成。

### 3.3 ER Solo / Send Mode のパラメータ記憶・復元 ＆ 位相保護
* **対象ファイル**: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`, `Source/PluginProcessor.cpp`
* **現状の課題**: Send Mode を押すと Wet 0dB / Dry -60dB に上書きされ、解除時に元の設定が復元されない。またセンド時に原音と 0〜5ms の初期反射が位相干渉する。
* **実装仕様**:
  1. `cachedWetLevel`, `cachedDryLevel` 変数を保持し、Send ON 時に保存、OFF 時に完全復元。
  2. Send Mode ON 時は、内部エンジンの Pre-delay に自動で **+5.0ms の下限オフセットガード** を付与し、原音トラックとのコムフィルタリングを音響工学的に自動防止。

---

## Phase 4: プロ仕様 UX ＆ コントロール最適化

日常的なミキシング作業における操作性を、世界のハイエンド商用プラグイン水準へ引き上げる。

### 4.1 カスタム ArcKnob の操作性完全刷新
* **対象ファイル**: `Source/AmbienceLookAndFeel.h`, `Source/PluginEditor.cpp`
* **実装仕様**:
  1. **ダブルクリック数値入力**: `setTextBoxStyle` を編集可能にし、ダブルクリックでテンキー入力エディタ（`Label::showEditor()`）を即座にポップアップ。
  2. **Shift + ドラッグ微調整**: `mouseDrag` において `ModifierKeys::shiftModifier` を検知し、ドラッグ感度を 1/5 に落とす「Fine Tuning モード」を実装。
  3. **Skew カーブ最適化**: Decay (0.1s〜60s) や PreDelay (0ms〜500ms) に対し、`setSkewForCentre(2.0f)` などの対数操作カーブを適用し、中低域側の分解能を最大化。

### 4.2 Pro Mode トグルのイージング付きスライド ＆ フェードトランジション
* **対象ファイル**: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`
* **実装仕様**:
  1. `juce::VBlankAttachment` または `juce::ComponentAnimator` を使用し、リフレッシュレート（60Hz〜144Hz）に同期した 150ms のトランジションを実装。
  2. タブ切り替え時、コンテンツがスッとスライドしながらアルファフェードする極上の UX を実現。

### 4.3 VU メーター 300ms バリスティクス化 ＆ Gain Reduction メーター追加
* **対象ファイル**: `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/GUI/AmbienceUI.h`, `Source/GUI/AmbienceUI.cpp`
* **実装仕様**:
  1. プロセッサー内に 1次 IIR エンベロープフォロワー（アタック 10ms、リリース 300ms）を配備し、VU メーターの針・バーの激しいピクつきを解消。
  2. `DynamicEQDucker` のゲインリダクション値（dB）を取得し、VU メーターの中央に **下向きに伸びるオレンジ/赤の GR（ダッキング）メーター** を追加実装。

---

## Phase 5: 次世代ビジュアライザー ＆ レスポンシブベクターUI

画面拡大時や重い DAW セッション下でも、極めて軽快に動作する次世代描画基盤を構築する。

### 5.1 3種ビジュアライザーのバックグラウンド FFT スレッド化
* **対象ファイル**: `Source/PluginProcessor.h`, `Source/PluginProcessor.cpp`, `Source/PluginEditor.h`, `Source/PluginEditor.cpp`
* **実装仕様**:
  1. メッセージスレッド上での FFT 演算（1024pt）を完全撤廃。
  2. `juce::AbstractFifo` と `juce::TimeSliceThread` を新設し、バックグラウンドスレッドで FFT および周波数平滑化を計算。
  3. GUI は描画バッファを受け取って描画するのみとし、UI カクつきを完全に根絶。
  4. 必要に応じて `juce::OpenGLContext` による GPU オフロードを有効化。

### 5.2 完全解像度非依存ベクターレスポンシブ UI
* **対象ファイル**: `Source/PluginEditor.cpp`, 各 GUI コンポーネント
* **実装仕様**:
  1. `AffineTransform::scale` のズーム依存を段階的に低減し、コンポーネントの `resized()` において相対パーセンテージ（`proportionOfWidth/Height`）による配置を徹底。
  2. 4K / 8K ディスプレイ下でも文字やアーチノブが一切ぼやけない「解像度非依存ベクターレンダリング」を達成。

---

## 検証・品質保証計画

各フェーズの完了ごとに、以下の手順を厳格に実施する：

1. **ビルド検証**:
   ```powershell
   cmake --build build --config Release --target Ambience_VST3 Ambience_Standalone ValidationRunner
   ```
2. **公式 1,200 項目音響物理テスト**:
   ```powershell
   cmd.exe /c "build\ValidationRunner_artefacts\Release\ValidationRunner.exe"
   python ValidationTools/measure_metrics.py
   ```
   * **合格基準**: 全 1,200 項目中 **1,080 PASS（90.0%）以上** を必須とし、1 項目でも異常低下した場合は即座に原因究明と修正を行う。
3. **ノブ急変ストレステスト**:
   * 急激なパラメータ変更下で NaN/Inf、振幅暴発（$> 10.0f$）、クリックノイズが発生しないことを自動シミュレーションで確認。
4. **システムデプロイ**:
   * `C:\Program Files\Common Files\VST3\Ambience2.0.0.vst3` へ最新バイナリを配置。
