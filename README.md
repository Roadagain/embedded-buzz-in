# embedded-buzz-in
早押し機の組み込みプログラム

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

シリアルモニターから `reset` を送信しても、親機の入力リセットボタンと同じように着順・LED・経過時間の起点をリセットできます。通信速度は115200 bps、改行設定は「LF」「CR」「CRとLF」のいずれかにしてください。コマンドは小文字で入力し、空行は無視します。不明なコマンドには `ERROR unknown_command`、31文字を超える行には `ERROR command_too_long` を返し、着順を変更しません。`correct` は現在の先頭を正解とし、`JUDGE result=correct child=子機番号`、`RESET` の順に出力して全入力・LED・経過時間の起点をリセットします。回答者がいない場合は `ERROR no_answerer` を返し、経過時間の起点は変更しません。現段階では `wrong` は未実装です。

ボタンの押下はGPIOの立ち下がり割り込みで検出します。子機の着順は、割り込み処理が呼ばれた順に記録します。子機ごとに、リセットまで最初の1回だけを着順に登録します。リセットボタンは、最後の立ち下がりから20 ms経過した時点で入力がLOWなら確定し、記録済みの着順を消去します。
