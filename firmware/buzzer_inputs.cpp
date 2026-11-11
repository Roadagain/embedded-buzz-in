#include "buzzer_inputs.h"

#include <Arduino.h>

namespace buzzer_inputs {
namespace {

portMUX_TYPE buttonMux = portMUX_INITIALIZER_UNLOCKED;

Ranking ranking;
uint32_t resetAtMs = 0;

class Button {
public:
    Button(uint8_t pin, Child child) : pin_(pin), child_(child) {}

    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        attachInterruptArg(pin_, onFalling, this, FALLING);
    }

private:
    static void ARDUINO_ISR_ATTR onFalling(void* context) {
        auto& button = *static_cast<Button*>(context);
        portENTER_CRITICAL_ISR(&buttonMux);
        // 空きがある間は、先頭の子機と異なる押下だけを追加する。
        if (ranking.count < CHILD_COUNT && ranking.order[0].child != button.child_) {
            auto& press = ranking.order[ranking.count++];
            press.child = button.child_;
            press.elapsedMs = static_cast<uint32_t>(millis()) - resetAtMs;
        }
        portEXIT_CRITICAL_ISR(&buttonMux);
    }

    const uint8_t pin_;
    const Child child_;
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

Button childOne(22, Child::One);
Button childTwo(18, Child::Two);
ResetButton resetButton(15);

}  // 無名名前空間

void begin() {
    // 起動直後は入力受付開始を経過時間の起点にする。
    resetAtMs = millis();
    childOne.begin();
    childTwo.begin();
    resetButton.begin();
}

Snapshot poll() {
    // リセットの判定と着順の読み取りを、子機の割り込みと競合しないように行う。
    portENTER_CRITICAL(&buttonMux);
    const bool resetPressed = resetButton.takePress();
    if (resetPressed) {
        ranking = Ranking{};
        resetAtMs = millis();
    }
    const Ranking snapshot = ranking;
    portEXIT_CRITICAL(&buttonMux);
    return {snapshot, resetPressed};
}

}  // 名前空間 buzzer_inputs
