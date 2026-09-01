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
採用しなかった）。実際にHLKへ再提出してテスト件数が減るかどうかの確認はフォローアップ。

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

---

## 2026-08-02: `keyboard.sys`/`mouse.sys`がWindows標準搭載ファイルと名前衝突 → `oib_kbd.sys`/`oib_mou.sys`に改名

### 症状

実際のHLKテストクライアントで`OpenInputBridgeSetup.exe`を実行すると、
`DiInstallDriverW`自体は成功する（「Staged the OpenInputBridgeKeyboard driver package into
the Driver Store.」まで出力される）にもかかわらず、続く
`SetupInstallServicesFromInfSectionW`が`3758096641`（16進`0xE0000101` =
`ERROR_SECTION_NOT_FOUND`）で失敗した。

### 原因

`installer/install.cpp`の`FindStagedInfPath`は、Driver Store内の
`%windir%\System32\DriverStore\FileRepository\keyboard.inf_*`をワイルドカード検索して
ステージ済みINFを見つける実装だが、このテストクライアントには**2つ**の
`keyboard.inf_<hash>`フォルダが存在していた。1つは今回ステージしたばかりの
OpenInputBridge自身のもの、もう一つは**Windowsに標準搭載されているPS/2キーボードの
インボックスドライバ**（`i8042prt.sys`/`kbdclass.sys`/`kbdhid.sys`を伴う、OSセットアップ時
からDriver Storeに存在するもの）だった。`keyboard.inf`という、いかにもありそうな安易な
ファイル名を選んだことが、Windows標準搭載ファイルとの衝突を招いた。これは特定のテスト
環境固有の問題ではなく、**あらゆるWindows環境で起こりうる衝突**である（`mouse.inf`側も
同様の標準搭載ファイルが存在するため、同じ問題を抱えていた）。

（この調査の過程で、`FindStagedInfPath`が複数該当時に「列挙順で最後に見つかったもの」を
採用していた点も別途修正し、「最終更新日時が一番新しいもの」を選ぶようにした——これは
衝突そのものの直接原因ではなかったが、あわせて直しておくべき頑健性の問題だった。）

### 対応

ドライバファイル名を、キーボード側は`oib_kbd.sys`、マウス側は`oib_mou.sys`に改名した
（`.inf`/`.cat`も同様、`.vcxproj`/`.inx`/`.rc`ソースファイルも一貫性のため合わせて改名）。
サービス名（`OpenInputBridgeKeyboard`/`OpenInputBridgeMouse`）はこの衝突と無関係なため
変更していない。

Interception互換性は`\\.\interceptionNN`というデバイス名とワイヤプロトコル（IOCTL）の
みで決まり、ドライバファイル名・サービス名・INFファイル名とは完全に無関係なため、
この改名によって互換性への影響は一切ない。

---

## 2026-08-08: 監査ログ（SACL）・トースト通知機能の設計

### 背景

`docs/SECURITY_CONSIDERATIONS.md`第5節の未着手事項「監査ログ・使用状況の可視化」について、
デバイスドライバ本体（`driver/`）を一切改変せずに実現する方式を検討した。あわせて、将来の
拡張候補として利用者への通知（トースト）機能もオプションとして設計に含めた。

### 採用した設計

- **SACL方式**: `\\.\interceptionNN`の各コントロールデバイスに`SetNamedSecurityInfoW`で
  SACL（監査ACE）を付与し、`auditpol /set /subcategory:"Kernel Object"`と組み合わせて
  Windows標準のオブジェクトアクセス監査（セキュリティイベントログ4656/4663）で記録する。
  ドライバの署名・カタログ・WHQL認定には一切触れない、完全にドライバ外側の変更で完結する。
- **SACLの再適用はタスクスケジューラで行う（常駐サービス化しない）**: SACLはコントロール
  デバイスオブジェクトが再作成されるたび（＝ドライバサービス起動のたび）に消えるため、
  SCM（サービス制御マネージャ）のサービス開始イベント（Event ID 7036、対象サービス名で
  フィルタ）をトリガーとするタスクスケジューラのタスクで再適用する。イベントドリブンな
  ワンショット実行で足りるため、常駐ポーリングサービスは不要と判断した。
- **トースト通知はユーザーセッション内で動くタスクとして実装する**: LocalSystemサービスは
  Session 0分離のため、ログオン中ユーザーの対話セッションに直接UIを出せない
  （`WTSQueryUserToken`+`CreateProcessAsUser`によるセッション越境が必要になり複雑化する）。
  代わりに、該当セキュリティイベント（4656/4663、対象デバイス名でフィルタ）をトリガーとし、
  「ユーザーがログオンしている場合のみ実行」に設定したタスクスケジューラのタスクとして実装する。
  これは自動的にログオン中ユーザーのセッション内で実行されるため、追加のセッション越境処理が
  不要になる。
- **トーストは専用AUMID（`OpenInputBridge.AuditNotifier`）+スタートメニューショートカットで
  「OpenInputBridge」名義表示する**: PowerShellの既存AUMIDを間借りする案（実装コストは最小）
  も検討したが、「Windows PowerShell」名義で表示されてしまい商用版（Pro/Subscription）の
  ブランディング上望ましくないため不採用。小さなネイティブヘルパー
  （`installer/toast-helper/OibToastHelper.exe`、C++/WinRT）を新設し、`HKCU\Software\Classes\
  AppUserModelId\...`へのAUMID登録とスタートメニューショートカットの両方を用意する方式を採用した。
- **実装箇所の一元化**: 3リポジトリ（OSS版/Pro/Subscription）とも、実際のドライバインストール
  処理は既にOSS版`installer/`の`OpenInputBridgeSetup.exe`に一本化されており、Pro/Subscriptionは
  ビルド成果物をそのまま同梱・CustomActionから呼び出すだけの構成になっている。今回の監査ログ・
  トースト機能もこの慣習に倣い、実体はOSS版`installer/`に一度だけ実装し、Pro/Subscriptionの
  WiXインストーラーはビルド成果物をコピーしてオプション機能（Feature）として公開するのみとする。

### 検討したが不採用にした案

1. **DACLの絞り込み（`Everyone`権限の制限）**: 本家Interceptionプロトコル互換性（無昇格
   プロセスからの利用）と両立しないため不採用。インストール後に管理者権限の外部プロセスが
   `SetNamedSecurityInfoW`で動的にDACLを絞り込む案も検討したが、絞り込みが適用されるまでの
   隙（TOCTOU）が原理上残ることと、外付けの保護ゆえに迂回・無効化されやすいことから見送った。
2. **消費側プロセスの許可リスト/署名検証（ユーザーモードでの事前ブロック）**: ハンドル
   オープン自体を防ぐ真の予防的アクセス制御は本来カーネル側（`IRP_MJ_CREATE`）でしか実現
   できない。ユーザーモードでのハンドル列挙ポーリングによる事後検知（開かれたことを検知して
   プロセスを強制終了する等）も検討したが、開いてから気づくまでのタイムラグがあり防御力が
   弱く、かつ常駐ポーリングプロセスが必要になるため、今回は見送った。
3. **SACL再適用を常駐Windowsサービス化する案**（Subscription版の`OpenInputBridgeLicenseSvc`
   に倣う案）: `OpenInputBridgeLicenseSvc`は24時間ごとのサブスクリプション再検証という
   無関係な責務のポーリングサービスであり、流用すると責務が混在する。SACL再適用はサービス
   起動時の1回で完結するイベントドリブンな処理のため、軽量なタスクスケジューラのイベント
   トリガー方式を採用し、常駐サービス化は不採用とした。
4. **トースト表示をLocalSystemサービスから直接行う案**: 上記「採用した設計」に記載の通り、
   Session 0分離により技術的に大幅な複雑化を伴うため不採用。
5. **トーストのAUMIDをPowerShellの既存AUMIDに間借りする案**: 実装コストは最小だが、通知の
   発行元表示が「Windows PowerShell」になってしまい、商用版のブランディング上望ましくないため
   不採用（詳細は上記「採用した設計」参照）。
6. **デバイスドライバ（カーネル側）に機能を追加する案**: 将来の選択肢として記録するに留め、
   現時点では不採用とした。
   - `EvtIoDeviceControl`やファイル作成コールバックで呼び出し元プロセスを検査し、許可リスト
     外を`STATUS_ACCESS_DENIED`で拒否する、真の予防的アクセス制御が可能になる
   - ETW（`EventWrite`）で汎用のWindowsセキュリティ監査より高速・高粒度なログを直接出せる
   - 一方で、ドライバの変更はWHQL再申請・再署名（`docs/DECISIONS.md`の各種WHQL関連の教訓が
     示す通り相応の手間とリードタイムを要する）を必要とし、`docs/CLEAN_ROOM.md`が定める
     「プロトコル忠実・最小限」というドライバの設計方針からも逸脱する。ユーザーモード側の
     変更だけで目的（利用状況の可視化・利用者への通知）を達成できる以上、現時点でドライバに
     手を入れる理由はないと判断した。将来、真の予防的アクセス制御が必要になった場合の選択肢
     として記録する。

**2026-08-09追記**: `--enable-audit-log`をインストーラーのMSIから呼ぶ場合の不具合を発見・修正した。
`install.cpp`の`SetupInstallServicesFromInfSectionW`呼び出しは`SPSVCINST_STARTSERVICE`フラグを
渡していないためドライバサービスを即座には起動せず、`SERVICE_SYSTEM_START`型のサービスは
次回起動まで実行される保証がない。そのため、ドライバインストールと同一のMSIトランザクション内で
監査ログ機能を有効化しようとすると、この時点では`\\.\interceptionNN`が1つも存在せず、
`RunEnableAuditLog`(`installer/auditlog.cpp`)が「対象デバイス0件」を即座にエラー扱いにして
`1`を返し、`CA_EnableAuditLog`が`Return="check"`のため**MSIインストール全体が失敗する**という
不具合があった。デバイス0件は再起動前のインストール直後には正常に起こり得る状態であり、
再適用タスク(SCM Event 7036トリガー)がドライバの初回起動時に自動的に拾ってくれるため、
このケースをエラーではなく情報メッセージに変更した(auditpolの有効化・タスク登録自体は
デバイスの有無と無関係に成功するため、そちらは変更していない)。`RunApplyAuditSacl`
(再適用タスク自身が呼ぶ内部コマンド)側は、7036イベント発火時=ドライバが起動しているはずの
タイミングで動くため、デバイス0件を引き続きエラーとして扱う。

