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

### 当時の判断: 様子見・保留（2026-08-01付で実施済みに更新）

HLK/WHQL認定の方針（`README.md`のM7参照）でも、「HLKの試験要件次第ではキーボード用/マウス用に
分割する可能性がある」と既に合意済み。今回のpnputilの件は、その分割を後押しする独立した
2つ目の理由になる（HLKの試験カテゴリ分類自体もINFの`Class`宣言に依存している可能性が高く、
`Class=System`のままではHLK側でも正しく分類されない懸念がある）。

分割は相応の作業量（ドライバプロジェクト構成・INF・インストーラ・パッケージングの見直し）を
伴うため、今すぐには着手せず、HLK提出の判断と合わせて改めて検討する。それまでは
`Class=System`のまま現状維持とする。

**2026-08-01追記**: 実際にHLKでテスト対象をSystemクラスに設定すると69件と膨大になり、
Systemクラス向けの広範なテスト一式が(キーボード/マウスフィルタドライバの実態と乖離した)
おそらく通らないパターンだと判断できたため、上記の保留を解除して分割を実施した。`driver/keyboard/`（`keyboard.sys`, `Class=Keyboard`）・
`driver/mouse/`（`mouse.sys`, `Class=Mouse`）の2プロジェクトに分割し、共通ロジック
（`ioctl.c`/`slots.c`/`driver.c`）は`driver/common/`に1本化して両プロジェクトから参照する
構成にした。`installer/`は同じApplet LLCの`kbdaddid`/`mouaddid`（`DriverManager.exe install
keyboard`方式、本番稼働中）に倣い、`OpenInputBridgeSetup.exe`1本のまま引数でドライバ種別を
切り替える形にし、`ModifyUpperFilters`による手動UpperFilters編集はそのまま流用した
（`kbdaddid`/`mouaddid`のINFにある宣言型`AddFilter`は、実際のインストーラでは使われておらず
採用しなかった）。詳細はplan `glowing-floating-pelican`参照。実際にHLKへ再提出してテスト件数が
減るかどうかの確認はフォローアップ。

---

## 2026-07-30: 別プロジェクト（nodokad2、WHQL署名取得済み）のエージェントによるコードレビュー

Applet LLCの別のキーボードフィルタドライバプロジェクト`nodokad2`（`C:\Users\applet\Documents\
GitHub\nodoka\nodoka\d2`、WHQL署名取得済み）を開発しているエージェントに、OpenInputBridgeの
ドライバコードを比較レビューしてもらい、2点の助言を得た。

### ① IOCTL_WRITE経路でClassServiceをPASSIVE_LEVELのまま直接呼んでいた（対応済み）

`driver/ioctl.c`の`OibCtlHandleWrite`は、チェーンを最後まで落ちたストロークを実際の
`ClassService`（kbdclass/mouclassの本来のコールバック）に直接届けるが、この呼び出しは
IOCTLハンドラのコンテキスト＝**PASSIVE_LEVEL**で行われていた。一方、実ハードウェア経由の
呼び出し（`kbdfilter.c`/`mousefilter.c`の`ServiceCallback`経由）は、ポートドライバの
ISR紐付きDPCから呼ばれるため常に**DISPATCH_LEVEL**である。`kbdclass`やwin32kのRaw Input
Managerが「実ハードウェア経由の呼び出しは必ずDISPATCH_LEVELである」という暗黙の前提を
どこかで置いている可能性があり、`nodokad2`は同種の直接呼び出し（INJECT経路）の前に
明示的に`KeRaiseIrql(DISPATCH_LEVEL, &old)`している。

対応: `OibCtlHandleWrite`の`ClassService`呼び出し（キーボード・マウス両方）を
`KeRaiseIrql(DISPATCH_LEVEL, &oldIrql)` / `KeLowerIrql(oldIrql)`で挟むように修正した。

### ② PAGEDコード＋スピンロックの陥穽（現状は該当しないが将来の注意点として記録）

`nodokad2`はHLKテストで3回連続BSOD（IRQL_NOT_LESS_OR_EQUAL, 0xd1）を経験した。原因は
`EvtDeviceAdd`/`EvtFilterCleanup`を`#pragma alloc_text(PAGE, ...)`でページアウト対象に
したまま、その中でスピンロック保持中（＝DISPATCH_LEVEL中）に`InsertTailList`等を実行して
いたこと。スピンロック保持中にコード自体がページアウトされていると、命令フェッチ自体が
ページフォールトを起こし、DISPATCH_LEVEL以上でのページフォールトは即BSODになる。

OpenInputBridgeの`driver/*.c`には現状`alloc_text`が一切無く、該当しないことを確認済み。
ただし将来パフォーマンス最適化等で`OibKbdEvtDeviceAdd`/`OibMouEvtDeviceAdd`や
`OibKbdEvtFilterDeviceCleanup`/`OibMouEvtFilterDeviceCleanup`を`PAGE`指定する場合は、
`OibSlotAssign`/`OibSlotRelease`（スピンロック区間）を非ページの別関数に切り出す必要がある。
`driver/kbdfilter.c`・`driver/mousefilter.c`の該当関数に、この注意点をコメントとして
残している。

---

## 2026-07-31: USBキーボード多数接続時にWindowsが起動しない件 → OpenInputBridgeとは無関係と確認

