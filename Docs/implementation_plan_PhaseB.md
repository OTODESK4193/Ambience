# Phase B: 16ch FDN ハイブリッド最適化・残響テールの完成

SDNによって生成された「初期〜中期散乱波」を受け取り、各ルームタイプ（特にSpringやInchindownなどの特殊環境）の物理的・音響的特性を完全にシミュレートする「16ch FDN後期残響エンジン」へと昇華させます。3名のサブエージェントによる数学的・音響工学的DeepResearchの結果に基づき、以下の実装を行います。

## User Review Required

> [!IMPORTANT]
> **SpringおよびPlateのリバーブ特性に関する確認**
> 音響物理的に、Springリバーブは「バシャン/ビヨーン」という金属的な分散（チャープ音）を、Plateは密度が高く明るい金属的な響きを意図的に発生させます。
> 本Phaseではこれらを完全に再現するため、特定のオールパスフィルター（群遅延の分散）を意図的に適用します。「意図的な物理的分散」と「粗悪なデジタルリンギング」は数学的に明確に区別して検証ツールで保証（滑らかなスイープ曲線のみを合格とする）しますが、「意図された金属的なキャラクター」が追加される点にご留意ください。

## Open Questions

> [!NOTE]
> 巨大空間「Inchindown（世界最長112秒の残響）」を再現するため、FDNの最大ディレイ長を 1,500ms (1.5秒) まで拡張します。これに伴いプラグインの消費RAM（メモリ）が約 20MB 増加しますが、プロユースの現代的なPC環境であれば全く問題ない水準と考えてよろしいでしょうか？

## Proposed Changes

---

### DSP Algorithms & Presets

#### [MODIFY] [AlgorithmPresets.h](file:///D:/VST_Project/Ambience/Source/AlgorithmPresets.h)
- 6つのルームタイプの `rt60` 帯域比率（10バンド）を、Subagentの調査した**実際の物理素材の減衰比率に完全一致**させます。
  - **Spring**: 低域と高域が物理的制動で早く減衰し、中域が長く残るバンドパス特性（`0.6 / 0.8 / 1.0 / 1.0 / 0.7 / 0.4 / 0.1` ベース）に変更。
  - **Inchindown**: 低中域（125Hz〜250Hz付近）の減衰を極限までゼロに近づけ、驚異的な112秒のロングテールを形成。
  - **Plate/Goldfoil**: 高周波数帯域の急激な吸音カーブを設定。

#### [MODIFY] [UniversalEngine.cpp](file:///D:/VST_Project/Ambience/Source/DSP/UniversalEngine.cpp)
- `ALGORITHM_DELAY_BOUNDS` の更新:
  - **Plate**: より高密度なエコーを得るため、ディレイ長を極端に短縮 (`D_min=2ms, D_max=25ms`)。
  - **Inchindown**: 巨大な空間を表現するため、ディレイ長を大幅に拡大 (`D_min=40ms, D_max=1500ms`)。
- `TOPOLOGY_APF_CONFIGS` (オールパス分散フィルター) の更新:
  - **Spring**: 特有の「チャープ音」を発生させるため、遅延長を 5ms〜15ms の「ストレッチド・オールパス」に延長。
  - **Plate**: エコー密度を急激に上昇させるため、極短のショートオールパス（0.5ms〜2ms）に再設定。
- メモリアロケーション（`prepare`）の拡張: ディレイ線のバッファを最大 2.0 秒（2.0 * fs）まで確保するよう修正。

### Validation Tools

#### [MODIFY] [measure_metrics.py](file:///D:/VST_Project/Ambience/ValidationTools/measure_metrics.py)
- **分散（Dispersion）とデジタルリンギングの識別ロジック追加**:
  - 群遅延（Group Delay）の微分連続性チェックを実装し、Spring/Plateで発生する「滑らかで意図的な周波数スイープ（分散）」は合格としつつ、「時間軸に対して水平に伸びる不連続なデジタルリンギング（定在波）」は依然として ESPRIT > 1.0dB の閾値で厳格にリジェクトする仕様にアップグレードします。

## Verification Plan

### Automated Tests
- 新しい `measure_metrics.py` を用いて、`ValidationRunner.exe` による 1,200項目（6 Room × 10 Freq × 5 Wave × 4 Metric）のテストを再実行します。
- 1つでも閾値をクリアできない場合は、16ch FDNの行列やオールパス係数を再調整し、100% PASS するまで上書きを許可しません。

### Manual Verification
- V2.0.0 を DAW 上に立ち上げ、V1.3.0 と直接比較。
- 特に Spring で Decay を最大にした際の「ビヨーン」という音と長い残響が物理的に正しく表現されているかを聴感でも確認します。
