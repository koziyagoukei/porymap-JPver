# Porymap 日本語版

## ビジュアルイベントエディター

イベントタブで NPC・看板・座標トリガーを選び、Script 欄の「イベントコマンド編集」から開きます。会話・はい／いいえ・フラグ・変数・条件分岐などを GUI で作成し、生成コードを確認して `.inc` / `.pory` に保存できます。新しいラベルを設定した場合は、最後に Porymap 本体でもマップを保存してください。

対応範囲・データ保護・検証内容は [ビジュアルイベントエディターの説明](VISUAL-EVENT-EDITOR-JA.md) を参照してください。

Pokémon 第3世代の逆コンパイルプロジェクト用マップエディター Porymap 6.3.1 の日本語版です。

## 起動

`Porymap-JP-windows-x64.zip` をすべて展開して、`Porymap-JP` フォルダー内の `porymap.exe` を起動してください。Windows 64 ビット用です。DLL や `platforms` などの同梱フォルダーも必要なので、exe だけを移動しないでください。Qt のインストールは不要です。

「ファイル」→「プロジェクトを開く...」で、`pokeemerald` などのプロジェクトフォルダーを選択します。`.gba` ROM を直接開くツールではありません。

メニュー、編集画面、設定、ツールチップ、主要な確認・エラーメッセージは起動時から日本語で表示します。マップ名、ポケモンの定数名、スクリプト名、ファイルパスなどのプロジェクト由来の識別子は変更しません。付属の英語マニュアル、変更履歴、開発者向けの詳細ログなどには原文が残ります。

基本タイルセット / 追加タイルセットは Primary / Secondary Tileset、通行設定は Collision、定型パーツは Prefab、タウンマップは Region Map に対応します。

## ビルド

Qt 6.8.2（MinGW 64-bit、Charts・QML・Network を含む）と MinGW 13.1 を使用しました。

```powershell
powershell -ExecutionPolicy Bypass -File tools/build-japanese.ps1 -SmokeTest
```

Qt の場所が異なる場合は `-QtRoot` と `-MingwRoot` を指定します。配布物は `dist/Porymap-JP` と `dist/Porymap-JP-windows-x64.zip` に生成されます。

日本語カタログは `translations/porymap_ja.ts`、訳語の一覧は `translations/ja.tsv` です。文言を更新するときは `lupdate src include forms -ts translations/porymap_ja.ts`、`python tools/localize.py`、上記ビルドの順で実行してください。Qt 標準ダイアログ用の `qtbase_ja.qm` も exe に組み込みます。

`-SmokeTest` は日本語表示、組み込み翻訳、イベント識別子の保持、タウンマップの形式を 6 通りで保存・読み戻しする検証を実行し、`build/smoke-results` に画面画像と結果を保存します。

検証で見つかった既存の JSON 処理の不具合も修正しています（キー検索結果の使い回し、文字列出力時の一時変数の寿命）。日本語表示したタウンマップの形式は、従来どおり英語の識別子で保存します。

## ライセンスと出典

Porymap と改変ソースのライセンスは同梱の `LICENSE.md` を参照してください。Qt 6.8.2 は動的リンクしており、Qt DLL は交換可能です。Qt のライセンス情報は配布物の `licenses` フォルダーを参照してください。

- Porymap: https://github.com/huderlem/porymap
- Qt ソースコード: https://download.qt.io/archive/qt/6.8/6.8.2/single/

上流の更新通知や公式ダウンロードは英語の通常版を案内する場合があります。日本語版を更新する際は、このフォークのソースから再ビルドしてください。
