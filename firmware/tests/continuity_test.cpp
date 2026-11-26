#include <cassert>
#include <iostream>

#include "Adafruit_AW9523.h"
#include "Adafruit_I2CDevice.h"
#include "../firmware.ino"

void tick(uint32_t now) { testNow = now; loop(); }
void sample() { tick(testNow + 1000); }
void expect(const std::string& output) {
    if (Serial.output != output) {
        std::cerr << "Expected:\n" << output << "Actual:\n" << Serial.output;
    }
    assert(Serial.output == output);
    Serial.output.clear();
}

int main() {
    continuityInputs = 0xFE;  // 子機1のみ導通あり。
    setup();
    expect("BOOT\nCONTINUITY child=1 connected=1\nCONTINUITY child=2 connected=0\nMODE mode=endless_chance\n");
    assert(continuityDirection == 0 && continuityLedMode == 0 && continuityInterrupts == 0);
    assert(continuityWrites == 0);
    assert(awAddress == 0x58 && awDirection == 3);
    // 初回読み取りは点灯チェックより前。子機1の点灯時間を差し引く。
    const uint32_t start = testNow - STARTUP_LED_ON_MS;
    const unsigned reads = continuityReads;
    loop();
    tick(start + 999);
    assert(continuityReads == reads);  // 1000 ms未満では読み取らない。
    tick(start + 1000);
    assert(continuityReads == reads + 1);
    expect("");

    // GPIO12/14は導通確認に使わない。
    levels[12] = LOW;
    levels[14] = LOW;
    sample();
    expect("");

    // 候補が元に戻ると確定せず、再度連続した読み取りを待つ。
    continuityInputs = 0xFF;
    sample();
    expect("");
    continuityInputs = 0xFE;
    sample();
    expect("");
    continuityInputs = 0xFF;
    sample();
    expect("");
    const unsigned beforeConfirm = continuityReads;
    tick(testNow + 999);
    assert(continuityReads == beforeConfirm);
    expect("");
    tick(testNow + 1);
    expect("CONTINUITY child=1 connected=0\n");

    // 子機2はピン1で独立して検知する。
    continuityInputs = 0xFD;
    sample();
    expect("");
    sample();
    expect("CONTINUITY child=2 connected=1\n");

    // 読み取り失敗をLOWと誤判定せず、状態不明を一度だけ通知する。
    continuityReadResult = false;
    sample();
    expect("ERROR continuity_read_failed\nCONTINUITY child=1 connected=unknown\nCONTINUITY child=2 connected=unknown\n");
    sample();
    expect("");
    continuityReadResult = true;
    sample();
    expect("");
    sample();
    expect("CONTINUITY child=1 connected=0\nCONTINUITY child=2 connected=1\n");

    // 確認途中で失敗した場合も、復旧後に連続した読み取りを待つ。
    continuityInputs = 0xFC;
    sample();
    expect("");
    continuityReadResult = false;
    sample();
    expect("ERROR continuity_read_failed\nCONTINUITY child=1 connected=unknown\nCONTINUITY child=2 connected=unknown\n");
    continuityReadResult = true;
    sample();
    expect("");
    sample();
    expect("CONTINUITY child=1 connected=1\nCONTINUITY child=2 connected=1\n");

    // millis()周回をまたいでも1000 ms周期と状態確定を維持する。
    continuityInputs = 0xFF;
    tick(UINT32_MAX - 499);
    const unsigned beforeWrap = continuityReads;
    tick(499);
    assert(continuityReads == beforeWrap);
    expect("");
    tick(500);
    assert(continuityReads == beforeWrap + 1);
    expect("CONTINUITY child=1 connected=0\nCONTINUITY child=2 connected=0\n");

    // 導通なしでも早押し受付・LED表示は継続する。
    setInputLevel(32, LOW);
    setInputLevel(33, LOW);
    loop();
    assert(buzzer_inputs::poll().ranking.count == 2);
    assert(awLevels[0] == HIGH && awLevels[1] == HIGH);
    assert(Serial.output.find("CONTINUITY") == std::string::npos);
    Serial.output.clear();

    // 読み取り失敗中も早押し順位を保持する。
    continuityReadResult = false;
    sample();
    expect("ERROR continuity_read_failed\nCONTINUITY child=1 connected=unknown\nCONTINUITY child=2 connected=unknown\n");
    assert(buzzer_inputs::poll().ranking.count == 2);
    assert(awLevels[0] == HIGH && awLevels[1] == HIGH);
    continuityReadResult = true;

    // 初期化失敗は状態不明。入力受付は維持し、読み取りはしない。
    for (int failure = 0; failure < 3; ++failure) {
        continuityBeginResult = failure != 0;
        continuityConfigureResult = failure != 1;
        continuityDeviceResult = failure != 2;
        setup();
        expect("BOOT\nERROR continuity_init_failed\nCONTINUITY child=1 connected=unknown\nCONTINUITY child=2 connected=unknown\nMODE mode=endless_chance\n");
        assert(inputsReady);
        const unsigned before = continuityReads;
        testNow += 1000;
        continuity_inputs::poll();
        assert(continuityReads == before);
    }
    continuityBeginResult = continuityConfigureResult = continuityDeviceResult = true;
    // 起動時の初回読み取り失敗からも復旧できる。
    continuityReadResult = false;
    continuity_inputs::begin();
    expect("ERROR continuity_read_failed\nCONTINUITY child=1 connected=unknown\nCONTINUITY child=2 connected=unknown\n");
    continuityReadResult = true;
    testNow += 1000;
    continuity_inputs::poll();
    expect("");
    testNow += 1000;
    continuity_inputs::poll();
    expect("CONTINUITY child=1 connected=0\nCONTINUITY child=2 connected=0\n");
    assert(continuityWrites == 0);
    std::cout << "Continuity integration tests passed\n";
}
