# FileSync
File Syncing Tool　ファイル同期ツール

ローカルのフォルダ間で、新規・変更ファイルを同期先へコピーするシンプルなCLIです。
Qt Widgets を使った C++26 のデスクトップアプリです。Clang 23 と CMake/Ninja を使います。

## 使い方

## ビルド

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-23
cmake --build build
```

このプロジェクトは C++26 と `import std` を使います。CMake が Qt と同じ `libstdc++` 用の標準ライブラリモジュールをビルドフォルダ内に生成します。`/home/smizuno/workspace` の `std.pcm`/`std.o` は `libc++` 用なので、この Qt のビルドには使いません。

Ninja がない環境では `-G Ninja` を省略できます。CMake はビルド手順を生成し、Ninja はその手順でコンパイルします。アプリは通常の Qt ウィンドウとして起動します。

## 同期機能

- 複数の同期設定を保存し、手動または定期的に実行
- 片方向バックアップと、更新日時を使った双方向同期
- 下位フォルダ、拡張子フィルター、ミラー削除を設定可能
- ミラー削除は同期開始前に確認
- 実行ログを画面に表示
