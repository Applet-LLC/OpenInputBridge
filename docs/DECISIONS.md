<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# 検討事項ログ

保留中・様子見にしている設計判断を記録する。クリーンルーム境界の証跡（`CLEAN_ROOM.md`）とは別に、
「何を検討し、なぜ今は着手しないことにしたか」を追跡するためのドキュメント。

---

## 2026-07-30: `pnputil /enum-drivers /class keyboard`/`/class mouse` にOpenInputBridgeが出ない

### 症状

```
pnputil /enum-drivers /class keyboard
pnputil /enum-drivers /class mouse
```

のどちらの出力にも、インストール済みのOpenInputBridgeが表示されない。インストール自体は正常
（サービス登録・UpperFilters登録とも実機で確認済み、`sc.exe query OpenInputBridge`はRUNNING）。

### 原因

`pnputil /enum-drivers /class <name>` は、Driver Store内の各ドライバパッケージが**INFの
`[Version]`セクションで宣言している`Class`**で絞り込む。`driver/OpenInputBridge.inx`は

```ini
Class       = System
ClassGuid   = {4d36e97d-e325-11ce-bfc1-08002be10318}
```

と「System」クラスで宣言しているため、`keyboard`/`mouse`どちらの絞り込みにも該当しない。
実際にKeyboard/Mouseクラスへの`UpperFilters`登録（＝実際の機能）ができているかどうかとは
別問題で、あくまでDriver Storeにおけるパッケージの分類上の見え方の問題。

### 半端な対策では直らない理由

1つのINFには`Class`を1つしか宣言できない。`Class=Keyboard`に変更すれば`/class keyboard`には
出るようになるが、`/class mouse`には引き続き出ない（逆も同様）。OpenInputBridgeは現在
**1バイナリ・1 INFでキーボード/マウス両クラスのUpperFiltersに登録する**設計のため、この制約に
直接ぶつかる。

### 調査結果: 正しい直し方は判明している（`AddFilter`宣言型フィルタ + ドライバ分割）

Applet LLCの別プロジェクト`kbdaddid`/`mouaddid`（本番稼働中）のINFを確認したところ、
この問題を構造的に解決する構成になっていた。

```ini
; kbdaddid.inf 抜粋
Class=Keyboard
ClassGuid={4D36E96B-E325-11CE-BFC1-08002BE10318}
...
[DefaultInstall.NTamd64.Filters]
AddFilter=kbdaddid,, kbdaddid_UpperFilter

[kbdaddid_UpperFilter]
FilterPosition=Upper
```

これはMicrosoft公式の「宣言型フィルタ」の仕組み（Windows 10 1903+）で、`UpperFilters`
レジストリ値を直接書き換える代わりに、INFのメタデータからOSがフィルタ一覧を組み立てる。
`[Manufacturer]`/`[Models]`が無いプリミティブドライバ（`[DefaultInstall.NTamd64.Filters]`
のように`DefaultInstall`配下で使う形）でも、クラス単位のアッパーフィルタ登録に使えることが、
`kbdaddid`/`mouaddid`の実運用で証明されている。

参考:
- [INF AddFilter Directive](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addfilter-directive)
- [INF DDInstall.Filters Section](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-ddinstall-filters-section)
- [Device Filter Driver Ordering](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/device-filter-driver-ordering)

この方式に乗り換えると、以下が同時に得られる。

- `pnputil`上も正しいクラス（Keyboard/Mouse）に表示されるようになる
- `installer/common.cpp`の自前`ModifyUpperFilters`（`kbdclass`/`mouclass`直前への位置調整・
  インスタンスサブキー同期など、正しく実装するのに何度か実機バグ修正が必要だったロジック）が
  丸ごと不要になる。INF/PnPが宣言型フィルタとして自動的に処理するため

ただし、前述の「1 INFにつきClassは1つ」という制約により、この方式を採用するには
**キーボード用・マウス用でドライバパッケージ（INF、場合によってはサービス/バイナリも）を
分割する**必要がある。

### 現時点の判断: 様子見・保留

HLK/WHQL認定の方針（`README.md`のM7参照）でも、「HLKの試験要件次第ではキーボード用/マウス用に
分割する可能性がある」と既に合意済み。今回のpnputilの件は、その分割を後押しする独立した
2つ目の理由になる（HLKの試験カテゴリ分類自体もINFの`Class`宣言に依存している可能性が高く、
`Class=System`のままではHLK側でも正しく分類されない懸念がある）。

分割は相応の作業量（ドライバプロジェクト構成・INF・インストーラ・パッケージングの見直し）を
伴うため、今すぐには着手せず、HLK提出の判断と合わせて改めて検討する。それまでは
`Class=System`のまま現状維持とする。
