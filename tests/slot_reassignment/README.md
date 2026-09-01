<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# slot_reassignment/

`driver/common/slots.c`のスロット割当て（PnP到着での動的割当て・`EvtCleanupCallback`での解放・
枯渇時のグレースフルデグラデーション）が、実機のホットプラグ連打やスリープ/レジュームでも
設計通りに動作するかを検証する実機テスト手順。

サードパーティ製ツール`interception-driver-fix`の調査（[`docs/THIRD_PARTY_INTERCEPTION_DRIVER_FIX.md`](../../docs/THIRD_PARTY_INTERCEPTION_DRIVER_FIX.md)）で、
この種の検証が`tests/`配下に存在しないことが判明したために追加した。同ドキュメントの
「未検証事項」を実際に検証するためのテストである。

## `hotplug_monitor`（スロット状態の常時監視ツール）

`hotplug_monitor.cpp`（自作・MITライセンス）は、20個（既定）すべての`\\.\interceptionNN`に対して
定期的に`IOCTL_GET_HARDWARE_ID`を発行し、「どのスロットに、どのハードウェアIDのデバイスが
割り当てられているか」の変化をタイムスタンプ付きで標準出力とログファイルに記録する常駐ツール。
`IOCTL_SET_FILTER`/`IOCTL_READ`/`IOCTL_WRITE`は一切呼ばないため、ストロークを捕捉することはなく
（`OIB_FILE_CONTEXT`のFilterは既定値の0/NONEのまま）、キーボード/マウスは監視中も普段通り使える。
`tests/upstream_lib/`のサンプル群と異なり`interception.dll`には依存しない生プロトコル実装
（`identify3.cpp`と同じ方式）で、`DeviceNameBase`を変更したインストールにも対応できる。

スリープ中はプロセス自体がOSに一時停止させられるだけなので、特別な対応は不要（レジューム後の
次回ポーリングで差分が検出される）。

### ビルド

`OpenInputBridge.sln`のDebug/Release構成に含まれる。単体なら:

```bat
msbuild tests\slot_reassignment\HotplugMonitor.vcxproj /p:Configuration=Release /p:Platform=x64
```

出力先は`tests\slot_reassignment\x64\Release\hotplug_monitor.exe`。

### 使い方

```bat
hotplug_monitor.exe [device-name-base] [poll-interval-ms] [duration-sec] [log-path]
```

- `device-name-base`: 監視対象の`\\.\<base>NN`のベース名（既定: `interception`）
- `poll-interval-ms`: ポーリング間隔（既定: 200）
- `duration-sec`: 指定秒数後に自動終了（既定: 0 = Ctrl+Cで手動停止）
- `log-path`: ログファイルの出力先（既定: `hotplug_monitor.log`）

起動直後に全スロットの初期状態を1行ずつ表示し、以後はスロットの割当てに変化があった行だけを
`[HH:MM:SS.mmm] slot NN (kbd|mou): <旧状態> -> <新状態>`の形式で追記する。停止時（Ctrl+Cまたは
`duration-sec`経過）に、その時点でのアクティブスロット数のサマリを1行出す。

## テストシナリオ

いずれも`hotplug_monitor.exe`をバックグラウンドの別ウィンドウで起動したまま実施し、
終了後にそのログを本READMEの「観察結果」節へ貼り付ける。

**1. 連続抜き差し**

USBキーボード（またはマウス）1台を20〜50回、素早く抜き差しする。都度、ログ上で該当ハードウェア
IDが再割当てされる（`(empty)`を経由して再度スロットに現れる）ことと、抜去中は確実に`(empty)`へ
戻ることを確認する。再割当てまでの遅延が実用上問題ない範囲（体感で1秒未満）であることも確認する。

**2. スロット枯渇からの回復**

レジストリで`KeyboardSlotCount`を小さく（例: 2）設定し、キーボードを3台接続する。3台目は
ログ上どのスロットにも現れない（＝グレースフルデグラデーションで素通しされている）ことを
確認しつつ、実際にそのキーボードで入力できることも確認する。その後1台を抜去し、3台目が
空いたスロットへ即座に（ドライバ再起動なしに）割り当てられることを確認する。

**3. スリープ/レジューム（接続維持）**

キーボード/マウスを挿したままスリープ→復帰し、ログ上でスロット割当てが復帰後も維持されている
（または即座に再割当てされる）ことを確認する。復帰直後に実際に入力が通ることも確認する。

- 対応するスリープ方式（Modern StandbyとS3の両方が使える機体なら両方）ごとに**3〜5回**。
- 揺れ（遅延・再割当て失敗）が一度でも観測された場合のみ、該当方式で20〜30回に増やして
  再現性を確認する。

**4. スリープ前後での抜き差し**

以下の3パターンをそれぞれ**3回ずつ**実施する（回数を稼ぐより、パターンの網羅を優先する）:

- (a) スリープ前に抜いておく → 復帰後に挿す
- (b) 挿したままスリープ → 復帰前（スリープ中）に抜く → 復帰後に挿す
- (c) 挿したままスリープ → 復帰直後、まだ何も操作しないタイミングで抜き差しする

シナリオ1と同じ観点（再割当ての成否・遅延）を確認する。

**5. 長時間の抜き差し繰り返し（スロットリーク検出）**

シナリオ1を100回以上の規模で流し、`hotplug_monitor`の終了時サマリ（アクティブスロット数）が
開始前の状態と一致する（単調に減っていかない）ことを確認する。減っている場合、特定の除去経路
（surprise removal等）で`EvtCleanupCallback`が呼ばれていない疑いがあり、`driver/common/slots.c`
側の追加調査が必要になる。

## 判定基準

- シナリオ1・3・4: 抜去→`(empty)`、再接続→ハードウェアIDが再び現れる、が毎回成立すること。
  再割当てまでの遅延が体感で1秒を大きく超える場合は異常として記録する。
- シナリオ2: スロット枯渇時も入力が素通しされ、スロット解放後は再割当てが自動的に起きること。
- シナリオ5: 開始時と終了時のアクティブスロット数が一致すること。

## 観察結果

（実機検証を実施した際、日付・機体情報とともにここに追記する。`hotplug_monitor.exe`の
ログ全体、またはその要約を貼り付けること。）
