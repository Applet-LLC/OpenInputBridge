<!--
Copyright (c) 2026 OpenInputBridge Contributors
SPDX-License-Identifier: MIT
Licensed under the MIT License. See LICENSE file in the project root for full license text.
-->

# upstream_lib/

無改変の `third_party/interception/library/interception.c` / `.h` をビルドし、
本ドライバに対してoblitum/Interceptionライブラリ自身のサンプルアプリを動かす統合テスト。最も強い互換性の証拠。
M3（捕捉経路）・M4（再注入経路）・M8（総合互換性テスト）で使用する。

## ビルド方法

`InterceptionLib.vcxproj` が、無改変の `interception.c`（`third_party/interception/library/`
から直接参照。コピーはしない）を `interception.dll` としてビルドする。oblitum/Interceptionライブラリ
本来のビルド方法（`buildit(-x64).cmd` → 旧来のNT DDK `build.exe`、`.\sources`/`.\makefile`使用）は
本リポジトリで使っているEWDKには含まれない旧式のスタンドアロンWDK（`%WDK%\bin\setenv.cmd`）が
別途必要なため、代わりにこのプロジェクトファイル自体（自作・MITライセンス）でモダンなMSBuild/EWDK
トゥールチェーンを使ってビルドする。`interception.c`/`.h` 自体は一切変更していない
（`third_party/README.md`・`docs/CLEAN_ROOM.md` 参照）。

`OpenInputBridge.sln` のDebug/Release構成に含まれているので、ソリューション全体をビルドすれば
自動的にビルドされる（`ReleaseWHQL`構成には含まれない。配布物の一部ではないテストツールのため）。
単体でビルドする場合は以下（EWDKのビルド環境をセットアップしたコマンドプロンプトから）。

```bat
msbuild tests\upstream_lib\InterceptionLib.vcxproj /p:Configuration=Release /p:Platform=x64
```

出力先は `tests\upstream_lib\x64\Release\interception.dll`（Debugなら `x64\Debug\`）。

`interception.rc`（バージョンリソース）は同梱していない。旧WDKの `common.ver` に依存しており、
Windows SDK単体では解決できないため。ビルドされる `interception.dll` の実体・動作には影響しない。

`third_party/interception/samples/` 配下の各サンプル（`identify` 等）をこの `interception.dll` に
対して実行することで、M0〜M4の動作確認ができる。

## `identify` サンプルの実行（M0/M2/M3/M4のスモークテスト）

`IdentifySample.vcxproj` が、無改変の `samples/identify/identify.cpp` + `samples/utils.c` を
`identify.exe` としてビルドする（`InterceptionLib.vcxproj`と同じ理由・同じ方式。ビルド方法は上記と同様）。
出力先は `interception.dll` と同じ `tests\upstream_lib\x64\Release\`（`Debug`なら`x64\Debug\`）で、
`identify.exe`はそのフォルダにある`interception.dll`をそのまま読み込む。

事前に、対象マシンにOpenInputBridgeドライバがインストール・再起動済みであること（`sc.exe query
OpenInputBridge`でSTATE:RUNNINGを確認）。管理者権限は不要（コントロールデバイスのSDDLは
Everyoneに読み書きを許可している。`driver/driver.c`の`OibControlDeviceSddl`参照）。

```bat
cd tests\upstream_lib\x64\Release
identify.exe
```

実行すると、キーボード/マウスを操作するたびに `INTERCEPTION_KEYBOARD(n)` / `INTERCEPTION_MOUSE(n)`
がコンソールに出力され続ける（= M0のcontext作成成功 + M2のスロット番号割当 + M3のフィルタ経由の捕捉が
できている証拠）。操作したキーボード/マウスは普段通り動く（= M4の書き戻し=パススルーが機能している証拠）。
キーボードでEscを押すと終了する。

**既知の制約**: このサンプル（無改変のupstreamコード）は`interception_wait`が返した1台分だけを
処理してから次の待ちに戻る作りになっている。`interception_wait`内部の`WaitForMultipleObjects`は
複数ハンドルが同時にシグナル状態のとき「配列中で最小のインデックス」を返す仕様があり、キーボード
（インデックス0-9）はマウス（10-19）より先に並ぶため、キーボード操作が続くとマウス側の捕捉が
後回しになり、出力が滞留してまとまって出てくることがある。実機テストで確認済み。下記の`identify2`は
この点を改善したもの。

## `identify2`（改良版スモークテスト、推奨）

`identify`実行時に、キーボード操作を続けているとマウスの捕捉出力が滞留し、キーボード操作の
タイミングでまとまって出てくる現象が実機テストで確認された（上記「既知の制約」参照）。upstreamの
`identify.cpp`自体に手を入れる理由は無い（クリーンルーム方針・vendoring方針上、変更しない）ため、
代わりに自作の`identify2.cpp`（`tests/upstream_lib/identify2.cpp`、MITライセンス）を用意した。

`identify.cpp`との違い:

- **ラウンドロビン方式で全20デバイスを毎回スイープする**。`interception_wait`で誰かが起きたら、
  それだけを処理するのではなく、20デバイス全部を非ブロッキングの`interception_receive`で
  一巡（読める分が無くなるまで）確認する。`WaitForMultipleObjects`の「最小インデックス優先」の
  影響を受けないため、キーボードがマウスの捕捉を飢餓状態にすることがない。
- マウス側フィルタを左/右/中央ボタンのDOWN+UP全部に拡張（`identify`はLEFT_BUTTON_DOWNのみ）。
- 各行にコード/状態（16進）・マウスならボタン状態とx/y座標も表示し、何が起きているかより
  詳細に確認できる。

`Identify2Sample.vcxproj`でビルドされ、出力先・実行方法は`identify`と同じ
（`tests\upstream_lib\x64\Release\identify2.exe`）。

```bat
cd tests\upstream_lib\x64\Release
identify2.exe
```
