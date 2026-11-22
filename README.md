# FileSync
File Syncing Tool　ファイル同期ツール

ローカルのフォルダ間で、新規・変更ファイルを同期先へコピーするシンプルなCLIです。
C++17 の標準ライブラリだけで動作します。

## 使い方

まずは変更内容を確認します（既定ではコピーしません）。

```sh
g++ -std=c++17 -O2 -Wall -Wextra -pedantic filesync.cpp -o filesync
./filesync ./写真 ./バックアップ
```

内容を確認してから、実際に同期します。

```sh
./filesync ./写真 ./バックアップ --apply
```

FileSyncは同期元にあるファイルだけを同期先へコピーします。同期先だけにあるファイルは削除せず、シンボリックリンクはスキップします。
