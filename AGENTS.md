# AGENTS.md

EXPO RACE: Project PLATEAU の万博会場モデルを使った、自作 DirectX 11 フレームワーク上のドローン散策・レースゲーム。
ゲームの内容とドキュメント一覧は [readme.md](readme.md) を参照し、その内容に従う。

## 基本ルール

- 日本語で返答・思考する
- サブエージェントは使わない
- ビルドチェックは不要
- ユーザーの指示に足りない点があれば、作業前に質問する
- git の履歴は原則参照しない。今のファイルを読んで問題点を探す

## コード規約

- 改行は CRLF、インデントはタブ（[.editorconfig](.editorconfig)）
- C/C++ (`.h` `.cpp` など) は UTF-8 BOM 付き、HLSL (`.hlsl` `.hlsli`) は BOM なし UTF-8
- 新しいシーンは手で作らず `python tool/manage_scene.py` で追加・削除する（`template/` のコピー、`app/scene.*`、`.vcxproj` / `.filters` への登録をまとめて行う）

## 実装後に必ずやること

- `python tool/encoding_converter.py` を実行する（文字コード・改行をそろえる安全なスクリプトなので、実行許可は取らない）

## 主な場所

- `framework/` エンジン本体、`shader/` HLSL、`app/` シーン管理
- `SCENE_TITLE/` `SCENE_GAME/` `SCENE_RESULT/` `SCENE_DEBUG/` 各シーン
- `document_framework/` フレームワークの仕様、`document_game/` ゲームの仕様・タスク一覧
- `tool/` ビルド・アセット変換・各種スクリプト
- `asset/expomodel/` と `asset/collision/expo_*.bin` は利用者が初回起動時に取得する万博アセット。再配布しない
