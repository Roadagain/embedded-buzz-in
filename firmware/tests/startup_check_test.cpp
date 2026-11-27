#include <cassert>
#include <iostream>
#include <vector>

#include "Adafruit_I2CDevice.h"
#include "../firmware.ino"

std::vector<uint8_t> expectedLeds;
unsigned checkedLeds = 0;
std::string startupOutput;

void checkDelay(uint32_t duration) {
    assert(duration == 200);
    assert(testNow == checkedLeds * 200);
    assert(checkedLeds < expectedLeds.size());
    assert(mcpLevels[0] == (expectedLeds[checkedLeds] == 0 ? HIGH : LOW));
    assert(mcpLevels[1] == (expectedLeds[checkedLeds] == 1 ? HIGH : LOW));
    // チェック中は入力を着順やリセットとして受け付けない。
    assert(!interrupts[32] && !interrupts[33] && !interrupts[27]);
    assert(Serial.output == startupOutput);
    ++checkedLeds;
}

void checkStartup(uint8_t inputs, bool initOk = true, bool readOk = true) {
    testNow = 0;
    inputsReady = false;
    levels.fill(HIGH);
    for (auto& callback : interrupts) callback = nullptr;
    Serial.output.clear();
    mcpCalls.clear();
    checkedLeds = 0;
    expectedLeds.clear();
    continuityInputs = inputs;
    continuityBeginResult = initOk;
    continuityReadResult = readOk;
    startupOutput = "BOOT\n";
    if (!initOk || !readOk) {
        startupOutput += initOk ? "ERROR continuity_read_failed\n" : "ERROR continuity_init_failed\n";
        startupOutput += "CONTINUITY child=1 connected=unknown\nCONTINUITY child=2 connected=unknown\n";
    } else {
        for (uint8_t child = 0; child < 2; ++child) {
            const bool connected = !(inputs & (1U << child));
            if (connected) expectedLeds.push_back(child);
            startupOutput += "CONTINUITY child=" + std::to_string(child + 1) +
                             " connected=" + (connected ? "1\n" : "0\n");
        }
    }
    onTestDelay = checkDelay;
    setup();
    onTestDelay = nullptr;
    assert(checkedLeds == expectedLeds.size() && testNow == checkedLeds * 200);
    assert(mcpLevels[0] == LOW && mcpLevels[1] == LOW);
    assert(interrupts[32] && interrupts[33] && interrupts[27]);
    assert(inputsReady);
    assert(Serial.output == startupOutput + "MODE mode=endless_chance\n");
    if (expectedLeds.empty()) {
        // 初期化時の全消灯以外に出力を書き込まない。
        for (const auto& call : mcpCalls) {
            assert(call != "register=20 value=1" && call != "register=20 value=2" && call != "register=20 value=3");
        }
    }
    assert(!continuity_inputs::isConnected(2) && !continuity_inputs::isConnected(255));
    Serial.output.clear();
    loop();
    assert(Serial.output.empty());
    assert(buzzer_inputs::poll().ranking.count == 0);
}

int main() {
    checkStartup(0xFF);  // 未接続：点灯も待機もなし。
    checkStartup(0xFE);  // 子機1のみ。
    checkStartup(0xFD);  // 子機2のみ。
    checkStartup(0xFC, false);  // 初期化失敗：状態不明は点灯しない。
    checkStartup(0xFC, true, false);  // 読み取り失敗も点灯しない。
    checkStartup(0xFC);  // 両方：1→2の順に計400 ms点灯。

    testNow += 75;
    setInputLevel(33, LOW);
    loop();
    assert(Serial.output == "PRESS child=2 rank=1 elapsed_ms=75\n");
    assert(mcpLevels[0] == LOW && mcpLevels[1] == HIGH);
    std::cout << "Startup check integration tests passed\n";
}