**2026-08-09追記(2)**: `--enable-audit-log`実行時に`auditpol.exe exited with code 87`
(`ERROR_INVALID_PARAMETER`)で失敗する不具合を、日本語版Windows実機での検証により発見・修正した。
`auditpol.exe`の`/subcategory`パラメータは、サブカテゴリの**ローカライズされた表示名**と照合される
仕様であり、`"Kernel Object"`という英語名は英語版Windowsでしか通らない。日本語版では
`auditpol /get /subcategory:"Kernel Object"`のように英語名を渡すと、パラメータ自体を認識できずに
使い方(Usage)メッセージとともにエラー87を返すことを実機で確認した(`auditpol /get
/subcategory:{0CCE921F-69AE-11D9-BED3-505054503030}`のようにGUID形式で渡すと正しく解釈され、
未昇格プロセスからの実行時は代わりに`ERROR_PRIVILEGE_NOT_HELD`(1314)になることも確認済み ——
GUID形式ならパースは通り、あとは権限の問題だけになるということ)。`auditpol`はWindows Vista以降
`/subcategory:{GUID}`形式をサポートしているため、`installer/auditlog.cpp`の
`SetKernelObjectAuditSubcategory`を、英語名の直書きからロケール非依存の固定GUID
(`{0CCE921F-69AE-11D9-BED3-505054503030}`、"Kernel Object"サブカテゴリの既知の固定GUID)を
使う形に変更した。Subscription版の同ファイルにも同じ修正を反映済み。

**2026-08-09追記(3)**: 上記2件の修正後、実機(再起動込み)で監査ログ機能一式が有効化される
ところまでは確認できたが、再起動後に`identify2.exe`(`tests/upstream_lib/`のサンプル)で
`\\.\interceptionNN`を開いてもセキュリティイベントログに記録が追加されず、トーストも
表示されないという報告があった。調査したところ、**再適用タスクが一度も発火していない**
ことが`schtasks /query /tn OpenInputBridgeAuditLogReapply /v /fo list`の「前回の実行時刻:
1999/11/30 0:00:00」「前回の結果: 267011」(`SCHED_S_TASK_HAS_NOT_RUN`、Task Scheduler自身が
使う「このタスクは一度も実行されていない」という定数)から判明した。

さらに実機の`Get-WinEvent`でSystemログを直接確認したところ、根本原因が判明した:
**`OpenInputBridgeKeyboard`/`OpenInputBridgeMouse`は`SERVICE_SYSTEM_START`型のカーネル
ドライバであり、通常のWin32サービスのようにSCMの`StartService`経由で起動するのではなく、
カーネル初期化のごく早い段階でI/Oマネージャーが直接ロードする**。そのためSCMが「自分が
起動させた」という状態遷移を認識せず、Event 7036(サービスが実行状態になった)自体が
このタイプのドライバに対しては一度も記録されないことを確認した(実機のSystemログには
同じService Control Managerプロバイダーの7023・7026(異常系イベント)は記録されているのに
7036だけが一切無かった)。前提としていた「サービス起動のたびに7036が発火し、それを
再適用タスクが拾う」という設計自体が、このドライバの起動方式には当てはまらなかった。

**対応**: 再適用タスクのトリガーを、Event 7036ベースの`EventTrigger`から、Windows起動のたびに
実行される`BootTrigger`に変更した。`SERVICE_SYSTEM_START`型ドライバはカーネル初期化のごく
早い段階でロードされるため、タスクスケジューラー自身のサービスが起動してブートトリガーを
評価する頃には、ドライバは確実に起動済みになっている。`installer/auditlog.cpp`の
`BuildReapplyTaskXml`を書き換え、もはや使われなくなったサービス表示名の定数
(`KeyboardServiceDisplayName`/`MouseServiceDisplayName`)も削除した。Subscription版の
同ファイルにも同じ修正を反映済み。既存のタスクは古い(発火しない)定義のまま残ってしまうため、
この修正を反映したビルドで`--disable-audit-log`→`--enable-audit-log`と再実行し、タスクを
新しい定義で登録し直す必要がある。

---

## 2026-08-09: OSS版のGUIインストーラーを廃止しCLIに一本化

### 背景

2026-08-08エントリの実装として、OSS版にも(Pro/Subscription版に倣い)`setup/`配下にWiX v3 MSIの
GUIインストーラーを新設していた。しかしOSS版では以下の理由でCLIインストーラー
(`OpenInputBridgeSetup.exe`)単体で十分と判断し、GUIインストーラーを削除した。

- 監査ログ・トースト通知の有効化を含め、GUIができることはCLIの引数(`--enable-audit-log`
  `--enable-toast`等)で全て実行可能であり、機能的な差はない
- OSS版は`ja-JP`/`en-US`の2つの独立したMSIを別々にビルドする構成だったため、利用者が自分の
  使用言語に応じてどちらを実行するか手動で選ぶ必要があり、UXとして煩雑だった(単一のMSIで
  OS言語に応じて自動選択する構成にする手もあったが、そこまでの作業コストに見合う効果が
  OSS版には無いと判断)
- Pro/Subscription版はライセンスキー入力ダイアログ等、GUIでなければ成立しないUIを持つため
  MSIを維持する必要があるが、OSS版にはその種の要件が無い
- 技術者向けのドライバツールという性格上、「Programs and Features」への登録や標準的な
  アンインストールUIが無いことは許容範囲と判断した

### 削除したもの

- `setup/`ディレクトリ一式(`Product.wxs`・`OpenInputBridgeInstaller.wixproj`・`Lang/`・
  `License.*.rtf`・`Prepare-Staging.ps1`)
- `OpenInputBridge.sln`からの`OpenInputBridgeInstaller`プロジェクトエントリ
- `packaging/sign.mak`の`msi`/`msi-build`/`sign-msi`/`stage-msi`ターゲット(`all`/`whql`からの
  依存も含め、2026-08-08時点で追加していたもの一式)

### スタートメニューショートカットの移設

トースト通知のAUMID(`OpenInputBridge.AuditNotifier`)には、非パッケージ型デスクトップアプリの
場合スタートメニューショートカットの存在が期待される(2026-08-08エントリ参照)。従来はこの
ショートカット作成をWiXの`<Shortcut>`要素に任せていたが、GUIインストーラーを廃止すると
作成主体が無くなる。そこで`installer/toastsetup.cpp`の`RunEnableToast`自体が`IShellLink`/
`IPersistFile`(COM)を使って直接`.lnk`を作成するように変更した
(`CreateStartMenuShortcut`/`RemoveStartMenuShortcut`)。作成先は全ユーザー共通の
`FOLDERID_CommonPrograms`配下の`OpenInputBridge`フォルダで、対象(`OibToastHelper.exe`)に
埋め込み済みのアイコンがそのまま使われる。

この変更は`installer/toastsetup.cpp`という共有ソースに対するものなので、Pro/Subscription版にも
自動的に波及する。Pro/Subscription版のWiXインストーラー側で従来作っていた同等のショートカット
(`CMP_ToastHelperShortcut`)は、CLI側が作るようになったショートカットと重複するため、
Pro/Subscription両方の`Product.wxs`からも削除した(Pro版は他に用途の無かった
`ProgramMenuFolder`ディレクトリごと削除。Subscription版はライセンスGUIのショートカットが
同じ`PROGRAMMENUFOLDER`を使い続けるためディレクトリ自体は残した)。

### README・LICENSEの同梱

OSS版の配布zip(`packaging/dist/OpenInputBridge.zip`)に`README.md`・`LICENSE`を同梱するように
`packaging/sign.mak`の`stage-bin`を拡張した。GUIインストーラーが無くなり、EULA表示等の
「利用前に必ず目を通させる」UIも無くなったため、zipを展開しただけで最低限の情報
(ライセンス・使い方)にアクセスできるようにする狙い。

---

## 2026-08-10: 監査ログが有効化後も記録されない問題(レガシー監査ポリシーとの競合)

### 症状

2026-08-09の2件の修正(auditpolのGUID化、再適用タスクのBootTrigger化)適用後、実機で
`--enable-audit-log`が全ステップ成功し、再起動後に再適用タスクも実際に発火(前回の結果: `0`)
することを確認できた。しかしそれでもなお、`identify2.exe`(`tests/upstream_lib/`)で
`\\.\interceptionNN`を開いてもセキュリティイベントログにイベントID 4656/4663が記録されず、
トーストも表示されなかった。

一方で、以前は無かったはずの「資格情報マネージャーの資格情報が読み取られました」
(イベントID 5379、読み取り操作: 資格情報の列挙)というイベントが大量に記録されるように
なっていることが分かった。5379は「Kernel Object」ではなく「その他のオブジェクトアクセス
イベント」サブカテゴリに属するイベントである。

### 原因調査

まず`auditpol /list /subcategory:* /r`(CSV形式)の出力を実機で確認し、GUID
`{0CCE921F-69AE-11D9-BED3-505054503030}`が実際に「Kernel Object」を指していること自体は
確認できた(前後のGUID`{0CCE921D}`=File System、`{0CCE921E}`=Registry、直後の
`{0CCE9220}`=SAM(この名前だけは非日本語文字列のため直接読めた)という並び順から、
GUIDの取り違えではないと判断できる)。

