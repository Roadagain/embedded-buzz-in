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
    levels[pin] = LOW;
    interrupts[pin](contexts[pin]);
}
void tick(uint32_t now) { testNow = now; loop(); }
void command(char value) { Serial.input.push_back(value); loop(); }

int main() {
    setup();
    expect("BOOT\nMODE mode=endless_chance\n");
    testNow = 100;
    press(22);
    press(18);
    loop();
    expect("PRESS child=1 rank=1 elapsed_ms=100\nPRESS child=2 rank=2 elapsed_ms=100\n");
    tick(200);
    command('m');  // 改行なしで即実行し、LED・着順を消去する。
    expect("MODE mode=first_only\nRESET\n");
    assert(levels[23] == LOW && levels[16] == LOW);
    const auto firstOnly = buzzer_inputs::poll();
    assert(firstOnly.mode == Mode::FirstOnly && firstOnly.ranking.count == 0);
    assert(!firstOnly.modeChanged && !firstOnly.resetPressed);
    command('o');
    command('x');
    expect("ERROR no_answerer\nERROR no_answerer\n");

    testNow = 250;
    press(18);
    loop();
    expect("PRESS child=2 rank=1 elapsed_ms=50\n");
    command('r');
    expect("RESET\n");
    assert(buzzer_inputs::poll().mode == Mode::FirstOnly);
    press(22);
    loop();
    expect("PRESS child=1 rank=1 elapsed_ms=0\n");
    command('o');
    expect("JUDGE result=correct child=1\nRESET\n");
    assert(buzzer_inputs::poll().mode == Mode::FirstOnly);

    testNow = 300;
    command('m');
    expect("MODE mode=endless_chance\nRESET\n");
    assert(buzzer_inputs::poll().mode == Mode::EndlessChance);
    testNow = 375;
    press(22);
    press(18);
    loop();
    expect("PRESS child=1 rank=1 elapsed_ms=75\nPRESS child=2 rank=2 elapsed_ms=75\n");
    command('x');
    expect("JUDGE result=wrong child=1 next_child=2\n");
    assert(wrongTransition);
    tick(400);
    command('m');  // 繰り上がり待機も解除する。
    expect("MODE mode=first_only\nRESET\n");
    assert(!wrongTransition && levels[23] == LOW && levels[16] == LOW);
    tick(1500);
    assert(levels[23] == LOW && levels[16] == LOW);

    // 連続コマンドは1文字ずつ順番に処理し、CR・LFは無視する。
    Serial.input = {'m', 'm', '\r', '\n'};
    loop();
    expect("MODE mode=endless_chance\nRESET\n");
    loop();
    expect("MODE mode=first_only\nRESET\n");
    loop();
    loop();
    expect("");
    command('M');
    expect("ERROR unknown_command\n");
    assert(buzzer_inputs::poll().mode == Mode::FirstOnly);

    // 物理リセットと同時のmも切替・リセットを1回行う。
    press(15);
    testNow = 1520;
    command('m');
    expect("MODE mode=endless_chance\nRESET\n");
    assert(buzzer_inputs::poll().mode == Mode::EndlessChance);
    std::cout << "Mode switch integration tests passed\n";
}
