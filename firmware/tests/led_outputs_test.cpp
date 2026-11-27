#include <algorithm>
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
    mcpBeginResult = false;
    setup();
    expectStopped("ERROR led_init_failed");
    mcpBeginResult = true;
    // 初期化の各レジスタ書き込みが失敗すると入力受付を始めない。
    for (int reg : {0x05, 0x0A, 0x04, 0x0C, 0x0D, 0x14, 0x15, 0x00, 0x01}) {
        mcpFailRegister = reg;
        setup();
        expectStopped("ERROR led_init_failed");
    }
    mcpFailRegister = -1;
    mcpWriteResult = false;
    setup();
    expectStopped("ERROR led_init_failed");

    mcpWriteResult = true;
    continuityInputs = 0xFE;
    onTestDelay = [](uint32_t) { mcpWriteResult = false; };
    setup();
    onTestDelay = nullptr;
    expectStopped("ERROR led_startup_failed",
                  "CONTINUITY child=1 connected=1\nCONTINUITY child=2 connected=0\n");

    mcpWriteResult = true;
    continuityInputs = 0xFF;
    mcpCalls.clear();
    setup();
    assert(Serial.output == "BOOT\nCONTINUITY child=1 connected=0\nCONTINUITY child=2 connected=0\nMODE mode=endless_chance\n");
    Serial.output.clear();
    assert(inputsReady);
    assert(wireSda == 25 && wireScl == 26 && wireTimeout == 10);
    assert(mcpAddress == 0x20 && mcpWire == &Wire);
    assert(mcpRegisters[0x00] == 0x7C && mcpRegisters[0x01] == 0x7F);
    assert(mcpRegisters[0x0A] == 0 && mcpRegisters[0x04] == 0 && mcpRegisters[0x05] == 0);
    assert(mcpRegisters[0x0C] == 0 && mcpRegisters[0x0D] == 0);
    // ラッチをLOWにしてから対象ピンを出力化する。
    auto latch = std::find(mcpCalls.begin(), mcpCalls.end(), "register=20 value=0");
    auto direction = std::find(mcpCalls.begin(), mcpCalls.end(), "register=0 value=124");
    assert(latch != mcpCalls.end() && direction != mcpCalls.end() && latch < direction);
    assert(mcpLevels[0] == LOW && mcpLevels[1] == LOW);
    assert(levels[23] == HIGH && levels[16] == HIGH);  // 旧LEDピンは操作しない。

    const unsigned writes = mcpWriteCount;
    loop();
    loop();
    assert(mcpWriteCount == writes);  // 同じ状態を毎loop送信しない。
    assert(led_outputs::set(true, false));
    assert(mcpLevels[0] == HIGH && mcpLevels[1] == LOW);
    assert(led_outputs::set(false, true));
    assert(mcpLevels[0] == LOW && mcpLevels[1] == HIGH);

    mcpWriteResult = false;
    assert(!led_outputs::set(true, true));
    mcpWriteResult = true;
    // 失敗前と同じ要求でも、途中まで書けた可能性があるので再送が必要。
    assert(led_outputs::set(false, true));
    assert(mcpLevels[0] == LOW && mcpLevels[1] == HIGH);
    assert(led_outputs::set(false, false));

    mcpWriteResult = false;
    setInputLevel(32, LOW);
    loop();
    assert(Serial.output == "PRESS child=1 rank=1 elapsed_ms=0\nERROR led_write_failed\n");
    Serial.output.clear();
    const unsigned beforeRetry = mcpWriteCount;
    loop();
    assert(mcpWriteCount == beforeRetry + 1 && Serial.output.empty());
    mcpWriteResult = true;
    loop();
    assert(!ledWriteError && mcpLevels[0] == HIGH && mcpLevels[1] == LOW);
    const unsigned afterRecovery = mcpWriteCount;
    loop();
    assert(mcpWriteCount == afterRecovery && Serial.output.empty());
    // MCP23017に給電したままESP32だけ再起動するケース。
    // 以前の出力とBANK設定が残っていても消灯・再設定できる。
    for (uint8_t config : {0x00, 0xA0}) {
        assert(led_outputs::set(true, true));
        mcpRegisters[0x0A] = config;
        assert(led_outputs::begin());
        assert(mcpRegisters[0x0A] == 0);
        assert(mcpRegisters[0x00] == 0x7C && mcpRegisters[0x01] == 0x7F);
        assert(mcpRegisters[0x14] == 0 && mcpRegisters[0x15] == 0);
        assert(mcpLevels[0] == LOW && mcpLevels[1] == LOW);
        const unsigned afterBegin = mcpWriteCount;
        assert(led_outputs::set(false, false) && mcpWriteCount == afterBegin);
    }
    std::cout << "LED output tests passed\n";
}