次に`HKLM\SYSTEM\CurrentControlSet\Control\Lsa\SCENoApplyLegacyAuditPolicy`を確認したところ、
値が存在しなかった(未設定)。Windowsには「詳細な監査ポリシー」(`auditpol`が操作する、
サブカテゴリ単位の設定)と「レガシーな基本監査ポリシー」(ローカル/グループポリシーの
9個の大分類、例: 単純な「オブジェクトアクセスの監査」のオン/オフ)という2系統の監査ポリシーが
並存しており、`SCENoApplyLegacyAuditPolicy`(ローカルセキュリティポリシーの「詳細な監査ポリシー
の構成を上書きする」に相当)が有効になっていないと、`auditpol`でサブカテゴリを個別に設定しても
意図通りに反映されない(あるいはレガシー側の設定と競合して予期しない挙動になる)ことが
Microsoft自身により文書化されている。今回の症状(狙ったサブカテゴリのイベントが出ず、
無関係なサブカテゴリのイベントだけ大量に出る)は、この競合が起きた場合の典型的な症状と一致する。

**不確かな点**: この環境では`secedit /export`等でレガシー監査ポリシーの実効値を直接確認する
コマンドが管理者権限を要求し、こちらの検証環境(非昇格)からは確認できなかった。そのため
「これが確実な原因である」と断定はできていないが、Microsoft公式に推奨されている前提条件が
未設定だったこと、および症状のパターンが一致することから、最有力の仮説として対応した。

### 対応

`installer/auditlog.cpp`の`SetKernelObjectAuditSubcategory`に、`Kernel Object`サブカテゴリを
有効化する前に`SCENoApplyLegacyAuditPolicy`を`1`に設定する処理(`ForceAdvancedAuditPolicy`)を
追加した。`RunDisableAuditLog`側では(auditpolサブカテゴリ自体の無効化を行わないのと同じ理由で)
元に戻さない — マシン全体設定であることに加え、これはMicrosoft自身が`auditpol`利用時に
常時有効にすることを推奨している設定でもあるため、本機能を無効化した後も有効なままで
問題ないと判断した。Subscription版の同ファイルにも同じ修正を反映済み。実機での最終確認は
利用者側で`--disable-audit-log`→(修正版ビルドで)`--enable-audit-log`を再実行し、再起動後に
イベントログ・トーストが機能するかを見て行う。

**2026-08-10追記**: 上記の修正を反映して実機で`--enable-audit-log`を実行したところ、
何も操作していないのにトースト通知が連続して表示され続ける("unknown process"表示、20件で停止)
という新たな症状が発生した。20という件数は監査対象デバイス数(`\\.\interception00`〜`19`の
ちょうど20個)と一致しており、原因は**`--enable-audit-log`自身が20個のデバイスそれぞれに
`SetNamedSecurityInfoW`でSACLを設定する際、その設定操作自体がたった今設定したばかりの監査条件
(`GENERIC_READ|GENERIC_WRITE`)に一致し、`OpenInputBridgeSetup.exe`自身を対象デバイスへの
アクセス元として毎回記録してしまう**ことだと判明した。再適用タスクは起動のたびに同じ処理を行うため、
放置すると**毎回の起動時に20回のトースト連発が発生し続ける**設計上の欠陥だった。

対応として、`installer/toast-helper/main.cpp`にアクセス元プロセスの解決処理を拡張し、
それが`OpenInputBridgeSetup.exe`自身だった場合はトーストを表示せず黙って終了するフィルターを
追加した(`ProcessInfo::isOwnInstaller`)。監査ログ自体への記録(セキュリティイベントログ)は
除外せず、あくまでトースト通知の表示だけを抑制する — 自己メンテナンス操作は「監査ログの
完全性」の観点では記録して問題ないが、「利用者への通知」の観点では意味のあるイベントではない
ため。Subscription版の同ファイルにも同じ修正を反映済み。

なお、この検証中に「イベントID 4656は0件、4663は80件」という報告もあった。4656(ハンドル要求)と
4663(アクセス実行)は本来近い場面で両方記録されることが多いが、トーストタスクのトリガーは
`(EventID=4656 or EventID=4663)`と両方を監視する設計にしているため、4663のみでも動作上は
問題ない。4656が0件である理由自体は未調査。

**2026-08-10追記(2)**: `--enable-audit-log`を再実行するたびに4663が80件ずつ増える
(20デバイス×4イベント/デバイス)一方、`identify2.exe`(`tests/upstream_lib/`、本家Interception
互換クライアントライブラリを使用)を実行してもイベント数が一切変化しないことが分かった。
これまで観測されていた4663は全て`--enable-audit-log`自身の自己メンテナンス操作によるもので、
**Interceptionプロトコル互換クライアントの実際のアクセスは一度も監査に引っかかっていなかった**
ことが判明した。

原因は`third_party/interception/library/interception.c`を確認して特定した。本家Interception
互換クライアントは各デバイスを`CreateFileA(device_name, GENERIC_READ, 0, NULL, OPEN_EXISTING,
0, NULL)`で**`GENERIC_READ`のみを要求してオープンする**(`GENERIC_WRITE`は要求しない)。一方
`installer/auditlog.cpp`の`BuildAuditSacl`は`AddAuditAccessAceEx`に`GENERIC_READ |
GENERIC_WRITE`という**汎用アクセス権を表す生のビット値をそのままACEに格納**していた。
ACEに格納するアクセスマスクは、あらかじめ対象オブジェクト種別の`GENERIC_MAPPING`で
`FILE_GENERIC_READ`等の**個別アクセス権に変換してから**格納するのが正しい作法であり、
生の汎用ビットのまま格納したACEは、実際のオープン要求(こちらもI/Oマネージャーによって
個別アクセス権に変換されてから照合される)と正しく比較されない場合がある。DACL側
(`driver/common/driver.c`の`OibControlDeviceSddl`)も同じ未変換の`"GRGW"`というSDDLトークンを
使っているが、こちらは実際のアクセス制御(Everyoneがデバイスを開けること)が確認できている
ため、DACLの照合とSACLの照合とで挙動に差があった可能性がある(明確な理由までは未確認)。

### 対応

`BuildAuditSacl`で`MapGenericMask`を使い、`GENERIC_READ | GENERIC_WRITE`を`SE_FILE_OBJECT`
向けの`GENERIC_MAPPING`(`FILE_GENERIC_READ`/`FILE_GENERIC_WRITE`/`FILE_GENERIC_EXECUTE`/
`FILE_ALL_ACCESS`)で個別アクセス権に変換してからACEに格納するように修正した。これは
`ApplySaclToDevice`/`ClearSaclOnDevice`が既に`SE_FILE_OBJECT`として扱っているオブジェクト種別
と一致する変換である。Subscription版の同ファイルにも同じ修正を反映済み。

---

## 2026-08-10: トースト自己ノイズ除外フィルターがPID解決の競合状態で不安定だった問題

### 症状

上記のGENERIC_READマッピング修正後、実機で`--enable-audit-log`/`--disable-audit-log`を
繰り返し実行したところ、**同じコマンドを実行しても、トーストが20個出る場合と0個の場合が
不規則に発生する**ことが分かった(`identify2.exe`実行では常にトースト無し・イベント数変化無し、
という別問題も併発していたため、当初は判別が難しかった)。

### 原因

`installer/toast-helper/main.cpp`の自己ノイズ除外フィルター(`ProcessInfo::isOwnInstaller`)は、
タスクから渡される`--process-id`を使って`OpenProcess`でプロセスイメージパスを**事後的に**
解決し、それが`OpenInputBridgeSetup.exe`かどうかを判定する実装だった。しかし
`--enable-audit-log`/`--disable-audit-log`/`--apply-audit-sacl`自体は20デバイスのループが
1秒未満で完了し、プロセスもすぐ終了する。イベントの発生からタスクスケジューラーが
`OibToastHelper.exe`を実際に起動するまでにはタイムラグがあるため、**トーストヘルパーが
`OpenProcess`を呼ぶ時点で対象プロセスが既に終了していることが多く**、その場合`OpenProcess`が
失敗して`isOwnInstaller`が既定値の`false`のまま(=トースト表示)にフォールバックしていた。
つまりフィルターが機能するかどうかは、ヘルパーの起動がどれだけ速く行われるか という
タイミング(競合状態)に左右されており、再現性が無かった。

### 対応

セキュリティ監査イベント(4656/4663)には、アクセス元プロセスの実行ファイルパスを表す
`ProcessName`フィールドがイベント発生時点の情報として直接含まれている(マニフェストベースの
`Microsoft-Windows-Security-Auditing`プロバイダーの標準フィールド)。事後的に`OpenProcess`で
解決する代わりに、`installer/toastsetup.cpp`の`BuildToastTaskXml`にこのフィールドを取得する
`ValueQueries`エントリ(`processName`)を追加し、`OibToastHelper.exe`に`--process-name`引数
として直接渡すように変更した。これによりプロセスの生死に関係なく確実に解決できる。
`ResolveProcessInfo`は`--process-name`が渡されればそれを最優先で使い、
`--process-id`によるPID解決は(理論上到達しないはずだが)フォールバックとしてのみ残した。
OSS版・Subscription版とも同じ修正を反映し、再ビルド確認済み。

なお、既存のタスクスケジューラー登録は旧い(`ProcessName`を渡さない)定義のまま残るため、
この修正の反映には`--enable-toast`の再実行(`/F`で上書き登録)が必要。

---

## 2026-08-10: GENERIC_READマッピング修正後も`identify2.exe`のアクセスだけ監査に載らない問題

### 症状

前項(GENERIC_READマッピング修正)の反映後も、実機で`--enable-audit-log`実行直後の
自己ノイズ(トースト20連発、4663が80件ずつ増加)は相変わらず確実に再現する一方、
`identify2.exe`(本家Interception互換クライアント)を実行してもイベント数
(4656=0件, 4663=変化なし)は一切変わらないままだった。

### 調査

ユーザーから自己ノイズの実イベントXMLを1件共有してもらい、直接確認した:

```
ObjectType   = File
ObjectName   = \Device\interception19
AccessMask   = 0x1  (AccessList: %%4416 = "ReadData (or ListDirectory)")
ProcessName  = ...\OpenInputBridgeSetup.exe
```

この時点で、まず`identify2.exe`自体が本当にデバイスのオープンに成功しているか
(そもそも監査以前の問題でアクセス自体が失敗していないか)を利用者に確認したところ、
キー入力/マウス操作は正常に検出できており、オープン自体は問題なく成功していることが
分かった。

