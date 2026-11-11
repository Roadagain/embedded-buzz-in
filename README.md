# embedded-buzz-in
早押し機の組み込みプログラム

## Stream Deck・PCとの連携

PC上のHTTP中継プログラムを使うと、Stream DeckからUSBシリアル経由でリセット・正解・不正解を操作できます。macOSでは、先頭の押下や不正解後の繰り上がりに合わせてPCで効果音も再生できます。

環境構築、起動、Stream Deck設定、接続状態の確認は [中継プログラムの使い方](bridge/README.md) を参照してください。

## 子機2台の構成

親機は ESP32-DevKitC V4（ESP32-WROOM-32）を使用します。PCとは USB で接続し、試作機への給電とスケッチの書き込みに使います。子機はそれぞれ、3.5 mm TRS（3極）ジャック、モーメンタリの常開スイッチ、赤色LEDで構成します。

### 配線

親機と各子機を3極のストレートケーブルで1対1に接続します。ジャックの端子はプラグとの導通で特定してください。配線とケーブルの抜き差しは親機の電源を切った状態で行います。

| TRS端子 | 親機・子機1ポート | 親機・子機2ポート | 各子機側 |
| --- | --- | --- | --- |
| Tip（先端） | GPIO22 | GPIO18 | スイッチの片端 |
| Ring（中央） | GPIO23 → 330 Ω抵抗 | GPIO16 → 330 Ω抵抗 | LEDのアノード |
| Sleeve（根元） | GND | GND | スイッチのもう片端とLEDのカソード |

330 Ω抵抗は、親機側でGPIOとRingの間にポートごとに1本入れます。親機のリセットボタンは常開スイッチを GPIO15 と GND の間に接続します。スイッチ入力には ESP32 の内部プルアップを使用します。ピンの位置は [ESP32-DevKitC V4 のピン配置](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html) を参照してください。

### 書き込み

Arduino IDE で `firmware/firmware.ino` を開き、ESP32 Arduino コアの **ESP32 Dev Module** と親機のシリアルポートを選びます。Arduino CLI でも実行できます。`PORT` は `arduino-cli board list` で確認したポートに置き換えてください。

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 firmware
arduino-cli upload --fqbn esp32:esp32:esp32 --port PORT firmware
```

### シリアル通信

Arduino IDEのシリアルモニターを115200 bpsで開きます。起動時の `BOOT` を確認するには、モニターを開いた状態でESP32基板のENボタンを押します。

```text
BOOT
PRESS child=1 rank=1
PRESS child=2 rank=2
RESET
```

`PRESS` には受け付けた子機番号と着順を表示します。同じ子機の連打では追加のログは出ません。親機の入力リセットボタンを押すと `RESET` を表示します。

シリアルモニターから `r` を送信しても、親機の入力リセットボタンと同じように着順・LED・経過時間の起点をリセットできます。通信速度は115200 bpsです。コマンドは小文字1文字を受信すると即実行し、改行は不要です。CR・LFは無視し、それ以外の未定義の文字（空白を含む）には1文字ごとに `ERROR unknown_command` を返し、着順を変更しません。連続して受信した文字は順番に処理します。`o` は現在の先頭を正解とし、`JUDGE result=correct child=子機番号`、`RESET` の順に出力して全入力・LED・経過時間の起点をリセットします。`x` は現在の先頭を不正解として回答待ちから外し、`JUDGE result=wrong child=子機番号` を出力します。不正解になった子機はすぐ消灯します。元の2着は1秒間点灯を維持してから、新しい先頭として点滅を開始します。後続は消灯を維持し、1秒後に点灯します。待機中も押下を受け付けます。外れた子機は離して押し直すと末尾に再参加できます。不正解では経過時間の起点を変更しません。`o` と `x` は、回答者がいない場合に `ERROR no_answerer` を返し、状態を変更しません。

ボタンの押下はGPIOの立ち下がり割り込みで検出します。子機の着順は、割り込み処理が呼ばれた順に記録します。回答待ちにいる子機の追加押下は無視します。リセットボタンは、最後の立ち下がりから20 ms経過した時点で入力がLOWなら確定し、記録済みの着順を消去します。
