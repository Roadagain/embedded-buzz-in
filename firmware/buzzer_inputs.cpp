#include "buzzer_inputs.h"

#include <Arduino.h>

namespace buzzer_inputs {
namespace {

portMUX_TYPE buttonMux = portMUX_INITIALIZER_UNLOCKED;

struct WaitingQueue {
    Press entries[CHILD_COUNT]{};
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;
};

WaitingQueue waiting;
uint32_t resetAtMs = 0;
Mode mode = Mode::EndlessChance;

class Button {
public:
    Button(uint8_t pin, Child child) : pin_(pin), child_(child) {}

    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        ready_ = digitalRead(pin_) == HIGH;
        released_ = false;
        attachInterruptArg(pin_, onChange, this, CHANGE);
    }

private:
    static constexpr uint32_t RELEASE_STABLE_MS = 20;

    static void ARDUINO_ISR_ATTR onChange(void* context) {
        auto& button = *static_cast<Button*>(context);
        portENTER_CRITICAL_ISR(&buttonMux);
        const uint32_t now = millis();
        if (digitalRead(button.pin_) == HIGH) {
            button.released_ = true;
            button.releasedAtMs_ = now;
            portEXIT_CRITICAL_ISR(&buttonMux);
            return;
        }
        // 次の立ち下がりで解放時間を確認する。loop()の実行間隔に依存させない。
        const bool ready = button.ready_ ||
                           (button.released_ &&
                            static_cast<uint32_t>(now - button.releasedAtMs_) >= RELEASE_STABLE_MS);
        button.ready_ = false;
        button.released_ = false;
        if (!ready) {
            portEXIT_CRITICAL_ISR(&buttonMux);
            return;
        }
        // 回答待ちにいる子機の連打は無視し、不正解で外れた子機は再参加できる。
        bool alreadyWaiting = false;
        for (uint8_t i = 0; i < waiting.count; ++i) {
            const uint8_t index = (waiting.head + i) % CHILD_COUNT;
            if (waiting.entries[index].child == button.child_) {
                alreadyWaiting = true;
                break;
            }
        }
        if (waiting.count < CHILD_COUNT && !alreadyWaiting) {
            auto& press = waiting.entries[waiting.tail];
            press.child = button.child_;
            press.elapsedMs = now - resetAtMs;
            waiting.tail = (waiting.tail + 1) % CHILD_COUNT;
            ++waiting.count;
        }
        portEXIT_CRITICAL_ISR(&buttonMux);
    }

    const uint8_t pin_;
    const Child child_;
    // 着順の消去・不正解では初期化せず、物理的な解放確認を維持する。
    bool ready_ = false;
    bool released_ = false;
    uint32_t releasedAtMs_ = 0;
};

class ResetButton {
public:
    explicit ResetButton(uint8_t pin) : pin_(pin) {}

    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        attachInterruptArg(pin_, onFalling, this, FALLING);
    }

    bool takePress() {
        if (!pendingPress_ || static_cast<uint32_t>(millis()) - fellAt_ < DEBOUNCE_MS) {
            return false;
        }

        pendingPress_ = false;
        return digitalRead(pin_) == LOW;
    }

private:
    static constexpr uint32_t DEBOUNCE_MS = 20;

    static void ARDUINO_ISR_ATTR onFalling(void* context) {
        auto& button = *static_cast<ResetButton*>(context);
        portENTER_CRITICAL_ISR(&buttonMux);
        button.pendingPress_ = true;
        button.fellAt_ = millis();
        portEXIT_CRITICAL_ISR(&buttonMux);
    }

    const uint8_t pin_;
    bool pendingPress_ = false;
    uint32_t fellAt_ = 0;
};

// MCP23017のI²C（GPIO25/26）とともに3V3側のヘッダーへまとめる。
Button childOne(32, Child::One);
Button childTwo(33, Child::Two);
ResetButton resetButton(27);

}  // 無名名前空間

void begin() {
    // 起動直後は入力受付開始を経過時間の起点にする。
    resetAtMs = millis();
    childOne.begin();
    childTwo.begin();
    resetButton.begin();
}

Snapshot poll(Command command) {
    // 判定・リセット・着順の読み取りを、子機の割り込みと競合しないように行う。
    portENTER_CRITICAL(&buttonMux);
    const bool buttonPressed = resetButton.takePress();
    const bool modeChanged = command == Command::ToggleMode;
    if (modeChanged) {
        mode = mode == Mode::EndlessChance ? Mode::FirstOnly : Mode::EndlessChance;
    }
    // 物理リセットを優先し、判定対象の確定と着順の更新を同じ排他区間で行う。
    const Child correctChild = !buttonPressed && command == Command::Correct && waiting.count > 0
                                   ? waiting.entries[waiting.head].child
                                   : Child::None;
    const Child wrongChild = !buttonPressed && command == Command::Wrong && waiting.count > 0
                                 ? waiting.entries[waiting.head].child
                                 : Child::None;
    const bool resetNeeded = buttonPressed || modeChanged || command == Command::Reset ||
                              correctChild != Child::None ||
                              (wrongChild != Child::None && mode == Mode::FirstOnly);
    if (resetNeeded) {
        waiting = WaitingQueue{};
        resetAtMs = millis();
    } else if (wrongChild != Child::None) {
        waiting.entries[waiting.head] = Press{};
        waiting.head = (waiting.head + 1) % CHILD_COUNT;
        --waiting.count;
    }
    // 外部にはリングの位置を見せず、先頭から着順に並べたコピーを返す。
    Ranking snapshot{};
    snapshot.count = waiting.count;
    for (uint8_t i = 0; i < waiting.count; ++i) {
        snapshot.order[i] = waiting.entries[(waiting.head + i) % CHILD_COUNT];
    }
    portEXIT_CRITICAL(&buttonMux);
    return {snapshot, resetNeeded, correctChild, wrongChild, mode, modeChanged};
}

}  // 名前空間 buzzer_inputs