次に`driver/common/driver.c`を確認し、各コントロールデバイスが`SetNamedSecurityInfoW`と
同じ`SE_FILE_OBJECT`種別で扱われていること(`OibControlDeviceSddl`のDACL、
`WdfControlDeviceInitAllocate`経由の生成)を再確認した。Windowsの詳細監査ポリシーでは、
`SE_FILE_OBJECT`(セキュリティイベントログ上`ObjectType="File"`と記録されるもの全般 — 実ファイル
かどうかを問わず、`CreateFile`系APIで開かれるオブジェクトはすべてこれに該当する)へのアクセス
監査は本来**「ファイルシステム」サブカテゴリ**が管轄しており、**「カーネルオブジェクト」
サブカテゴリではない**(後者はミューテックス/セマフォなど`OpenMutex`/`OpenSemaphore`系で
開かれる真のNTカーネルオブジェクト向け)。今回`auditpol`で有効化していたのは「カーネル
オブジェクト」(GUID `{0CCE921F-...}`)のみで、「ファイルシステム」(GUID
`{0CCE9215-69AE-11D9-BED3-505054503030}`)は一度も有効化していなかった。

自己ノイズ(`SetNamedSecurityInfoW`自身のアクセス)がなぜ「カーネルオブジェクト」有効化だけで
記録されていたのかは完全には特定できていない(SACL書き込み自体に付随する別経路の監査である
可能性がある)。ただし`identify2.exe`側の通常の`CreateFileA(GENERIC_READ)`オープンが
`SE_FILE_OBJECT`として扱われ「ファイルシステム」サブカテゴリの管轄になるという点は
Microsoftのドキュメント上明確であり、これが未有効化のままだったことが最有力の原因と判断した。

### 対応

`installer/auditlog.cpp`に「ファイルシステム」サブカテゴリのGUID定数
(`FileSystemSubcategoryGuid`)を追加し、`RunEnableAuditLog`/`RunDisableAuditLog`で
「カーネルオブジェクト」と「ファイルシステム」の**両方**を有効化/(残置)するように変更した
(`SetKernelObjectAuditSubcategory`を`SetAuditSubcategory`+`SetObjectAccessAuditSubcategories`に
分割)。自己ノイズ用に「カーネルオブジェクト」を無効化する理由はなく、どちらが実際に
効いているか確証が持てない以上、両方有効化するのが安全側の対応と判断した。OSS版・
Subscription版とも同じ修正を反映し、再ビルド確認済み。実機での`identify2.exe`実行時の
イベント発生確認は次回のユーザーテストで検証予定。

### 追記: `FileSystemSubcategoryGuid`のGUID自体が誤っていた

