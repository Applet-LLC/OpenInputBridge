<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# サードパーティ製ツール `interception-driver-fix` の調査とOIBへの示唆

## 結論: コード変更は不要。DACLロックダウン案の実需裏付けとしてのみ記録

本家（oblitum） Interceptionドライバの不具合を回避するサードパーティ製ツール
[`interception-driver-fix`](https://github.com/hygorostrowskij/interception-driver-fix)
（[deepwiki](https://deepwiki.com/hygorostrowskij/interception-driver-fix/1-overview)）を調査した。
結論として、このツールが解決している根本問題はOpenInputBridge（OIB）のアーキテクチャでは既に発生し
得ない設計になっており、**OIB側にコード変更は不要**と判断した。ただし、このツールが実装している
オプトインのDACLロックダウン機能は、`docs/SECURITY_CONSIDERATIONS.md`で以前から「検討が必要だが
未着手」としていた案と同一であり、実際に需要があることの裏付けとして記録する価値があると判断した。

以下は、2026-09時点でこの結論に至るまでに行った調査の記録。将来同様の質問（「〇〇というツールが
話題になっているが、OIBは大丈夫か」）が出た際に再調査しなくて済むように残す。

## 背景

ユーザーが `interception-driver-fix` というツールを発見し、それが何を実施しているか、OIBに
フィードバックすべき内容がないかの検討を依頼した。調査はリポジトリのREADMEおよびdeepwikiの
該当ページ（Overview、Project Purpose and Problem Statement、Core Fix Logic）をWebFetchで参照する
形で行った（リポジトリのクローンはしていない）。

## `interception-driver-fix` が解決している問題

- 本家Interceptionドライバ（レガシーWDMドライバ、Microsoftのkbfiltr/moufiltrサンプルに近い設計）は、
  デバイスのホットプラグや、スリープ復帰時にWindowsのPnPマネージャが実施する再列挙によって、
  `\Device\KeyboardClassN`/`\Device\PointerClassN`の`N`が単調に増えていく。本家ドライバは固定範囲の
  内部スロット/シンボリックリンクしか用意していないため、再列挙後のデバイスがその範囲外に
  割り当てられると、ハードウェア自体はPnP上で認識されているにもかかわらず、フィルタフックが
  usermodeへの経路を失い**入力が一切通らなくなる（"frozen"状態）**。
- 対策として、本家ドライバ本体は改変せず、**別プロセスのuser-mode one-shotサービス**を新設し、
  Windows起動時に`\Device\KeyboardClass0-9`（既定スロット）宛のシンボリックリンクを、
  `KeyboardClass10-19 → 0-9`、`20-29 → 0-9`……という形で大量（既定でキーボード/ポインタ各1000本）に
  事前生成しておく（"symlink folding"）。`NtCreateSymbolicLinkObject`に`OBJ_PERMANENT`フラグを
  付けてオブジェクトマネージャ名前空間に直接作成することで、サービス終了後もリンクを永続化している。
- 副次機能として、`\Device\Interception{00-XX}`のセキュリティディスクリプタをSDDL文字列で
  書き換える**オプトインのロックダウンモード**がある。既定は`D:(A;;FRFW;;;WD)(A;;FR;;;RC)(A;;FA;;;SY)(A;;FA;;;BA)`
  （Everyoneにも汎用読み書きを許可、本家の仕様に合わせた互換維持）だが、ロックダウン時は
  `D:(A;;FA;;;SY)(A;;FA;;;BA)`（SYSTEM/Administratorsのみ）に絞り込む。

## OIBとの突き合わせ

### 1. スロット/シンボリックリンクの枯渇・不整合問題は、OIBのアーキテクチャでは起きない

OIB（`driver/common/slots.c`, `driver/common/slots.h`）は本家と異なり、次のように設計されている:

- `DriverEntry`時に`OIB_TOTAL_DEVICE_SLOT_COUNT`（既定20、キーボード/マウスの内訳はレジストリで
  可変）個の固定スロットを一括確保する。
- PnPでフィルタFDOが到着するたびに、`OibSlotAssign`が空きスロットへ動的に割り当てる。
- デバイス除去時は、フィルタFDOの`EvtCleanupCallback`（`driver/keyboard/kbdfilter.c`,
  `driver/mouse/mousefilter.c`）から`OibSlotRelease`が呼ばれ、スロットが解放されて次のPnP到着に
  再利用可能になる。
- スロットが尽きた場合（接続台数がスロット数を超えた場合）も、`OibSlotAssign`は
  `STATUS_DEVICE_NOT_READY`を返すだけで、フィルタFDO自体は正常に構築され、入力は素通しされる
  （`\\.\interceptionNN`経由で操作できないだけ）。本家のような「デバイスは見えるのに入力が
  一切通らない」フリーズ状態にはならない、意図的な「グレースフルデグラデーション」設計になっている
  （`slots.h`の`OibSlotAssign`コメント参照）。

したがって、`interception-driver-fix`が対処している根本原因（固定範囲の外に再列挙されたデバイスが
永久に迷子になる）は、OIBでは「毎回空きスロットを動的に探して再利用する」設計によって構造的に
回避されている。symlink foldingのようなワークアラウンドをOIB側に追加移植する必要はない。

**未検証事項（テスト手順・ツールは追加済み、実機検証はこれから）**: 上記の動的再割当てが、
実機での**ホットプラグの連打**や**スリープ/レジューム**でも設計通りに動作するかを裏付ける
検証手順が`tests/`配下に存在しなかったため、[`tests/slot_reassignment/`](../tests/slot_reassignment/README.md)
に手順書と、スロット割当ての変化をタイムスタンプ付きで記録する監視ツール`hotplug_monitor.exe`を
追加した（カスタムの電源遷移ハンドラ（D0Entry/D0Exit等）は依然として実装しておらず、WDFの
既定動作に委ねたままなので、このテストは「既定動作で十分か」を裏付けるためのものであり、
実装変更を伴うものではない）。ただし**実機での実施・観察結果の記録はまだ行っていない**
（`tests/slot_reassignment/README.md`の「観察結果」節が空のまま）。設計上は問題ないはずだが、
実機検証のログが無い間は、将来この種の不具合報告が来た場合の一次切り分け対象として記録しておく。

### 2. DACLロックダウンは、OIB側の既存の未着手案と同一提案

`docs/SECURITY_CONSIDERATIONS.md`の「5. 検討が必要な対策案（未着手）」には、以前から
「`Everyone`への読み書き権限を制限する（DACLの絞り込み）」案が、本家プロトコル互換性
（無昇格プロセスからの利用）とのトレードオフを理由に未着手として記録されている。

`interception-driver-fix`のロックダウンモードは、まさにこれと同じ発想を**オプトイン**
（既定オフ、設定ファイルで有効化）として実装したものであり、実際にこの種の機能を求める
ユーザーが存在することの裏付けと言える。OIBで実装する場合も、本家互換性を損なわないよう
既定は現状維持（Everyoneに読み書き許可）のまま、opt-inの管理者限定モードを追加する、
という設計思想は流用できる。ただし、**実装するかどうか・する場合の方式（インストーラの
オプション機能にするか、監査ログ機能と同様にタスクスケジューラでの再適用にするか等）は
本ドキュメントの範囲外とし、別途判断する**。

### 3. 対象外: kbfiltr型フィルタドライバとの共存問題

`docs/COEXISTENCE.md`が扱う「複数のkbfiltr型フィルタドライバ（`nodokad2.sys`/`kbdaddid.sys`）を
同時にインストールした場合のキャプチャ優先順位・インストーラの順序調停」問題とは、
`interception-driver-fix`は無関係である。同ツールが扱うのはシンボリックリンクとACLの話であり、
複数フィルタドライバの相対位置を調停する機能は持っていない。混同しないよう、ここに明記しておく。

## 参照

- [`interception-driver-fix`](https://github.com/hygorostrowskij/interception-driver-fix)
  （調査時点のREADME・deepwikiの内容に基づく。リポジトリはクローンしていない）
- `driver/common/slots.c` / `driver/common/slots.h`（OIBのスロット割当て設計）
- [`docs/SECURITY_CONSIDERATIONS.md`](SECURITY_CONSIDERATIONS.md)（DACL絞り込み案の既存の検討記録）
- [`docs/COEXISTENCE.md`](COEXISTENCE.md)（問題領域が異なる別件であることの確認用）
- [`docs/DECISIONS.md`](DECISIONS.md)（2026-09-01の該当エントリ）
- [`tests/slot_reassignment/README.md`](../tests/slot_reassignment/README.md)（本ドキュメントの
  未検証事項を実際に検証するための実機テスト手順・`hotplug_monitor`ツール）
