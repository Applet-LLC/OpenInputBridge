<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# precedence_blackbox/

実物の（LGPLライセンス版）Interceptionドライバに対する、ブラックボックスI/O観察テスト。
複数プロセスが同一デバイスを異なる`IOCTL_SET_PRECEDENCE`値で同時にフックした際の配送順序を
観察し、M5実装の仕様として採用する。逆アセンブル等は行わず、外部から観測可能な
`DeviceIoControl`の入出力のみを対象とする（`docs/CLEAN_ROOM.md`参照）。観察結果・結論は
このディレクトリと`docs/CLEAN_ROOM.md`の両方に記録すること。

## `precedence_probe`（OpenInputBridge自身に対するM5実機テスト）

上記は実物ドライバに対する仕様確認用だが、`precedence_probe.cpp`（自作・MITライセンス）は
**OpenInputBridge自身**の precedence フックチェーン実装（`driver/common/ioctl.c`の
`OibFindNextChainRecipient`/`OibIsHigherPriority`/`OibCtlHandleWrite`）を実機検証するための
対話式ツール。無改変の`interception.h`/`interception.dll`を使う点は`tests/upstream_lib/`の
サンプル群と同じ（`PrecedenceProbe.vcxproj`参照）。

同一の`\\.\interceptionNN`は複数プロセスから同時に開ける（実機確認済み。`identify.exe`/
`identify2.exe`を複数ウィンドウで同時起動できることを確認済み）。

### ビルド

`OpenInputBridge.sln`のDebug/Release構成に含まれる。単体なら:

```bat
msbuild tests\precedence_blackbox\PrecedenceProbe.vcxproj /p:Configuration=Release /p:Platform=x64
```

出力先は`tests\upstream_lib\x64\Release\precedence_probe.exe`（`interception.dll`と同じ場所）。

### 使い方

```bat
precedence_probe.exe <precedence> [--consume]
```

- `<precedence>`: このインスタンスの precedence（整数。未指定時の既定値は0）
- `--consume`: 捕捉したストロークを送り返さない（チェーンより下・実際のハードウェアには一切
  届かなくなる）。省略時は素通し（チェーンを継続、キーボードは普段通り使える）

キーボードでEscを押すと終了する。

### テストシナリオ

2つ以上のコマンドプロンプトを開いて、それぞれで実行する。

**1. 配送順序とチェーン継続の確認**

```
ウィンドウA: precedence_probe.exe 10
ウィンドウB: precedence_probe.exe 0
```

キーを押す → Aが先に表示され、続けてBも表示されることを確認する（高precedenceが先に受け取り、
送り返した内容が低precedenceへ引き継がれる）。キーボードは普段通り動く。

**2. 捕捉による遮断の確認**

```
ウィンドウA: precedence_probe.exe 10 --consume
ウィンドウB: precedence_probe.exe 0
```

キーを押す → Aだけが表示され、Bには何も表示されない。かつ、そのキー入力はどのウィンドウにも
実際には入力されない（Aが握り潰すため、Bにもハードウェア＝実際のOS入力にも届かない）ことを確認する。

**3. 同precedenceでのアタッチ順タイブレークの確認**

```
ウィンドウA: precedence_probe.exe 5   （先に起動）
ウィンドウB: precedence_probe.exe 5   （後で起動）
```

どちらも`--consume`を付けないので、テスト1と同様にAが受け取って送り返した内容がBにも
チェーン継続で届き、**両方のウィンドウに表示されるのが正しい**（Bに何も表示されない場合は
むしろおかしい）。ここで確認すべきは表示の有無ではなく**順序**: キーを押す → 常にAが先に表示され、
その後Bが表示されることを確認する（precedenceが同値の場合、先にアタッチした
方が優先されるはず）。

観察結果・期待通りだったかどうかは、このREADMEまたは`docs/CLEAN_ROOM.md`に追記すること。

### 観察結果（2026-07-30、実機）

3シナリオすべて実機で確認済み、いずれも想定通り。

1. 配送順序とチェーン継続: 想定通り
2. 捕捉による遮断: 想定通り
3. 同precedenceでのアタッチ順タイブレーク: 想定通り。マイクロ秒精度のタイムスタンプ表示に
   修正した上で確認したところ、常にAが先に表示された（`OibIsHigherPriority`の
   `AttachSequence`比較が正しく機能している）。

`driver/common/ioctl.c`のprecedenceフックチェーン実装（`OibFindNextChainRecipient`/
`OibIsHigherPriority`/`OibCtlHandleWrite`）は、配送順序・チェーン継続・捕捉遮断・
同precedenceのタイブレークいずれも実機で確認済み。
