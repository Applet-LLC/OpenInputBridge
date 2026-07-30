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

**テスト時の注意**: マウスクリックをコンソールウィンドウ自身の上で行うと、Windowsコンソールの
QuickEditモード（クリック＝範囲選択開始）が働き、選択解除するまで出力が一時停止して見えることがある。
これは`identify`やドライバの問題ではなくコンソール自体の挙動なので、メモ帳など他のウィンドウを
アクティブにした状態でキーボード/マウスを操作するか、コンソールでクリックした後は一度Enterキーを
押して選択解除するとよい。

## `identify2`（詳細出力版）

`identify2.cpp`（`tests/upstream_lib/identify2.cpp`、自作・MITライセンス。`identify.cpp`自体は
クリーンルーム/vendoring方針上変更しないため、別ファイルとして用意）は、`identify`と同じ用途で
使える、出力内容をより詳しくしたサンプル。

`identify.cpp`との違い:

- 各行にコード/状態（16進）・マウスならボタン状態とx/y座標も表示し、何が起きているかより
  詳細に確認できる。
- マウス側フィルタを左/右/中央ボタンのDOWN+UP全部に拡張（`identify`はLEFT_BUTTON_DOWNのみ）。
- `interception_wait`で1台起きるたびに、全20デバイスを非ブロッキングの`interception_receive`で
  一巡確認してから次の待ちに戻るラウンドロビン方式を採用（`identify`は起きた1台分だけを処理する）。

`Identify2Sample.vcxproj`でビルドされ、出力先・実行方法は`identify`と同じ
（`tests\upstream_lib\x64\Release\identify2.exe`）。

```bat
cd tests\upstream_lib\x64\Release
identify2.exe
```

## `hardwareid`（M2: `IOCTL_GET_HARDWARE_ID`のスモークテスト）

無改変の`samples/hardwareid/hardwareid.cpp`を`HardwareIdSample.vcxproj`でビルドしたもの
（`identify`/`identify2`と同じ理由・同じ方式）。キーボード操作・マウス左クリックのたびに、
そのイベントを発生させた物理デバイスのハードウェアIDを`IOCTL_GET_HARDWARE_ID`経由で取得して表示する。

```bat
cd tests\upstream_lib\x64\Release
hardwareid.exe
```

実行してキーボード/マウスを操作し、`USB\VID_xxxx&PID_xxxx\...`のような、そのデバイスの実際の
ハードウェアIDらしき文字列が表示されればM2の合格基準を満たす。空文字列や明らかにおかしい値しか
出ない場合はドライバ側の`OibCtlHandleGetHardwareId`（`driver/ioctl.c`）を要確認。

## `caps2esc`（M4/M8: 実際のキーリマップ動作の確認）

`identify`/`identify2`/`hardwareid`/`precedence_probe`は、捕捉したストロークを**中身を変えずに
そのまま**送り返すだけで、いわゆるキーリマップ（捕捉した内容を書き換えてから送り返す）の動作は
検証していない。`caps2esc`（`third_party/interception/samples/caps2esc/caps2esc.cpp`、無改変）は
CapsLockとEscapeを入れ替える実際のリマップツールで、この観点の最初のテストになる
（`CapsToEscSample.vcxproj`でビルド。`Ctrl+CapsLock`の組み合わせも`Ctrl`として機能するよう
考慮された実装になっている）。

元のサンプルは`UMTYPE=windows`（コンソール無し、タスクマネージャーから終了する想定）でビルドされる
設計だが、テストツールとして起動確認・終了がしやすいよう、ビルド設定のみ`Console`サブシステムに
変更している（`caps2esc.cpp`自体は無改変）。

```bat
cd tests\upstream_lib\x64\Release
caps2esc.exe
```

実行した状態でCapsLockキーを押すとEscapeとして機能する（＝ウィンドウを閉じたりできる）ことを
確認する。逆にEscapeキーを押すとCapsLockとして機能する（＝Caps Lockのオン/オフが切り替わる）ことも
確認する。`Ctrl`を押しながらCapsLockを押すと、通常の`Ctrl`+何かの組み合わせとして機能することも
確認するとよい。二重起動防止（`try_open_single_program`）が入っているため、2つ目のプロセスは
即座に終了する（正常な動作）。終了はコンソールウィンドウを閉じるか`Ctrl+C`。

## `axes`（マウス側の中身書き換えテスト）

`caps2esc`のマウス版。無改変の`samples/axes/axes.cpp`（`AxesSample.vcxproj`）を実行すると、
相対移動時のマウスのY軸（縦方向）が反転する。マウスを動かして、縦方向だけ逆に動くこと
（左右はそのまま、上下だけ逆転）を確認する。キーボードのDOWN/UPはそのまま素通しされる
（Escで終了）。

```bat
cd tests\upstream_lib\x64\Release
axes.exe
```

## `cadstop`（`INTERCEPTION_FILTER_KEY_ALL`・Ctrl+Alt+Del捕捉のテスト）

無改変の`samples/cadstop/cadstop.cpp`（`CadStopSample.vcxproj`）。これまでのツールは全部
`INTERCEPTION_FILTER_KEY_DOWN|UP`のみを使っていたが、これは`INTERCEPTION_FILTER_KEY_ALL`
（0xFFFF）を使うため、E0/E1等の他のビットも含めたフィルタ照合の追加確認になる。また、
Win32のフック（`SetWindowsHookEx`等）では原理的にブロックできないCtrl+Alt+Delの捕捉を試みる、
kbdclassより下でフックする本ドライバならではの動作確認でもある。

```bat
cd tests\upstream_lib\x64\Release
cadstop.exe
```

実行した状態でCtrl+Alt+Delを押し、通常のセキュリティ画面（Windowsのロック画面等）が
**出ない**ことを確認する（＝捕捉できている証拠）。コンソールに`ctrl-alt-del pressed`と
表示されることも合わせて確認する。念のため: 何か問題があっても実害はなく、コンソール
ウィンドウを閉じれば通常通りCtrl+Alt+Delが機能する状態に戻る。

## `mathpointer`（絶対座標・高頻度連続WRITEのテスト）

無改変の`samples/mathpointer/mathpointer.cpp`（`MathPointerSample.vcxproj`）。これまでの
ツールはすべて相対移動の素通し/変換だったが、これは`INTERCEPTION_MOUSE_MOVE_ABSOLUTE`
（絶対座標指定）でのマウス移動と、1本の曲線あたり数百回に及ぶ連続`IOCTL_WRITE`呼び出しを
テストできる。**実機での動作を想定したサンプル**（仮想マシンでは正しく動かない場合がある、と
サンプル自身が起動時に警告する）。

```bat
cd tests\upstream_lib\x64\Release
mathpointer.exe
```

実行するとまず「なりすます対象のマウスを動かしてください」と表示されるので、実際に使う
マウスを少し動かす。その後、ペイント等の描画アプリを開いてお絵描きモードにし、数字キー
（テンキーではない方の0〜9）を押すと、その数字に対応した曲線をマウスカーソルが自動で
描画する（クリック状態も自動制御される）。Escで終了。曲線がなめらかに正しく描画されれば、
絶対座標指定・高頻度連続WRITEとも問題なく機能している証拠になる。
