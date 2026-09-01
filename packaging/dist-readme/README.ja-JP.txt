OpenInputBridge インストールガイド　1.00 2026-08-20 Applet LLC
==============================================================

このパッケージは、Interception プロトコル互換のWindows用キーボード/マウス
入力インターセプトドライバ「OpenInputBridge」をインストールします。
本製品は評価版であり、デバイスドライバは、テスト署名版あるいは、EV署名版となります。

動作環境
--------
- Windows 11 24H2以上
- インストールのための管理者権限
- テスト署名モード
　テスト署名のデバイスドライバがロードできるよう設定された環境が必要です。
　すなわち、BitLocker/セキュアブート未使用 であり、かつ
　bcdedit /set TESTSIGNING ON設定後、再起動が必要です。 

インストール手順
----------------
1. 「setup.bat」を実行してください。
2. インストール完了には再起動が必要です。

【重要】展開先のフォルダについて
このzipは、展開した場所がそのままインストール先になります。インストール後にこの
フォルダを移動・削除しないでください。また、OneDriveなど同期対象になっているフォルダ
（同期対象の「デスクトップ」フォルダ等を含みます）の配下には展開しないでください。
監査ログ・トースト通知機能が正しく動作しなくなることを確認しています。
「C:\OpenInputBridge」のような、同期されないローカルのフォルダへの展開を推奨します。

パッケージ内のファイル
----------------------
- setup.bat
  セットアップのためのbatファイル

- OpenInputBridgeSetup.exe
　セットアップファイルの本体です。

- oib_kbd, oib_mou, Symbold
　OpenInputBridgeをbuildして作成したデバイスドライバとシンボルファイルです。
　これらのファイルは再配布不可です。デバイスドライバにはEV署名がついているものとなり、便宜上、評価可能な形にしているものです。

- OibToastHelper.exe
- OibToastHelper.ico
  デバイスドライバが呼ばれたことを示すトースト表示用バイナリとアイコンファイルです。

- LICENSE
  ソースコードがMITライセンスであることを示すものです。

- README.md
  https://github.com/Applet-LLC/OpenInputBridge のREADMEです。
　より詳細なドキュメントとなっています。免責事項もご覧ください。
　日本語で書いてあるので、大変申し訳ありませんが、必要に応じて母国語に訳してください。

- README.en-US.txt
- README.ja-JP.txt
  本 README です。

監査ログ・トースト通知
--------------------------------------
setup.batでインストールすると「監査ログ」「トースト通知」が有効となります。

Interception互換のデバイスをどのプロセスが開いたかをWindows標準のセキュリティイベントログに記録し、その発生をデスクトップのトースト通知でその場でお知らせします。

具体的には
「イベントビューアー → Windowsログ → セキュリティ」を開き、イベントID 4656、 ObjectNameが\Device\interceptionNN（NNは00〜19の数字）になっているものを探してください。プロセス名が意図したプロセスかどうかを確認してください。心当たりのないプロセス名が 記録されていた場合、そのプロセスが実際にInterceptionプロトコル互換のキー/マウス入力を 観測・注入できる状態にあることを意味します。

信頼している特定のソフトウェアについて通知だけを表示したくない場合は、管理者権限の
コマンドプロンプトから以下のように操作してください。
なお、セキュリティイベントログへの記録はホワイトリスト登録の有無に関わらず継続します。

ホワイトリストに設定する場合
    OpenInputBridgeSetup.exe --allow-process "C:\full\path\to\app.exe"

ホワイトリストから削除する場合
    OpenInputBridgeSetup.exe --disallow-process "C:\full\path\to\app.exe"

現在のホワイトリスト一覧表示
    OpenInputBridgeSetup.exe --list-allowed-processes


アンインストール
----------------
OpenInputBridgeSetup.exe /uninstallを管理者権限のプロンプトで
実行し、その後、PCを再起動してください。

デバイスドライバのアンインストールには、pnputil -d oemXX.infは使用しないでください。不完全にアンインストールが実施され、再起動後、キーボードやマウスが使用不可となります。


購入
--------------
WQHL署名が付いたデバイスドライバは、以下のサブスクリプション版あるいは
Pro版を購入すると入手可能です。ご検討ください。

https://applet.gumroad.com/l/hbxqex
OpenInputBridge-Subscription
3か月ごとの課金となります。

https://applet.gumroad.com/l/xsggij
OpenInputBridge-Pro
1回払いとなります。


ソースコード
------------
https://github.com/Applet-LLC/OpenInputBridge

ソースコードは、MIT Licenseです。
zipファイルに同梱されたデバイスドライバは、評価用のものであり
再配布不可です。



販売・開発元
------------
Applet LLC
https://appletllc.com/ appletllc@gmail.com

