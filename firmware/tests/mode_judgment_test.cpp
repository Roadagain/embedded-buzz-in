#include <cassert>
#include <iostream>

#include "../firmware.ino"

void expect(const std::string& output) {
    if (Serial.output != output) {
        std::cerr << "Expected:\n" << output << "Actual:\n" << Serial.output;
    }
    assert(Serial.output == output);
    Serial.output.clear();
}
void press(uint8_t pin) {
    setInputLevel(pin, LOW);
}
void release(uint8_t pin) { setInputLevel(pin, HIGH); }
void tick(uint32_t now) { testNow = now; loop(); }
void command(char value) { Serial.input.push_back(value); loop(); }
void expectEmpty(Mode mode) {
    const auto input = buzzer_inputs::poll();
    assert(input.ranking.count == 0 && input.mode == mode);
    assert(levels[23] == LOW && levels[16] == LOW);
    assert(!wrongTransition);
}

int main() {
    setup();
    const uint32_t startedAt = testNow;
    expect("BOOT\nMODE mode=endless_chance\n");
    // エンドレスチャンスの不正解では繰り上げ、経過時間の起点を維持する。
    testNow = startedAt + 100;
    press(22);
    press(18);
    loop();
    expect("PRESS child=1 rank=1 elapsed_ms=100\nPRESS child=2 rank=2 elapsed_ms=100\n");
    assert(levels[23] == HIGH && levels[16] == HIGH);
    release(22);
    release(18);
    tick(startedAt + 350);
    assert(levels[23] == LOW && levels[16] == HIGH);
    command('x');
    expect("JUDGE result=wrong child=1 next_child=2\n");
    assert(levels[23] == LOW && levels[16] == HIGH);
    testNow = startedAt + 400;
    press(22);
    loop();
    expect("PRESS child=1 rank=2 elapsed_ms=400\n");
    release(22);
    assert(levels[23] == LOW && levels[16] == HIGH);
    tick(startedAt + 1350);
    assert(levels[23] == HIGH && levels[16] == HIGH);
    tick(startedAt + 1600);
    assert(levels[23] == HIGH && levels[16] == LOW);
    command('o');
    expect("JUDGE result=correct child=2\nRESET\n");
    expectEmpty(Mode::EndlessChance);

    command('m');
    expect("MODE mode=first_only\nRESET\n");
    // どちらの子機が先頭でも、後続を消灯し、不正解で全リセットする。
    for (const bool reverse : {false, true}) {
        const uint8_t firstPin = reverse ? 18 : 22;
        const uint8_t secondPin = reverse ? 22 : 18;
        const uint8_t firstLed = reverse ? 16 : 23;
        const uint8_t secondLed = reverse ? 23 : 16;
        const std::string first = reverse ? "2" : "1";
        const std::string second = reverse ? "1" : "2";
        testNow += 100;
        press(firstPin);
        press(secondPin);
        loop();
        expect("PRESS child=" + first + " rank=1 elapsed_ms=100\nPRESS child=" + second +
               " rank=2 elapsed_ms=100\n");
        assert(levels[firstLed] == HIGH && levels[secondLed] == LOW);
        release(firstPin);
        release(secondPin);
        tick(testNow + 250);
        assert(levels[firstLed] == LOW && levels[secondLed] == LOW);
        command('x');
        expect("JUDGE result=wrong child=" + first + " next_child=0\nRESET\n");
        expectEmpty(Mode::FirstOnly);
        command('x');
        command('o');
        expect("ERROR no_answerer\nERROR no_answerer\n");
        testNow += 50;
        press(secondPin);
        loop();
        expect("PRESS child=" + second + " rank=1 elapsed_ms=50\n");
        release(secondPin);
        assert(levels[secondLed] == HIGH && levels[firstLed] == LOW);
        command('o');
        expect("JUDGE result=correct child=" + second + "\nRESET\n");
        expectEmpty(Mode::FirstOnly);
    }
    // 先頭だけの不正解も全リセットし、経過時間の起点を更新する。
    testNow += 100;
    press(22);
    loop();
    expect("PRESS child=1 rank=1 elapsed_ms=100\n");
    release(22);
    command('x');
    expect("JUDGE result=wrong child=1 next_child=0\nRESET\n");
    expectEmpty(Mode::FirstOnly);
    testNow += 25;
    press(18);
    loop();
    expect("PRESS child=2 rank=1 elapsed_ms=25\n");
    command('m');
    expect("MODE mode=endless_chance\nRESET\n");
    expectEmpty(Mode::EndlessChance);
    std::cout << "Mode judgment integration tests passed\n";
}
