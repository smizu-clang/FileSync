# FileSync
File Syncing Tool　ファイル同期ツール

ローカルのフォルダーを同期する Qt Widgets デスクトップアプリです。C++26 / Clang 23 で開発しています。

## ビルド

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-23
cmake --build build
```

このプロジェクトは C++26 と `import std` を使います。CMake が Qt と同じ `libstdc++` 用の標準ライブラリモジュールをビルドフォルダ内に生成します。`/home/smizuno/workspace` の `std.pcm`/`std.o` は `libc++` 用なので、この Qt のビルドには使いません。

Ninja がない環境では `-G Ninja` を省略できます。CMake はビルド手順を生成し、Ninja はその手順でコンパイルします。アプリは通常の Qt ウィンドウとして起動します。

## 機能

- 複数の同期設定を保存し、手動または定期的に実行
- 透明感のある配色と、設定から切り替えられるライト／ダークテーマ
- 設定から日本語／Englishを切り替え
- Noto Sans JPを同梱し、日本語フォントがない環境でも表示
- 同期方向: 更新元 → 更新先、更新元 ← 更新先、双方向
- フォルダー内の重複ファイルを内容の SHA-256 が一致する場合に削除
- 下位フォルダー、拡張子フィルター、ミラー削除を設定可能
- 手動実行前にコピー、削除、競合、エラーをプレビュー
- 更新元・更新先のパスを含む更新ログとエラー記録
- 削除・上書き前に計画時のファイル内容と照合

同梱フォントはNoto Sans JP（SIL Open Font License 1.1）です。ライセンスは `assets/fonts/OFL.txt` に含まれています。
