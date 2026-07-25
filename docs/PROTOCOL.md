<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# ワイヤプロトコル仕様

このドキュメントは、OpenInputBridgeドライバが実装する、ユーザーモードとの通信プロトコルの仕様です。

**情報源について**: 以下の仕様は、`third_party/interception/library/interception.c` / `interception.h`
(LGPL、[oblitum/Interception](https://github.com/oblitum/Interception) より無改変でvendor)
を読み解いて独自に再記述したものです。プロプライエタリなカーネルドライバ（.sys）のソースコードやバイナリの
逆アセンブル結果は一切参照していません。詳細は [CLEAN_ROOM.md](CLEAN_ROOM.md) を参照してください。

## デバイス構成

| 項目 | 値 |
|---|---|
| `INTERCEPTION_MAX_KEYBOARD` | 10 |
| `INTERCEPTION_MAX_MOUSE` | 10 |
| `INTERCEPTION_MAX_DEVICE` | 20 |

- デバイス番号 1〜10 = キーボード（インデックス0〜9）
- デバイス番号 11〜20 = マウス（インデックス10〜19）
- デバイスパス: `\\.\interceptionNN`（`NN` は2桁ゼロ埋め、`00`〜`19`。インデックス `i` → デバイス名の `NN` は `i` そのもの）
- `CreateFileA` は `GENERIC_READ` のみで `OPEN_EXISTING` オープンされる（共有モード0）

**互換上の重要な制約**: upstreamのユーザーモードライブラリ `interception_create_context()` は、20個の
デバイスすべてのオープンに成功しないと `NULL` を返す。したがって本ドライバは、**物理的なキーボード/マウスの
接続台数に関わらず、常に20個すべてのコントロールデバイスを公開し続けなければならない**。

## IOCTLコード

すべて `CTL_CODE(FILE_DEVICE_UNKNOWN, function, METHOD_BUFFERED, FILE_ANY_ACCESS)` で定義される。

| 名前 | function (16進) | 概要 |
|---|---|---|
| `IOCTL_SET_PRECEDENCE` | `0x801` | このハンドルの優先順位(`int`)を設定 |
| `IOCTL_GET_PRECEDENCE` | `0x802` | このハンドルの優先順位(`int`)を取得 |
| `IOCTL_SET_FILTER` | `0x804` | このハンドルのフィルタビットマスク(`unsigned short`)を設定 |
| `IOCTL_GET_FILTER` | `0x808` | このハンドルのフィルタビットマスク(`unsigned short`)を取得 |
| `IOCTL_SET_EVENT` | `0x810` | 非空通知用イベントハンドルを登録 |
| `IOCTL_WRITE` | `0x820` | ストロークを書き込む（合成注入 / 捕捉済みストロークの解放） |
| `IOCTL_READ` | `0x840` | 捕捉済みストロークを読み出す（非ブロッキング） |
| `IOCTL_GET_HARDWARE_ID` | `0x880` | 下位デバイス(PDO)のハードウェアID文字列を取得 |

## データ構造（標準NT DDK構造体）

ワイヤ上でやり取りされる構造体は、Interception独自形式ではなく、標準のNT DDK構造体そのものである。
Interception独自の `InterceptionKeyStroke` / `InterceptionMouseStroke` への変換は、ユーザーモードライブラリ側
（`interception.c`）でのみ行われている。したがってドライバは以下の標準構造体のみを扱えばよい。

```c
typedef struct _KEYBOARD_INPUT_DATA
{
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    ULONG  ExtraInformation;
} KEYBOARD_INPUT_DATA, *PKEYBOARD_INPUT_DATA;

typedef struct _MOUSE_INPUT_DATA
{
    USHORT UnitId;
    USHORT Flags;
    USHORT ButtonFlags;
    USHORT ButtonData;
    ULONG  RawButtons;
    LONG   LastX;
    LONG   LastY;
    ULONG  ExtraInformation;
} MOUSE_INPUT_DATA, *PMOUSE_INPUT_DATA;
```

## 各IOCTLの意味論

- **`IOCTL_SET_EVENT`**: 入力バッファは `HANDLE[2]`（1要素目のみ使用、2要素目は常に0）。ドライバは
  呼び出し元プロセスコンテキストで `ObReferenceObjectByHandle` によりイベントオブジェクトを参照し、
  このハンドル（コントロールデバイスのオープンインスタンス）の捕捉キューが「空→非空」に遷移した瞬間に
  `KeSetEvent` でシグナルする。ユーザーモード側の `interception_wait` / `interception_wait_with_timeout` は
  20個ぶんのイベントを `WaitForMultipleObjects` で待ち受ける。
- **`IOCTL_GET_FILTER` / `IOCTL_SET_FILTER`**: `unsigned short` のビットマスク。デバイスから来たイベントの
  うち、どの種別を「捕捉してユーザーモードに渡す（＝ハードウェアには即座に反映させず保留する）」か、
  「素通しする」かを、オープンインスタンス単位・デバイス単位で制御する。
- **`IOCTL_GET_PRECEDENCE` / `IOCTL_SET_PRECEDENCE`**: `int`。同一の物理デバイスを複数プロセスが同時に
  フックしている場合の優先順位。実装の詳細（複数コンテキストへの配送方式が「各々に独立コピー」なのか
  「優先順位チェーンで順に手渡し」なのか）は、公開ライブラリのソースだけからは断定できないため、
  実物ドライバに対するブラックボックスI/O観察（`tests/precedence_blackbox/`）で検証してから実装する。
- **`IOCTL_READ`**: 出力バッファに、捕捉済みで未読の `KEYBOARD_INPUT_DATA` / `MOUSE_INPUT_DATA` を
  詰めて返す非ブロッキング呼び出し。現在キューにある分だけ（呼び出し元のバッファサイズを上限として）返す。
- **`IOCTL_WRITE`**: 入力バッファの `KEYBOARD_INPUT_DATA` / `MOUSE_INPUT_DATA` 配列を、実際の入力ストリームへ
  再注入する。合成入力の注入と、`IOCTL_READ`で捕捉したストロークの解放は、同じ経路で扱われる。
- **`IOCTL_GET_HARDWARE_ID`**: 出力バッファに、下位デバイス(PDO)のハードウェアIDプロパティ文字列を
  呼び出し元バッファサイズに収まる範囲で返す。

## 参照実装ファイル（vendored, 無改変）

- `third_party/interception/library/interception.c`
- `third_party/interception/library/interception.h`
