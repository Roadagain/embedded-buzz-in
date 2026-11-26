#include <cassert>
#include <iostream>

#include "Adafruit_AW9523.h"
#include "Adafruit_I2CDevice.h"
#include "../firmware.ino"

void expectStopped(const std::string& error, const std::string& continuity = "") {
    const std::string expected = "BOOT\n" + continuity + error + "\n";
    assert(Serial.output == expected);
    assert(!inputsReady);
    assert(!interrupts[32] && !interrupts[33] && !interrupts[27]);
    const uint32_t before = testNow;
    loop();
    assert(testNow == before + 10);
    assert(Serial.output == expected);
    Serial.output.clear();
}

int main() {
    wireBeginResult = false;
    setup();
    expectStopped("ERROR led_init_failed");
    wireBeginResult = true;
    awBeginResult = false;
    setup();
    expectStopped("ERROR led_init_failed");
    awBeginResult = true;
    awConfigureResult = false;
    setup();
    expectStopped("ERROR led_init_failed");
    awConfigureResult = true;
    awWriteResult = false;
    setup();
    expectStopped("ERROR led_init_failed");

    awWriteResult = true;
    continuityInputs = 0xFE;
    onTestDelay = [](uint32_t) { awWriteResult = false; };
    setup();
    onTestDelay = nullptr;
    expectStopped("ERROR led_startup_failed",
                  "CONTINUITY child=1 connected=1\nCONTINUITY child=2 connected=0\n");

    awWriteResult = true;
    continuityInputs = 0xFF;
    awCalls.clear();
    setup();
    assert(Serial.output == "BOOT\nCONTINUITY child=1 connected=0\nCONTINUITY child=2 connected=0\nMODE mode=endless_chance\n");
    Serial.output.clear();
    assert(inputsReady);
    assert(wireSda == 25 && wireScl == 26 && wireTimeout == 10);
    assert(awAddress == 0x58 && awWire == &Wire);
    assert(awDirection == 3 && awLedMode == 0 && !awOpenDrain);
    assert(awCalls[0] == "direction=0");
    assert(awCalls[1] == "output=0" && awCalls[2] == "direction=3");
    assert(awLevels[0] == LOW && awLevels[1] == LOW);
    assert(levels[23] == HIGH && levels[16] == HIGH);  // 旧LEDピンは操作しない。

    const unsigned writes = awWriteCount;
    loop();
    loop();
    assert(awWriteCount == writes);  // 同じ状態を毎loop送信しない。
    assert(led_outputs::set(true, false));
    assert(awLevels[0] == HIGH && awLevels[1] == LOW);
    assert(led_outputs::set(false, true));
    assert(awLevels[0] == LOW && awLevels[1] == HIGH);

    awWriteResult = false;
    assert(!led_outputs::set(true, true));
    awWriteResult = true;
    // 失敗前と同じ要求でも、途中まで書けた可能性があるので再送が必要。
    assert(led_outputs::set(false, true));
    assert(awLevels[0] == LOW && awLevels[1] == HIGH);
    assert(led_outputs::set(false, false));

    awWriteResult = false;
    setInputLevel(32, LOW);
    loop();
    assert(Serial.output == "PRESS child=1 rank=1 elapsed_ms=0\nERROR led_write_failed\n");
    Serial.output.clear();
    const unsigned beforeRetry = awWriteCount;
    loop();
    assert(awWriteCount == beforeRetry + 1 && Serial.output.empty());
    awWriteResult = true;
    loop();
    assert(!ledWriteError && awLevels[0] == HIGH && awLevels[1] == LOW);
    const unsigned afterRecovery = awWriteCount;
    loop();
    assert(awWriteCount == afterRecovery && Serial.output.empty());
    std::cout << "LED output tests passed\n";
}
