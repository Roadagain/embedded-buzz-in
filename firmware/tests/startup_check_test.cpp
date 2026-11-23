#include <cassert>
#include <iostream>

#include "Adafruit_AW9523.h"
#include "../firmware.ino"

unsigned checkedLeds = 0;

void checkDelay(uint32_t duration) {
    assert(duration == 200);
    assert(testNow == checkedLeds * 200);
    assert(awLevels[0] == (checkedLeds == 0 ? HIGH : LOW));
    assert(awLevels[1] == (checkedLeds == 1 ? HIGH : LOW));
    // チェック中は入力を着順やリセットとして受け付けない。
    assert(interrupts[32] == nullptr && interrupts[33] == nullptr);
    assert(interrupts[27] == nullptr);
    assert(Serial.output == "BOOT\n");
    ++checkedLeds;
}

int main() {
    onTestDelay = checkDelay;
    setup();
    assert(checkedLeds == 2 && testNow == 400);
    assert(awLevels[0] == LOW && awLevels[1] == LOW);
    assert(interrupts[32] && interrupts[33] && interrupts[27]);
    assert(Serial.output == "BOOT\nMODE mode=endless_chance\n");
    Serial.output.clear();
    loop();
    assert(Serial.output.empty());
    assert(buzzer_inputs::poll().ranking.count == 0);

    testNow += 75;
    setInputLevel(33, LOW);
    loop();
    assert(Serial.output == "PRESS child=2 rank=1 elapsed_ms=75\n");
    assert(awLevels[0] == LOW && awLevels[1] == HIGH);
    std::cout << "Startup check integration tests passed\n";
}
