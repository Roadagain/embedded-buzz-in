#include <cassert>
#include <iostream>

#include "Arduino.h"
#include "../buzzer_inputs.h"

using namespace buzzer_inputs;

void fresh(uint32_t now = 100) {
    testNow = now;
    levels[22] = HIGH;
    levels[18] = HIGH;
    levels[15] = HIGH;
    begin();
    if (poll().mode != Mode::EndlessChance) poll(Command::ToggleMode);
    poll(Command::Reset);
    assert(interruptModes[22] == CHANGE && interruptModes[18] == CHANGE);
    assert(interruptModes[15] == FALLING);
}

void press(uint8_t pin) { setInputLevel(pin, LOW); }
void release(uint8_t pin) { setInputLevel(pin, HIGH); }
void expectCount(uint8_t count) { assert(poll().ranking.count == count); }

int main() {
    fresh();
    press(22);
    auto result = poll();
    assert(result.ranking.count == 1 && result.ranking.order[0].elapsedMs == 0);
    poll(Command::Wrong);
    // 押しっぱなしで不正解になっても、追加割り込みを受け付けない。
    interrupts[22](contexts[22]);
    expectCount(0);
    press(18);  // 子機1の解放待ちでも子機2は即時に受け付ける。
    expectCount(1);
    release(22);
    testNow += 19;
    press(22);
    expectCount(1);
    release(22);
    testNow += 20;
    press(22);  // loop/pollを挟まなくても20 msの解放を認識する。
    result = poll();
    assert(result.ranking.count == 2);
    assert(result.ranking.order[1].child == Child::One);
    assert(result.ranking.order[1].elapsedMs == 39);

    // 判定前に解放済みなら、不正解直後も余分な待ち時間なしで押し直せる。
    fresh();
    press(22);
    release(22);
    testNow += 20;
    poll(Command::Wrong);
    press(22);
    expectCount(1);

    // リセットは進行中の解放確認の時刻も変更しない。
    fresh();
    press(22);
    release(22);
    testNow += 19;
    poll(Command::Reset);
    ++testNow;
    press(22);
    result = poll();
    assert(result.ranking.count == 1 && result.ranking.order[0].elapsedMs == 1);

    // 短いLOWを挟むたびに解放確認の開始時刻が更新される。
    fresh();
    press(22);
    poll(Command::Wrong);
    release(22);
    testNow += 15;
    press(22);
    release(22);
    testNow += 5;  // 最初の解放から20 msでも、連続HIGHは5 msだけ。
    press(22);
    expectCount(0);
    release(22);
    testNow += 20;
    press(22);
    expectCount(1);

    // 解放済みでも回答待ちの間は追加登録しない。
    // 無視された押下も、次回は改めて解放を必要とする。
    fresh();
    press(22);
    release(22);
    testNow += 20;
    press(22);
    expectCount(1);
    poll(Command::Wrong);
    interrupts[22](contexts[22]);
    expectCount(0);
    release(22);
    testNow += 20;
    press(22);
    expectCount(1);

    // 全リセット・正解・モード切替・1着のみの不正解でもロックを維持する。
    for (const auto command : {Command::Reset, Command::Correct,
                               Command::ToggleMode, Command::Wrong}) {
        fresh();
        if (command == Command::Wrong) poll(Command::ToggleMode);
        press(22);
        assert(poll(command).resetNeeded);
        interrupts[22](contexts[22]);
        expectCount(0);
        release(22);
        testNow += 20;
        press(22);
        result = poll();
        assert(result.ranking.count == 1 && result.ranking.order[0].elapsedMs == 20);
    }

    // 物理リセットでも押しっぱなしの子機を再登録しない。
    fresh();
    press(22);
    press(15);
    testNow += 20;
    assert(poll().resetNeeded);
    interrupts[22](contexts[22]);
    expectCount(0);

    // 起動時から押されていた子機も、一度安定して離すまで受付禁止。
    fresh();
    levels[22] = LOW;
    begin();
    interrupts[22](contexts[22]);
    expectCount(0);
    release(22);
    testNow += 20;
    press(22);
    expectCount(1);

    // millis()の周回をまたいでも解放時間を判定できる。
    fresh(UINT32_MAX - 10);
    press(22);
    poll(Command::Wrong);
    release(22);
    testNow += 20;
    press(22);
    result = poll();
    assert(result.ranking.count == 1 && result.ranking.order[0].elapsedMs == 20);
    std::cout << "Button release tests passed\n";
}
