#include <cassert>
#include <iostream>

#include "../firmware.ino"

unsigned checkedLeds = 0;

void checkDelay(uint32_t duration) {
    assert(duration == 200);
    assert(testNow == checkedLeds * 200);
    assert(levels[23] == (checkedLeds == 0 ? HIGH : LOW));
    assert(levels[16] == (checkedLeds == 1 ? HIGH : LOW));
    // チェック中は入力を着順やリセットとして受け付けない。
    assert(interrupts[22] == nullptr && interrupts[18] == nullptr);
    assert(interrupts[15] == nullptr);
    assert(Serial.output == "BOOT\n");
    ++checkedLeds;
}

int main() {
    onTestDelay = checkDelay;
    setup();
    assert(checkedLeds == 2 && testNow == 400);
    assert(levels[23] == LOW && levels[16] == LOW);
    assert(interrupts[22] && interrupts[18] && interrupts[15]);
    assert(Serial.output == "BOOT\nMODE mode=endless_chance\n");
    Serial.output.clear();
    loop();
    assert(Serial.output.empty());
    assert(buzzer_inputs::poll().ranking.count == 0);

    testNow += 75;
    levels[18] = LOW;
    interrupts[18](contexts[18]);
    loop();
    assert(Serial.output == "PRESS child=2 rank=1 elapsed_ms=75\n");
    assert(levels[23] == LOW && levels[16] == HIGH);
    std::cout << "Startup check integration tests passed\n";
}