上記対応版を実機で試した利用者から、`--enable-audit-log`実行(「"Kernel Object" and "File
System" audit subcategories」というメッセージも確認済み)後も`identify2.exe`実行でイベント数が
一切変化しないという報告があった。切り分けのため、実際に何のサブカテゴリが有効化されたのかを
`auditpol /get /subcategory:{GUID}`で直接確認してもらったところ、次の重大な誤りが判明した:

```
auditpol /get /subcategory:"{0CCE9215-69AE-11D9-BED3-505054503030}"
  -> カテゴリ「ログオン/ログオフ」、サブカテゴリ「ログオン」(成功および失敗)
```

つまり`FileSystemSubcategoryGuid`に設定していた`{0CCE9215-...}`は「ファイルシステム」ではなく
**「ログオン」**サブカテゴリのGUIDだった(サブカテゴリGUID一覧を記憶から書き起こす際の
思い違いが原因で、複数存在するよく似た連番GUIDのうち隣接する別サブカテゴリのものを
取り違えた)。これにより、この時点までの`--enable-audit-log`実行は実質的に「カーネル
オブジェクト」のみを有効化しており(意図せず「ログオン」の監査も有効化してしまっていたが、
これはログオン成功/失敗という一般的によく有効化される項目であり実害は無い)、本来の狙いだった
「ファイルシステム」サブカテゴリは一度も有効化されていなかった。

正しい「ファイルシステム」サブカテゴリのGUIDは`{0CCE921D-69AE-11D9-BED3-505054503030}`
(auditpolでの実機確認により裏付け済み)。`FileSystemSubcategoryGuid`をこの値に修正し、
コメントに誤り経緯を明記した。OSS版・Subscription版とも修正・再ビルド確認済み。

この件は「実装した対応が理論上正しいはずでも、実機のauditpol出力で実際に何が有効化された
のかを直接確認するまでは信用しない」という、このセッション全体を通じた教訓を改めて裏付ける
事例でもある。

---

## 2026-08-10: `identify2.exe`が監査に載らない根本原因 — 「ハンドル操作」サブカテゴリも必要だった

### 経緯

「ファイルシステム」GUIDの訂正版を実機で試した利用者から、`--enable-audit-log`実行後
(`auditpol /get`で「ファイルシステム」が正しく「成功および失敗」になっていることも確認済み)
も`identify2.exe`実行でイベント数が変化しないという報告が続いた。

新設した`--dump-audit-sacl`診断コマンド(実装時、SACL読み取りにも`SeSecurityPrivilege`が
必要な点を実装し忘れており、これも合わせて修正)で実際にデバイスに設定されているSACLを
読み戻したところ、20台すべてで`mask=0x0012019f`(`FILE_GENERIC_READ | FILE_GENERIC_WRITE`と
完全一致)、`sid=S-1-1-0`(Everyone)、`flags=0xc0`(成功・失敗とも監査)と、意図した通り
正確な内容であることが確認できた。SACL自体・両サブカテゴリの設定とも理論上完璧に揃って
いるにもかかわらず、`identify2.exe`だけが一切引っかからない状態が続いた。

### 切り分け

以下を実機で直接検証し、一つずつ仮説を消していった:

1. **`identify2.exe`のオープン自体は成功しているか**: 利用者に確認したところ、キー入力/
   マウス操作は正常に受信できており、デバイスのオープン自体は問題なく成功している
   (アクセス制御=DACLの問題ではない)。
2. **監査サブシステム自体がこのマシンで機能しているか**(制御対照実験): 通常のNTFSファイル
   に同等のSACL(Everyone, Read/Write, 成功・失敗)をPowerShellの`Set-Acl`で設定し、別プロセス
   (`Get-Content`)から読み取ったところ、**期待通り4663イベントが即座に記録された**。これに
   より、このマシンの監査サブシステム自体は正常であり、問題は当ドライバのデバイスオブジェクト
   固有の何かに絞り込まれた。
3. **`identify2.exe`固有の問題か、それとも当該デバイスへのどんな通常オープンでも起きるか**:
   ここでVS Codeを管理者権限で起動してもらったところ、Claude Code自身のシェル呼び出しもその
   権限を継承していることに気づき、以降は直接検証を行った。管理者権限のPowerShellプロセスから
   `CreateFile(GENERIC_READ)`のP/Invoke呼び出しで`\\.\interception00`を直接開いたところ、
   オープン自体は成功する(`Opened OK`)ものの、**この場合もイベントは一切記録されなかった**。
   これにより「`identify2.exe`固有」「非昇格プロセス固有」という仮説は完全に排除され、
   問題はこのデバイスオブジェクトへの通常のCREATE操作全般に共通する何かだと判明した。

### 原因

自己ノイズ(`SetNamedSecurityInfoW`自身のアクセス)は確実に記録される一方、直接の
`CreateFile(GENERIC_READ)`によるオープンは(管理者権限であっても)一切記録されないという
非対称性から、試しに**「ハンドル操作」サブカテゴリ**(GUID
`{0CCE9223-69AE-11D9-BED3-505054503030}`)を追加で有効化し、同じ`CreateFile(GENERIC_READ)`
直接呼び出しを再実行したところ、**今度こそ`4656`(ハンドル要求)イベントが即座に記録され、
`AccessMask=0x120089`(`FILE_GENERIC_READ`)とSACLの内容が正確に一致した**。

通常のNTFSファイル(上記の制御対照実験)では「ファイルシステム」サブカテゴリ単体で4663が
記録されたのに対し、`WdfControlDeviceInitAllocate`で作成した制御デバイスオブジェクトでは
CREATE時の4656生成に「ハンドル操作」サブカテゴリも必要、という違いがあるらしいことが実機で
裏付けられた。この非対称性の正確なWDF/オブジェクトマネージャー内部の理由までは特定できて
いないが、auditpolでの有効化とプローブ結果による実証は揺るがない。

### 対応

`installer/auditlog.cpp`に`HandleManipulationSubcategoryGuid`定数を追加し、
`SetObjectAccessAuditSubcategories`で「カーネルオブジェクト」「ファイルシステム」
「ハンドル操作」の3つすべてを有効化するように変更した。OSS版・Subscription版とも修正・
再ビルド確認済み。実機での`identify2.exe`実行時のイベント発生確認は次回のユーザーテストで
最終検証予定。

この一連の調査は、実機での地道な切り分け(制御対照実験、プロセス/権限を変えた再現テスト、
診断コマンドによる状態の直接読み戻し)なしには到達できなかった。理論的な推測(「File System
のはず」)が実際には不十分であり、さらに別のサブカテゴリが必要だったという事実は、Windows
の詳細監査ポリシーがオブジェクトの種類(ここでは「通常のNTFSファイル」対「WDFの制御
デバイスオブジェクト」)によって想定以上に細かく依存することを示している。

利用者による実機再検証の結果、`identify2.exe`実行で狙い通りイベントが記録され、
トースト通知も表示されることが確認できた(ただし後述の通り20件表示された)。これにより
本機能は一連の実機デバッグを経て最終的に動作確認まで到達した。

---

## 2026-08-10: トーストの重複表示(1クライアント起動で20件)への対応

### 症状

上記の修正後、`identify2.exe`を実行すると期待通り監査イベントが記録されるようになったが、
**トースト通知が1回のクライアント起動につき20件**表示されることが分かった。

### 原因

`third_party/interception/library/interception.c`の`interception_create_context()`は、
起動時に`\\.\interception00`から`\\.\interception19`まで20台の制御デバイスを**ほぼ同時に
全て**`CreateFileA`でオープンする(本家Interceptionプロトコルの仕様であり、
`identify2.exe`固有ではなく、あらゆる互換クライアントに共通する挙動)。SACLはデバイスごとに
独立して監査するため、この一括オープンは20個の独立したセキュリティイベントとなり、
`installer/toastsetup.cpp`が登録するタスクスケジューラーのタスクもイベントごとに個別へ
`OibToastHelper.exe`を起動する。結果として、1回のクライアント起動が20回のプロセス起動・
20件のトースト表示につながっていた。

### 対応

`installer/toast-helper/main.cpp`に、直近の表示から短時間(3秒)以内に**同じプロセス名**からの
トースト要求が来た場合は表示をスキップする、デバウンス処理を追加した(`ShouldSuppressAsDuplicate`)。
`OibToastHelper.exe`は監査イベント1件につき独立した新規プロセスとして起動されるため、
プロセス自身の変数では状態を持てない。そこで`%LOCALAPPDATA%\OpenInputBridge\
toast_debounce.dat`に「最後に表示した時刻・プロセス名」を書き込み、以降の起動時にこれを
参照する形で状態を永続化した。複数の`OibToastHelper.exe`インスタンスがほぼ同時に起動される
ため、名前付きミューテックス(`Local\OpenInputBridge.ToastDebounce`)でこのファイルへの
アクセスを直列化している。多少の競合(ミューテックス待機のタイムアウトなど)が残っても、
最悪トーストが1件余分に出るだけであり、通知機能としては許容範囲と判断した。

デバウンスの単位はプロセス名ベース(グローバルな時間ウィンドウではなく)とした。これは、
ほぼ同時刻に**別の**クライアントが起動した場合(たとえば正規クライアントの直後に不審な
プロセスが動いた場合)にまで通知を握りつぶしてしまうと、本機能の目的(異常なアクセスの
可視化)を損なうと判断したため。監査ログ自体(セキュリティイベントログ)は一切間引かず、
20件そのまま記録され続ける — 抑制するのはあくまでトースト表示のみ。

OSS版・Subscription版とも同じ修正を反映し、再ビルド確認済み。

---

## 2026-08-10: トースト表示のホワイトリスト機能を追加

### 背景

利用者から、信頼している既知のInterception互換クライアントについては毎回のトースト通知を
抑制したい(アラート疲れの防止)という要望があり、ホワイトリスト機能の追加を決定した。
実装前に以下の3点を利用者と確認した:

1. **抑制対象**: トースト表示のみ。監査ログ(セキュリティイベントログ)への記録は
   ホワイトリスト登録の有無に関わらず全件継続する(`isOwnInstaller`と同じ方針 — 抑制するのは
   あくまで「通知」であり、「記録」ではない)。監査ログ自体まで間引く案(SACLを動的に変える等)
   は実装が複雑になり、監査の完全性という目的とも相反するため不採用。
2. **保存場所**: レジストリ(`HKLM\SOFTWARE\OpenInputBridge`)。AUMID登録
   (`HKLM\SOFTWARE\Classes\AppUserModelId\...`)と同様、マシン全体に効く設定はレジストリに
   保存し、CLI(`--allow-process`/`--disallow-process`)で管理する方針とした。
3. **照合粒度**: フルパス一致(ファイル名のみの一致ではない)。`isOwnInstaller`はファイル名
   のみの照合で十分(固定の既知ファイル名1つだけを見分ければよいため)だが、ホワイトリストは
   利用者が任意の第三者ソフトウェアを登録するものであり、ファイル名のみの照合だと同名の別
   ソフトウェア(別フォルダにある無関係・場合によっては悪意のあるプロセス)まで巻き込んで
   抑制してしまう。ただし、これはあくまで「通知を抑制するかどうか」の判定であり、デバイスへの
   アクセス制御(DACL)や監査記録そのものには一切影響しないため、セキュリティ境界としての
   厳密さを持つものではない。

### 実装

- `installer/toastsetup.h`/`.cpp`: `RunAllowProcess`/`RunDisallowProcess`/`RunListAllowedProcesses`
  を追加。ホワイトリストは`HKLM\SOFTWARE\OpenInputBridge`キーの`ToastAllowedProcessPaths`値
  (`REG_MULTI_SZ`、フルパスのリスト)に保存する。
- `installer/main.cpp`: `--allow-process <フルパス>` / `--disallow-process <フルパス>` /
  `--list-allowed-processes` のCLIサブコマンドを追加。前者2つは`argc==3`の専用ハンドリングを
  新設(既存の`argc==2`単体コマンド群とは別枠)。
- `installer/toast-helper/main.cpp`: `IsProcessAllowlisted`を追加し、`ResolveProcessInfo`が
  返す`ProcessInfo`に新たに`fullPath`フィールド(イベントの`ProcessName`、またはPIDフォール
  バック時は`QueryFullProcessImageNameW`の結果)を持たせて、これと照合する。判定順序は
  `isOwnInstaller`確認 → ホワイトリスト確認 → デバウンス確認 → トースト表示、とした
  (ホワイトリスト対象ならデバウンスの状態更新自体も行わない)。
- レジストリキーパス・値名の文字列定数は、AUMID文字列(`ToastAppUserModelId`)と同様の理由
  (`toastsetup.cpp`と`toast-helper/main.cpp`は別プロジェクトとしてビルドされ、ヘッダーを
  共有できない)により、両ファイルにコメント付きで重複定義している。

OSS版・Subscription版とも同じ修正を反映し、両方の実行ファイル(`OpenInputBridgeSetup.exe`、
`OibToastHelper.exe`)を再ビルド確認済み。実機での動作確認は次回のユーザーテストで実施予定。

---

## 2026-08-11: OS/アーキテクチャ事前チェックとインストール後の自己診断(`--verify-install`)を追加

### 背景

`setup.bat`によるワンクリックインストールを安全にするため、以下2点を追加した。

1. **対応環境の事前チェック**: x64かつWindows 10 バージョン1903(May 2019 Update、ビルド18362)
   以降でなければインストール作業に進まないようにする。対応外環境でカーネルドライバの
   インストールを試みることは、BSODやサイレントな入力破損につながりかねないため、
   `setup.bat`側(PowerShellでの事前チェック、UAC昇格より前に実施し無駄な昇格プロンプトを
   避ける)と`OpenInputBridgeSetup.exe`自身の両方でチェックする(`common.cpp`の
   `IsSupportedWindowsEnvironment`)。表向きの対応OSはWindows 11以上のため、エラー
   メッセージは技術的な下限(1903)ではなく「This is the wrong Windows version. It's for
   Windows 11.」とした。アーキテクチャは`GetNativeSystemInfo`で確認する(自プロセスが
   ARM64上でx64エミュレーションされている可能性があるため、`GetSystemInfo`ではなく
   ネイティブアーキテクチャを返すこちらを使用)。
   Pro/Subscription版は自前のインストーラーにこのチェックを別途組み込む予定のため、
   `OpenInputBridgeSetup.exe`をそちらのインストーラーから呼び出す際に干渉する場合に備え、
   `--skip-version-check`で無効化できるようにした。

2. **インストール後の自己診断(`--verify-install`、新設`installer/verify.cpp`)**:
   - **フィルタ登録の整合性確認**: `UpperFilters`にドライバがフィルタとして登録されている
     のに、そのサービスの`ImagePath`が指す実体ファイルが存在しない場合(インストールが
     途中で失敗した、等)、**そのまま再起動するとキーボード/マウスが完全に使用不能になる**
     という重大な事故につながる。これを検知した場合、フィルタ登録自体を削除し、
     「インストールが正しく完了していないためフィルタ登録を削除した」旨のエラーメッセージを
     表示する。`ImagePath`の実際の値は実機で確認済み
     (`\SystemRoot\System32\DriverStore\FileRepository\oib_kbd.inf_.../oib_kbd.sys`、
     `REG_EXPAND_SZ`だが`%変数%`形式ではなくリテラルな`\SystemRoot\`プレフィックス)で、
     これを踏まえて`ResolveServiceImagePath`で解決している。
   - **監査ログ・トースト通知の有効化リマインダー**: 監査ログ・トースト通知機能を
     ユーザーが選択しなかった場合でも、`--verify-install`実行時に「このデバイスドライバは
     管理者権限の無いプロセスからでも誰でもアクセスできるため、ログ記録やトースト通知の
     有効化を検討してください」という内容の英文メッセージを表示する(有効化自体は強制
     しない、あくまで気づきを促すリマインダーとして)。有効化判定は、監査ログ再適用タスク
     (`AuditLogReapplyTaskName`)・トースト通知タスク(`ToastNotifyTaskName`)がタスク
     スケジューラーに登録されているかどうかで判断する(そのため両定数を`auditlog.h`/
     `toastsetup.h`でpublicに公開し直した)。

`setup.bat`は、OS/アーキテクチャチェック → 昇格 → ドライバインストール →
`--enable-audit-log` → `--enable-toast` → `--verify-install`、の順に実行するよう更新した。

### 実装メモ

- `RunSystem32Tool`に`suppressOutput`引数を追加(NULデバイスへのリダイレクト)。
  `schtasks /Query`でタスクの存在有無を確認する`ScheduledTaskExists`は、タスクが無い
  (=機能無効、正常な状態)場合でも`schtasks`自身が「見つかりません」的なメッセージを標準
  出力に出すため、これを抑制する目的で追加した。
- `common.cpp`に`IsRegisteredAsUpperFilter`(クラスレベルの`UpperFilters`に対象が含まれるか
  確認するだけの読み取り専用関数)を追加。`ModifyUpperFilters`の内部ヘルパー
  (`ReadMultiSz`等)を流用している。
- `main.cpp`は`--skip-version-check`をargv走査の最初の段階で取り除き、残りの引数だけで
  従来通りの分岐処理(`argc==2`相当の単体コマンド群、`argc==3`相当の`--allow-process`等、
  インストール/アンインストール引数のループ)を行うよう作り直した
  (`std::vector<std::wstring>`ベースに変更)。

OSS版・Subscription版とも同じ修正を反映し、再ビルド確認済み。Pro/Subscription版の
インストーラー側(WiX)への`--verify-install`/`--skip-version-check`の組み込みは、今回は
対象外(各インストーラー側で別途検討予定)。実機での動作確認(管理者権限が必要なため)は
次回のユーザーテストで実施予定。

---

## 2026-08-11: `setup.bat`が改行コード(LF)のせいでcmd.exeに正しく解釈されなかった問題

### 症状

利用者が`setup.bat`を実行したところ、PowerShell・コマンドプロンプトのどちらからでも、
`rem`コメント行の内容(`Copyright`等)がそのままコマンドとして実行されようとしたり、
`OpenInputBridgeSetup.exe --enable-audit-log`の`--enable-audit-log`部分だけが独立した
未知のコマンドとして扱われたりするなど、構文エラーが多発した。

### 原因

`setup.bat`のファイル本体が、改行コードLF(`\n`)のみで保存されていた(CRLF`\r\n`ではなく)。
cmd.exeのバッチパーサーは、単純な単一行コマンドであればLFのみでもおおむね動作するが、
このファイルにある`if not "..." ( ... )`のような複数行にまたがる括弧ブロックの解析は
CRLFを前提としており、LFのみの場合に解析が崩れることが実機で確認された。同じ問題が
`driver/build_codeql.bat`にも(単純な単一行コマンドのみのため表面化していなかったが)
存在していた。

さらに調査したところ、このリポジトリには`.gitattributes`が存在せず、利用者の
`git config core.autocrlf`は`true`だった。これは「git内部の保存形式はLF、チェックアウト時に
そのマシンの設定でCRLFへ変換する」という挙動のため、**このマシンではコミット後も
正しくCRLFへ復元されるが、`core.autocrlf`が`false`または未設定の環境(Linux/Mac、あるいは
明示的に無効化しているWindows環境)でこのリポジトリをcloneすると、保存されているLFが
そのままチェックアウトされ、今回と同じバグが再発する**ことが分かった。

### 対応

1. `packaging/setup.bat`・`driver/build_codeql.bat`の実体をCRLFに変換した。
2. リポジトリ直下に`.gitattributes`を新設し、`*.bat text eol=crlf`を追加。これにより
   `.bat`ファイルは、チェックアウトする側の`core.autocrlf`設定に関係なく常にCRLFで
   チェックアウトされるようになる(gitが比較・保存に使う正規化後の内容は変わらないため、
   他の`.cpp`/`.h`/`.md`ファイルの扱いには影響しない)。

---

## 2026-08-11: トーストをクリックすると該当プロセスの実体をエクスプローラーで選択表示する機能を追加

### 背景

利用者から、トースト通知をクリックした際に、検知したプロセス(バイナリ)の実体があるフォルダを
開き、可能であればそのファイルを選択状態にしたい、という要望があった。

### 検討した実現方式

トースト本体のクリック(WinRTでは「アクティブ化」と呼ぶ)をハンドルするには、大きく分けて
以下2通りの方式がある。

1. **COMベースのトースト アクティベーション**(`INotificationActivationCallback`実装 +
   `ToastActivatorCLSID`登録): Microsoftの標準的な非パッケージ化デスクトップアプリ向け
   実装方式だが、COMローカルサーバー(`IClassFactory`/`INotificationActivationCallback`の
   実装、CLSID登録、`-Embedding`起動への対応等)を新規に実装する必要があり、実装コストが高い。
2. **プロトコルアクティベーション**(`<toast launch="..." activationType="protocol">`):
   トーストに独自スキームのURI(例: `oib-reveal:...`)を`launch`属性として持たせ、そのスキームを
   通常のURIプロトコルハンドラー(`HKEY_CLASSES_ROOT`配下の`shell\open\command`)として
   レジストリに登録するだけで、クリック時にWindowsが登録済みコマンドを起動してくれる。
   常駐リスナーもCOMサーバーも不要で、既存の`OibToastHelper.exe`(1回起動して終了する
   使い捨てプロセス)というアーキテクチャとも自然に整合する。

実装コストと、既存アーキテクチャ(常駐プロセスを持たない設計)との親和性から、2の
プロトコルアクティベーション方式を採用した。

### 実装

- **`installer/toastsetup.cpp`**: `RegisterRevealProtocol`/`UnregisterRevealProtocol`を追加。
  `HKLM\SOFTWARE\Classes\oib-reveal`に、`URL Protocol`値と
  `shell\open\command`(`"<OibToastHelper.exeのパス>" --reveal "%1"`)を登録する。
  `RunEnableToast`/`RunDisableToast`から、既存のAUMID登録/解除と同様のタイミングで
  呼び出す(AUMID登録直後に登録、Scheduled Task/Start Menuショートカット解除と同様の
  タイミングで解除)。
- **`installer/toast-helper/main.cpp`**:
  - `ShowToast`に`processFullPath`引数を追加。値がある場合、`<toast>`要素に
    `launch="oib-reveal:<percent-encodeしたフルパス>"`と`activationType="protocol"`属性を
    付与する(値が空の場合は従来通りクリック不可の通常トーストのまま — PIDフォールバック
    経路で`fullPath`が解決できなかった場合に相当)。
  - `UriEncode`/`UriDecode`を追加。パスをいったんUTF-8へ変換してからバイト単位で
    percent-encodeする方式とし、日本語ユーザー名を含むパス等、非ASCII文字を含むパスも
    正しく往復できるようにした。
  - `wmain`の先頭に、`--reveal <uri>`呼び出し(Windowsがプロトコルハンドラー経由で
    起動する際の引数)を検出する分岐を追加。既存のトースト表示ロジック(`--object-name`等)
    とは完全に独立したコードパスとして扱う。`oib-reveal:`プレフィックスを取り除いて
    percent-decodeした後、`RevealInExplorer`で`explorer.exe /select,"<path>"`を起動する。

登録スコープはAUMIDと同様HKLM(マシン全体)とした。クリックした際にどのユーザーの
セッションで処理されても機能するようにするため。

OSS版・Subscription版とも同じ修正を反映し、両方の実行ファイル(`OpenInputBridgeSetup.exe`、
`OibToastHelper.exe`)を再ビルド確認済み。実機での動作確認(トーストクリック→
エクスプローラーでの選択表示)は次回のユーザーテストで実施予定。

---

## 2026-09-01: OS事前チェックの下限をWindows 10 1903からWindows 11(ビルド22000)に引き上げ

### 経緯

[Issue #4](https://github.com/Applet-LLC/OpenInputBridge/issues/4)にて、Windows 10 x64 22H2
(ビルド19045)環境に本ドライバをインストールしたところ、再起動後にキーボード・マウスとも
入力不可になるという報告があった。

`OpenInputBridgeSetup.exe`(`common.cpp`の`IsSupportedWindowsEnvironment`)・`setup.bat`とも、
2026-08-11の事前チェック導入時点では、技術的な下限としてビルド18362(Windows 10 1903、
"May 2019 Update")を採用しており、Windows 10 22H2(ビルド19045)はこれを満たすため
インストールを許可していた。表向きの対応OSは当初からWindows 11以上とアナウンスしており
(エラーメッセージも"It's for Windows 11."のまま)、Windows 10は実機での動作確認・
サポート対象外だったが、コード側のチェックがそれよりも緩く、Windows 10でもインストールが
通ってしまう不整合があった。

### 対応

- `installer/common.cpp`の`kMinimumSupportedBuildNumber`を18362から22000
  (Windows 11の最初のリリースビルド)に変更。
- `installer/common.h`・`installer/main.cpp`のコメントを、Windows 10 1903+ではなく
  Windows 11+が実際のチェック対象である旨に修正。
- `packaging/setup.bat`側の事前チェック(PowerShellでのビルド番号比較)も同じく
  18362から22000に変更。`OpenInputBridgeSetup.exe`本体のチェックとsetup.bat側の
  チェックは常に同じ下限を指す必要がある(2026-08-11の項参照)。
- `README.md`のインストール手順説明を「Windows 10 バージョン1903（May 2019 Update）以降」
  から「Windows 11以降」に修正し、Windows 10が(22H2のような最新パッチレベルであっても)
  非対応であることを明記。

`packaging/dist-readme/README.en-US.txt`・`README.ja-JP.txt`は、配布物作成時点から
既に「Windows 11 24H2 or later」と記載しておりコード側との不整合はなかった
(実際のチェックがそれより緩かっただけ)。エラーメッセージ自体
("This is the wrong Windows version. It's for Windows 11.")も変更していない
— 表向きの対応OSの説明は当初から一貫してWindows 11であり、今回はコード側の
チェックをその説明に合わせて厳格化したという位置づけ。

---

## 2026-09-01: ARM64対応（[Issue #2](https://github.com/Applet-LLC/OpenInputBridge/issues/2)）

### 経緯

`oib_kbd.sys`/`oib_mou.sys`をARM64ネイティブでもビルド・配布できるようにしたい、という
要望。現状は`OpenInputBridge.sln`のすべてのプロジェクトがx64のみで構成されている
（`driver/keyboard/oib_kbd.vcxproj`・`driver/mouse/oib_mou.vcxproj`のヘッダコメントには
「ARM64は将来追加できる」という趣旨の記述が以前から存在していた）。

要件として、(1) ソリューションの1回のビルドでx64・ARM64両方のドライバが作れること
（ARM64ビルドのためだけにソリューションのプラットフォームを手動切り替えする必要が
ないこと）、(2) ARM64版のプロジェクトは既存x64プロジェクトのソースコードを（コピーではなく）
参照する形で新規追加し、Configuration Manager上でARM64を選択する形にすること、
(3) infファイルはアーキテクチャ間で共通のままでよいこと、が指定された。

### 対応

- **ドライバプロジェクト**: `driver/keyboard/oib_kbd_arm64.vcxproj`・
  `driver/mouse/oib_mou_arm64.vcxproj`を新規追加。既存の`oib_kbd.vcxproj`/`oib_mou.vcxproj`と
  同じディレクトリに置き、`ClCompile`/`ClInclude`/`Inf`の各項目は同じ相対パス
  （`../common/*.c/.h`、`kbdfilter.c`/`mousefilter.c`、`oib_kbd.inx`/`oib_mou.inx`）で
  既存ソースを参照する（コピーしない）。`ProjectConfigurations`は`Debug|ARM64`・
  `Release|ARM64`のみを持つ。
- **ソリューション配線**: `OpenInputBridge.sln`に新しいSolution Platform（`ARM64`）は
  追加せず、既存の`Debug|x64`/`Release|x64`/`ReleaseWHQL|x64`という3つのソリューション構成
  すべてについて、`oib_kbd_arm64`/`oib_mou_arm64`の`ActiveCfg`/`Build.0`を各々の
  `Debug|ARM64`/`Release|ARM64`プロジェクト構成にマッピングした。これにより、
  Configuration Manager上ではこの2プロジェクトのPlatform列が常にARM64に固定された状態で
  見え、既存のビルドコマンド（`msbuild OpenInputBridge.sln /p:Platform=x64 ...`）を
  そのまま実行するだけで両アーキテクチャが同時にビルドされる。`msbuild
  OpenInputBridge.sln /p:Platform=x64 /t:oib_kbd_arm64`で実際にこの対応関係を確認し、
  意図通りARM64としてビルドされること（このマシンにはARM64向けSpectre軽減ライブラリと
  完全なWDKが入っていないため、コンパイル自体はMSB8040/`ntddk.h`不足でx64側と同様に
  失敗するが、その手前までは正しく到達する）を確認済み。
  `Packaging`プロジェクトの`ProjectDependencies`にも両プロジェクトを追加した。
- **INFファイル**: `oib_kbd.inx`/`oib_mou.inx`は共有のまま、既存の`NTamd64`/
  `NTamd64.Services`セクションと対になる`NTarm64`/`NTarm64.Services`セクションを追加した
  （中身の`CopyFiles`/`ServiceInstall`セクションはアーキテクチャ非依存のためそのまま
  共有）。x64ビルド・ARM64ビルドそれぞれのInf2Catステップが同じINF本文を自分の
  パッケージフォルダにスタンプするだけなので、使わない方のデコレーションセクションは
  単に無視される。
- **パッケージング（`packaging/sign.mak`）**: x64/ARM64それぞれの`DRIVER_PACKAGE_DIR_*`/
  `TARGET_*`変数を追加し、`sign-driver`/`sign-all`/`verify`/`stage-driver`/`stage-symbol`の
  各ターゲットが両アーキテクチャを無条件に扱うようにした。配布ZIPのレイアウトを
  `oib_kbd\oib_kbd.inf`のようなフラット構成から`oib_kbd\x64\oib_kbd.inf`・
  `oib_kbd\arm64\oib_kbd.inf`という`<arch>`サブフォルダ付きの構成に変更（x64/ARM64を
  1つのZIPに統合して同梱するため）。`nmake -f sign.mak -n stage`で生成コマンド列が
  意図通りであることを確認済み。`whql`ターゲットは、WHQL認定がアーキテクチャごとに
  個別のHLK申請になる（x64用とARM64用で別々に申請・別々に返ってくる）ことを踏まえ、
  `Signed\oib_kbd\x64\`・`Signed\oib_kbd\arm64\`など4つのサブフォルダへの手動配置を
  案内するよう更新した。
- **インストーラ**: `installer/common.h`/`common.cpp`に`IsNativeArm64()`を追加
  （`GetNativeSystemInfo`ベースの既存の`IsSupportedWindowsEnvironment()`と同じ
  ネイティブアーキテクチャ判定を共有）。`IsSupportedWindowsEnvironment()`の許可
  アーキテクチャを`PROCESSOR_ARCHITECTURE_AMD64`のみから、`PROCESSOR_ARCHITECTURE_ARM64`も
  含む形に拡張。`install.cpp`のパッケージパス解決（`infPath`/`catPath`）とサービス
  セクション名選択（`DefaultInstall.NTamd64.Services`/`DefaultInstall.NTarm64.Services`）、
  および`uninstall.cpp`のパッケージパス解決を、`IsNativeArm64()`に基づく`x64`/`arm64`
  サブフォルダ・セクション名選択に変更した。`installer\OpenInputBridgeSetup.vcxproj`を
  ビルドし直し、変更がクリーンにコンパイルされることを確認済み。
- **インストーラ本体・トーストヘルパーはx64のみ**とし、ARM64ネイティブ化はしない。
  どちらもユーザーモードアプリであり、ARM64 Windows上ではx64エミュレーションで問題なく
  動作するため（`IsSupportedWindowsEnvironment()`が元々`GetSystemInfo`ではなく
  `GetNativeSystemInfo`を使っていたのも、まさにこの「インストーラ自身がエミュレーションで
  動いていても実機のアーキテクチャを見る」ことを意図した設計だった）。ネイティブ
  ビルドが必須なのはカーネルドライバ（.sys）のみ。
- **配布物は統合ZIP**とし、x64/ARM64別々のリリースパッケージには分けない。
  インストール時に`IsNativeArm64()`でホストのネイティブアーキテクチャを判定し、
  該当する方のドライバパッケージを自動的に選択する。
- `README.md`のアーキテクチャ概要・`setup.bat`のインストール条件説明・ビルド方法の
  各節をARM64対応に合わせて更新。

### 追記: パッケージ化フォルダ名の不一致修正

実機ビルドで、コンパイル・リンク自体は成功して`.sys`は生成されるものの、`packaging\sign.mak`が
参照する`driver\keyboard\ARM64\Release\oib_kbd\oib_kbd.sys`が見つからずコピー/署名に失敗する
不具合が発覚。原因はWDKの`WindowsDriver.Common.targets`（`PackageDir`プロパティの既定値）が

```
<PackageDir Condition="'$(PackageDir)' == ''">$(OutDir)$(ProjectName)\</PackageDir>
```

となっており、パッケージ化先フォルダ名が`$(TargetName)`(`oib_kbd`)ではなく`$(ProjectName)`
（vcxprojファイル自身の名前、新規追加した`oib_kbd_arm64`/`oib_mou_arm64`ではその名前）に
なっていたため。既存の`oib_kbd.vcxproj`/`oib_mou.vcxproj`はプロジェクト名とTargetNameが
たまたま一致していた（ファイル名がそのまま`oib_kbd`/`oib_mou`）ため、この問題が表面化して
いなかった。`oib_kbd_arm64.vcxproj`/`oib_mou_arm64.vcxproj`の`OutDir`と同じ
`PropertyGroup`に`<PackageDir>$(OutDir)oib_kbd\</PackageDir>`（マウス側は`oib_mou\`）を
明示的に追加し、パッケージ化先フォルダ名をTargetNameベースの名前に固定した。

### 追記: ARM64実機での`SetupInstallServicesFromInfSectionW`失敗（`ERROR_IN_WOW64`） — ネイティブARM64インストーラが必須と判明

#### 症状

上記のPackageDir修正後、ARM64 Windows 11実機に`setup.bat`でインストールしたところ、
キーボード用ドライバは`DiInstallDriverW`でDriver Storeへの格納までは成功するものの、
その直後の`SetupInstallServicesFromInfSectionW`（サービス登録）が失敗し、
`OpenInputBridgeSetup.exe`が終了コード1で終了。マウス用ドライバは（`main.cpp`が
キーボードの失敗で処理全体を打ち切るため）そもそも試行されなかった。Intel版（x64）
Windows 11実機では同じ症状は再現しない。

#### 原因

`install.cpp`が`wprintf(L"... %lu\n", GetLastError())`で出力していたエラーコード
`3758096949`（10進）は16進で`0xE0000235`。Windows SDK付属の`setupapi.h`
（`Include\<version>\um\setupapi.h`）で

```c
#define ERROR_IN_WOW64  (APPLICATION_ERROR_MASK|ERROR_SEVERITY_ERROR|0x235)
```

と定義されている値と完全一致する。すなわち「WOW64プロセスからの呼び出しであるため、
この操作は許可されていません」。ARM64 Windows上でx64エミュレーションにより動作している
プロセスは、Microsoftの実装上この種のSetupAPI呼び出しに関してWOW64プロセス相当として
扱われる。`DiInstallDriverW`はこの制約に該当しない（実機で確認済み: ステージングまでは
成功する）が、`SetupInstallServicesFromInfSectionW`は該当するため、ここで失敗する。

これは、インストーラ（`OpenInputBridgeSetup.exe`）はドライバと異なりユーザーモードアプリ
なのでx64のままエミュレーションで動かせばよい、という当初の判断（[Issue #2](https://github.com/Applet-LLC/OpenInputBridge/issues/2)対応時の判断）が、ドライバの
インストール処理そのものに関しては成立しないことを意味する。監査ログ・トースト通知・
UpperFilters登録など、SetupAPIのドライバインストール系エントリポイントを使わない他の処理は、
実機でも問題なくx64エミュレーションで動作している（`setup.bat`のログ上、
`--enable-audit-log`・`--enable-toast`は正常終了している）。

#### 対応

- **`installer/OpenInputBridgeSetup_arm64.vcxproj`を新規追加**。ドライバのARM64対応
  （`oib_kbd_arm64.vcxproj`等）と同じパターンで、`installer/`内の既存ソース
  （`main.cpp`/`install.cpp`/`uninstall.cpp`/`common.cpp`/`auditlog.cpp`/
  `toastsetup.cpp`/`verify.cpp`）を相対パスで参照する別プロジェクトとし、
  `ProjectConfigurations`は`Debug|ARM64`/`Release|ARM64`のみ。`TargetName`は
  `OpenInputBridgeSetup-arm64`とし、既存の`OpenInputBridgeSetup.exe`（x64）とは
  別名にした（ドライバパッケージと異なりインストーラexeは配布zip直下にフラットに
  置かれており、アーキテクチャ別サブフォルダを持たないため）。`OpenInputBridge.sln`にも
  同じパターンでConfiguration Managerのマッピングを追加し、既存の3ソリューション構成
  すべてでARM64に固定。`msbuild ... /p:Platform=ARM64`でビルドし、生成された
  `OpenInputBridgeSetup-arm64.exe`のPEヘッダのMachine値が`0xAA64`（IMAGE_FILE_MACHINE_ARM64）
  であることを確認済み。
  - なお、このマシンで素の`msbuild`から直接ビルドした際、複数バージョンのMSVC
    ツールセットが混在しており、既定で選択されるバージョンにARM64向けクロス
    コンパイラ（`Hostx64\arm64\cl.exe`）が含まれていなかったため、
    `/p:VCToolsVersion=<ARM64ツール入りバージョン>`を明示指定してビルドを確認する
    一幕があった（`LNK1112`: "モジュールのコンピューターの種類 'x86' は対象
    コンピューターの種類 'ARM64' と競合しています"）。この点は、ユーザーの実際の
    ビルド手順（`_VS2022WDK.cmd`でEWDK環境をセットアップしてから`devenv`を起動）が
    `VCToolsVersion=14.44.35207`（ARM64クロスコンパイラを含むバージョン）を
    明示的に固定していることを確認しており、問題にならないことを確認済み。
- **`packaging/sign.mak`**: `TARGET_BIN_ARM64`（`..\installer\ARM64\Release\OpenInputBridgeSetup-arm64.exe`）を追加し、`sign-bin`/`sign-all`/`verify`/`stage-bin`の各ターゲットで
  `TARGET_BIN`（x64）と並行して扱うようにした。配布zip直下に`OpenInputBridgeSetup.exe`と
  `OpenInputBridgeSetup-arm64.exe`がフラットに同居する。
- **`packaging/setup.bat`**: `[System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture`
  （このプロセス自身のアーキテクチャではなく実機のOSアーキテクチャを返す）でホストが
  ARM64かどうかを判定し、`OIB_EXE`変数に`OpenInputBridgeSetup.exe`または
  `OpenInputBridgeSetup-arm64.exe`を設定して、以降のすべての呼び出しをこの変数経由に
  変更した。
- **`OpenInputBridge.sln`**: `Packaging`プロジェクトの`ProjectDependencies`に
  `OpenInputBridgeSetup_arm64`を追加。
- `README.md`のアーキテクチャ概要・インストール・ビルド方法の各節を、インストーラも
  ARM64ネイティブビルドが必須である旨に合わせて更新（トーストヘルパー
  `OibToastHelper.exe`はSetupAPIのドライバインストールAPIを呼ばないためx64のままで
  問題ない）。
- `installer/toastsetup.cpp`・`installer/auditlog.cpp`・`installer/verify.cpp`は
  変更していない。これらが使うAPI（レジストリ・タスクスケジューラ・トースト通知の
  各Win32/COM API）はSetupAPIのドライバインストール系エントリポイントではなく、
  ARM64エミュレーション下でも実機で正常動作を確認済み。

### 追記: `isOwnInstaller`自己ノイズ判定がARM64版インストーラ名を認識していなかった

`installer/toast-helper/main.cpp`の`ResolveProcessInfo`内、`OpenInputBridgeSetup.exe`自身の
`--apply-audit-sacl`（起動時の毎回のSACL再適用）を「自分自身のルーチン処理」として
トースト表示を抑制する`isOwnInstaller`判定が、`OpenInputBridgeSetup.exe`という固定文字列
比較のみだったため、ARM64版（`OpenInputBridgeSetup-arm64.exe`）からの同じ処理を自分自身と
認識できていなかった。結果として、ARM64ホストでは起動のたびに最大20個（スロット数分）の
無意味なトーストが表示される回帰が起きるところだった。`OpenInputBridgeSetup-arm64.exe`も
比較対象に追加して修正。

---

## 2026-09-01: OneDrive同期フォルダへのインストールで監査ログ・トースト通知が機能しない

### 症状

ARM64実機で、`C:\Users\<user>\OneDrive\Desktop\OpenInputBridge`（OneDriveで同期される
デスクトップフォルダ配下）に展開してインストールしたところ、ドライバ自体は正常動作する
ものの、監査ログ・トースト通知機能が全く動作しなかった。切り分けの結果:

- `OpenInputBridgeSetup-arm64.exe --dump-audit-sacl`で全`\\.\interceptionNN`のSACLが
  0 ACE（一度も適用されていない）
- `OpenInputBridgeSetup-arm64.exe --apply-audit-sacl`を**手動**で実行すると正常にSACLが
  適用され、以降トーストも正常に表示される
- タスクスケジューラの`OpenInputBridgeAuditLogReapply`タスク（起動時にSYSTEM権限で
  `--apply-audit-sacl`を自動実行する、`installer/auditlog.cpp`の`BuildReapplyTaskXml`が
  登録するBootTriggerタスク）の実行履歴が存在しない — すなわち、このタスクが
  自動的には一度も実行されていない

インストール先をOneDrive同期対象外のローカルフォルダ（例: `C:\OpenInputBridge`）に
変更してアンインストール→再インストール→再起動したところ、`OpenInputBridgeAuditLogReapply`
が正常に起動時実行され、`identify3.exe`を起動しただけでトーストが即座に表示されることを
確認した。

### 原因（推定）

`OpenInputBridgeAuditLogReapply`はSYSTEM権限・BootTrigger（起動時、ユーザーのサインイン
セッションが確立する前）で、`GetInstallerExecutablePath()`が記録したインストーラexeの
フルパスを直接実行する。OneDriveのFiles On-Demand機能により、インストーラexeがクラウド
専用のプレースホルダーファイルとして扱われている場合、そのファイルの実体（バイト列）を
ハイドレートするにはOneDrive同期エンジン（ユーザーのサインインセッション内で動作し、
そのユーザーの認証情報を必要とする）が必要となるため、ユーザーセッションが存在しない
起動直後のSYSTEMコンテキストではファイルの実体にアクセスできず、タスクの実行自体が
（履歴にすら残らない形で）失敗していると考えられる。この現象自体はARM64固有ではなく、
x64環境でも同じ条件（OneDrive同期フォルダへのインストール）であれば起こり得ると考えられる
（今回たまたまARM64の実機検証中に見つかったのみで、この不具合そのものはアーキテクチャに
依存しないインストール先の問題）。

### 対応

- `README.md`のトースト通知・監査ログ節に、OneDriveなどクラウド同期フォルダの配下には
  展開しないよう明記（既存の「展開先を移動・削除しないでください」という注意と合わせて）。
- `packaging/dist-readme/README.ja-JP.txt`・`README.en-US.txt`（配布zipに同梱される
  エンドユーザー向けインストールガイド）のインストール手順にも同様の注意書きを追加。
- インストーラ側での自動検知（インストール先がOneDrive等のクラウド同期フォルダ配下かを
  判定し警告する）は今回実装していない — 現時点ではドキュメントでの注意喚起のみ。

---

## 2026-09-01: サードパーティ製`interception-driver-fix`の調査 → コード変更なし、DACL案への裏付け記録のみ

ユーザーから、本家Interceptionドライバの不具合（ホットプラグ/スリープ復帰時のデバイスフリーズ）を
回避するサードパーティ製ツール[`interception-driver-fix`](https://github.com/hygorostrowskij/interception-driver-fix)
を教えられ、OIBへのフィードバックの要否を検討した。

調査の結果、同ツールが対処している根本原因（固定範囲のシンボリックリンク/内部スロットが
PnP再列挙で枯渇・不整合を起こす）は、OIBの`driver/common/slots.c`が採用する「固定20スロットへの
動的割当て+解放時の再利用+スロット枯渇時のグレースフルデグラデーション」設計では構造的に
発生しないと判断し、symlink foldingのようなワークアラウンドの移植は不要と結論した。

一方、同ツールのオプトインDACLロックダウン機能は、`docs/SECURITY_CONSIDERATIONS.md`で以前から
未着手としていた「DACLの絞り込み」案と同一提案であり、実需の裏付けとして参照を追記した。

調査の詳細と判断根拠は[`docs/THIRD_PARTY_INTERCEPTION_DRIVER_FIX.md`](THIRD_PARTY_INTERCEPTION_DRIVER_FIX.md)
に記録した。この時点ではユーザーの指示により、コード・自動テストの追加は行わずドキュメント化のみを
実施した（ホットプラグ連打・スリープ復帰時のスロット再割当ての実機/自動テストによる裏付けが
`tests/`配下に無いことは、同ドキュメントに未検証事項として記録済み）。

---

## 2026-09-01: 上記の未検証事項に対する実機テスト手順・監視ツールを追加

上記エントリで未検証事項として記録した「ホットプラグ連打・スリープ復帰時のスロット再割当て」
について、ユーザーからの追加依頼を受け、検証手順とツールを`tests/slot_reassignment/`に追加した:

- `hotplug_monitor.cpp`/`HotplugMonitor.vcxproj`: 20スロット全ての`\\.\interceptionNN`に対して
  `IOCTL_GET_HARDWARE_ID`を定期ポーリングし、スロット割当ての変化をタイムスタンプ付きで記録する
  常駐監視ツール。`IOCTL_SET_FILTER`/`IOCTL_READ`/`IOCTL_WRITE`を呼ばないため、監視中もキーボード/
  マウスは通常通り使える(`tests/upstream_lib/identify3.cpp`と同じ生プロトコル方式、
  `interception.dll`非依存)。
- `README.md`: シナリオ1〜5(連続抜き差し、スロット枯渇からの回復、スリープ/レジューム、
  スリープ前後の抜き差し、長時間繰り返しでのリーク検出)の手順と、それぞれの実施回数の目安
  (シナリオ3は各スリープ方式3〜5回、シナリオ4は3パターン×3回、シナリオ5は100回以上)を記録。

`OpenInputBridge.sln`のDebug/Release構成にプロジェクトを追加した(ReleaseWHQL・ARM64は他の
テストツールと同様に対象外)。**実機での実施・観察結果の記録はまだ行っておらず**、
`tests/slot_reassignment/README.md`の「観察結果」節は空のまま。ドライバ本体
(`driver/common/slots.c`)への変更は行っていない。
