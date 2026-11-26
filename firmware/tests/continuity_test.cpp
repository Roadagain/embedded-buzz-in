#include <cassert>
#include <iostream>

#include "Adafruit_AW9523.h"
#include "../firmware.ino"

void tick(uint32_t now) { testNow = now; loop(); }

int main() {
    levels[12] = LOW;
    setup();
    assert(Serial.output == "BOOT\nMODE mode=endless_chance\nCONTINUITY child=1 connected=1\n");
    Serial.output.clear();
    const uint32_t start = testNow;
    loop();
    assert(Serial.output.empty());

    // 途中で元に戻る短い変化は報告せず、最後の変化から20 ms待つ。
    levels[12] = HIGH;
    tick(start + 1);
    levels[12] = LOW;
    tick(start + 10);
    levels[12] = HIGH;
    tick(start + 15);
    tick(start + 34);
    assert(Serial.output.empty());
    tick(start + 35);
    assert(Serial.output == "CONTINUITY child=1 connected=0\n");
    Serial.output.clear();
    tick(start + 100);
    assert(Serial.output.empty());

    // millis()の周回をまたいでも再接続を検知する。
    levels[12] = LOW;
    tick(UINT32_MAX - 9);
    tick(9);
    assert(Serial.output.empty());
    tick(10);
    assert(Serial.output == "CONTINUITY child=1 connected=1\n");
    Serial.output.clear();

    // 導通なしでも仮実装ではボタン1・2の押下受付を維持する。
    levels[12] = HIGH;
    tick(20);
    tick(40);
    Serial.output.clear();
    setInputLevel(32, LOW);
    setInputLevel(33, LOW);
    loop();
    assert(buzzer_inputs::poll().ranking.count == 2);
    assert(awLevels[0] == HIGH && awLevels[1] == HIGH);
    assert(Serial.output.find("CONTINUITY") == std::string::npos);
    std::cout << "Continuity integration tests passed\n";
}