実機テストで、USBキーボードを10個程度接続したままWindowsを起動できない事象が報告された。
コードレビューでは、`OibKbdEvtDeviceAdd`（`driver/kbdfilter.c`）・`OibSlotAssign`
（`driver/slots.c`）ともデバイス数に応じて悪化するブロッキング待機・デッドロック・無限ループの
類は見当たらず（スロット枯渇時も`OIB_SLOT_INDEX_NONE`が正しく設定され、素通しフィルタとして
安全に動作を続ける設計・実装になっている）、`OIB_KEYBOARD_SLOT_COUNT=10`という値との一致は
気になったものの、コード上の根拠は見つからなかった。

その後、**OpenInputBridgeドライバを完全にアンインストールした状態でも同じ事象が再現する**
ことが実機で確認された。したがってこれはOpenInputBridge固有の不具合ではなく、テスト環境
（Windows自体、あるいはUSBホストコントローラ/ハブ側）に起因する問題と判断してよい。

`bcdedit /set bootlog yes`による起動ログ（`ntbtlog.txt`）は、キーボードを抜いて起動に
成功した際のログしか取得できず（起動できなかった試行そのもののログは残らない）、
イベントビューアーもドライバ未インストール状態のため参照先が無く、これ以上の原因究明は
本プロジェクトのスコープ外として現時点では追わない。

---

## 2026-08-02: キーボード/マウスの配分を可変にする + 新規クライアント向け発見用IOCTLの追加

### 背景

2026-08-01の分割後、`keyboard.sys`/`mouse.sys`のどちらか片方だけをインストールすることが
技術的に可能になった。しかし本家Interceptionのユーザーモードライブラリ`interception_create_context()`
は`\\.\interception00`〜`19`の20個すべてのオープン成功を要求するため（`docs/PROTOCOL.md`）、
片方が無い状態では本家ライブラリを使うアプリは（キーボードだけ/マウスだけを使いたいアプリで
あっても）丸ごと動作しなくなる。

### 検討した対策と採用しなかった案

「不在検出＋もう片方の範囲もダミーのコントロールデバイスとして肩代わりする」案も検討したが、
これは起動時に相手ドライバの名前衝突を検出して握りつぶす、という相互検出ロジックが
`driver/common/driver.c`に必要になり、かつ「片方だけ入れて起動→再起動せずにもう片方を追加
インストール」という順序で先に居座ったダミーが本物の名前を明け渡さない、という順序依存の
落とし穴があったため見送った。

### 採用した設計

「両方のドライバは常にインストールされている」ことを前提に維持しつつ、キーボード/マウスへの
20個の配分を可変にする方式を採用した。`KeyboardSlotCount`という1つのレジストリ値
（`REG_DWORD`、両サービスの`Parameters`キーに同じ値を書く）だけを持ち、マウス側の配分は
常に`20 - KeyboardSlotCount`として導出する。値が1つしかないため、2つの独立したドライバ間で
配分が食い違って番号が衝突・欠落する事故が構造的に起こらない。インストーラの
`OpenInputBridgeSetup.exe install keyboard|mouse --slots=N`で設定する。

`driver/common/slots.c`の配列は`OIB_TOTAL_DEVICE_SLOT_COUNT`（常に20）で確保し、実際に使う
範囲だけを実行時の`ActiveSlotCount`で制御する形にしたため、動的メモリ確保は不要だった。

**互換性への影響**: 本家ライブラリの`interception.h`は0〜9=キーボード/10〜19=マウスという
境界をコンパイル時に固定しているため、既定(10/10)以外の配分にすると、この仕様を知らない
古いクライアントは境界のズレた範囲を誤ったデバイス種別として扱い続ける。オープン自体は
失敗しないため、クラッシュにはならないが、捕捉ビットが一致せず何も捕まらない・データを
誤った構造体として解釈するといったサイレントな誤動作になる（詳細は`docs/PROTOCOL.md`）。

### 新規クライアント向け発見用IOCTLの追加

上記の互換性問題に新規クライアント/ライブラリが安全に対応できるよう、`IOCTL_GET_HARDWARE_ID`
と同じ「本家プロトコルには無いOIB独自拡張」という位置づけで2つ追加した
（`driver/common/ioctl.h`/`ioctl.c`、`docs/PROTOCOL.md`）。

- `IOCTL_GET_KEYBOARD_SLOT_COUNT`（0x900）: 現在のキーボード側配分個数を返す。
- `IOCTL_GET_DRIVER_IDENTITY`（0xA00）: 署名(`Signature`)・バージョン・種別を返す。本物の
  Interceptionドライバはこのコードを実装していないため失敗するはず、という設計だが、
  **これは本物のドライバの実挙動を検証したものではない未確認の前提**である（クリーンルーム
  方針上、本物のドライバの実挙動を調べること自体を避けているため）。`docs/PROTOCOL.md`に
  ブラックボックス検証待ちとして明記した。

バージョン値（`FileVersion`等、`keyboard.rc`/`mouse.rc`）と、このIOCTLが返すバージョン値が
将来ズレないよう、両方が参照する`driver/common/version.h`を新設して一元化した
（リソースコンパイラが`ntddk.h`/`wdf.h`を読み込まずに済むよう、`driver.h`とは独立した
依存の無い小さいヘッダにした）。

詳細はplan `glowing-floating-pelican`参照。
