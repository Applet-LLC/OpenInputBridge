<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# 他のkbfiltr型フィルタドライバとの共存について

## 結論: 共存は非サポート

OpenInputBridge (`oib_kbd.sys`/`oib_mou.sys`) は、Applet LLC の `nodokad2.sys`（のどか用リマップドライバ）や
`kbdaddid.sys`（デバイス識別用フィルタ）など、同じ「kbfiltr型」（`IOCTL_INTERNAL_KEYBOARD_CONNECT`/
`IOCTL_INTERNAL_MOUSE_CONNECT` を横取りして `ClassService` を差し替える方式）の他社フィルタドライバとの
共存を**正式にはサポートしない**。OpenInputBridgeを使うユーザーは、`kbdaddid`/`nodokad2` を同時に
使わないことを前提とする。

以下は、2026-08時点でこの結論に至るまでに行った調査の記録。将来同様の質問が出た際に再調査しなくて
済むように残す。

## 背景

Applet LLCの「のどか」プロジェクト（`nodokad2.sys`）と、そのサブコンポーネント`kbdaddid.sys`は、
OpenInputBridgeと同じ「kbdclassより下・CONNECT横取り」というアーキテクチャパターンを採用している。
実運用環境で `oib_kbd.sys` / `kbdaddid.sys` / `nodokad2.sys` の3つが同一キーボードスタックの
UpperFiltersに同時に登録される状況が実際に発生し、この3者の相互作用を調査した。

## 調査で判明した事実

### 1. CONNECT横取り自体は位置非依存（クラッシュ・データ破損の意味では安全）

`OibKbdEvtInternalDeviceControl`（`driver/keyboard/kbdfilter.c`）の実装は、直上にいるのが本物の
`kbdclass`か、他のkbfiltr型フィルタかを一切区別せず、「1回だけCONNECTを受けてsave & forward」する
汎用的な作りになっている。`kbdaddid.sys`（`DispatchInternalIoctl`/`ServiceCallback`、
WDM/`IoAttachDeviceToDeviceStack`方式）も同様に自己完結しており、隣接ドライバの種類を前提にしていない。
そのため、3者をどの順序でUpperFiltersに並べてもロード自体が失敗したりクラッシュしたりすることはない。

### 2. kbdaddidは「CONNECT横取り」と「IRP_MJ_READ完了ルーチン」の二重の仕組みを持つ

`kbdaddid.sys`のソース（`Driver.c`）を確認したところ、ExtraInformationへのDeviceId刻印を
ServiceCallback（CONNECT横取り経由）だけでなく、`DispatchRead`/`ReadComplete`
（`IRP_MJ_READ`の完了ルーチンフック、旧nodokad.sys由来の技法）でも二重に行っている。
どちらも自分宛のIRP/コールバックだけを扱う自己完結した実装であり、位置関係そのものを壊す要因には
ならないが、想定より複雑な実装であることが分かった。

### 3. しかし「キー捕捉の優先順位」は位置に依存する

kbfiltr型フィルタは、物理キーストロークに対してハードウェアに一番近い（＝ServiceCallbackが最初に
呼ばれる）ドライバが、そのキーを完全に握って離さない権利を持つ。OpenInputBridgeのInterception
クライアントがキーを捕捉（キューに保留）すると、それより上のドライバは解放されるまでそのキーを
一切見ない。したがって、`oib_kbd.sys`をkbdaddid/nodokad2より上下どちらに置くかで、
「Interceptionクライアントとnodokaのリマップ、どちらが優先されるか」という実質的な挙動が変わる。

### 4. IOCTL_WRITEでの解放・合成注入は、CONNECT時点で保存した直上のみに配送される

`OibCtlHandleWrite`が捕捉済みストロークを解放・合成注入する際、呼び出し先は
CONNECT横取り時点で保存した`UpperConnectData`（＝その時点で直上にいたドライバ）に固定される。
そのため、`oib_kbd.sys`をkbdaddidより上に置くと、oib_kbd経由で解放・注入されたキーは
kbdaddidの刻印処理をバイパスしてしまう。これは`nodokad2.sys`のINJECT実装でも全く同じ理由で
起きる既知の挙動であり、kbfiltr型フィルタを複数チェーンする場合の一般的な特性である。

### 5. OpenInputBridgeのインストーラは他フィルタとの順序調整ロジックを持たない

`installer/common.cpp`の`ModifyUpperFiltersAtKey`は、既存のUpperFiltersリストから
`kbdclass`を探し、その直前に無条件で挿入するだけの実装。nodokad2側の`DriverManager.cpp`が持つ
`kKeyboardPreClassOrder`のような「既知の共存ドライバとの相対順序を保つ」仕組みは無い。
そのため、インストール順序次第で3者の相対位置が偶然決まってしまい、再インストール／アップデートの
たびに位置が黙って変わり得る（＝上記3・4項の挙動が意図せず変化し得る）ことが分かった。

## この結論に至った理由

上記4・5項の通り、真の意味で安全に共存させるには、OpenInputBridge側にkbdaddid/nodokad2を
認識した順序調整ロジックを実装する必要があるが、これは：

- OpenInputBridge自体の「Interception互換」という目的にとって本質的な機能ではない
- 対象ユーザー（無改造のInterceptionクライアントを使う層）とのどかユーザー層は重複を想定しない
- 実装・検証コストに見合わない

と判断し、**共存を正式サポート対象外とする**ことに決定した。OpenInputBridgeを使う環境では
`kbdaddid.sys`/`nodokad2.sys`のインストールを避けること。
