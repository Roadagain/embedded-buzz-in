#include <Arduino.h>

constexpr uint32_t BLINK_HALF_PERIOD_MS = 250;

portMUX_TYPE buttonMux = portMUX_INITIALIZER_UNLOCKED;

enum class Child : uint8_t { None = 0, One = 1, Two = 2 };

constexpr uint8_t CHILD_COUNT = 2;

struct Ranking {
    Child order[CHILD_COUNT]{};
    uint8_t count = 0;
};

Ranking ranking;

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
        if (ranking.count < CHILD_COUNT && ranking.order[0] != button.child_) {
            ranking.order[ranking.count++] = button.child_;
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

constexpr uint8_t LED_ONE = 23;
constexpr uint8_t LED_TWO = 16;

uint8_t reportedCount = 0;
uint32_t firstPressedAt = 0;

void setup() {
    childOne.begin();
    childTwo.begin();
    resetButton.begin();

    pinMode(LED_ONE, OUTPUT);
    pinMode(LED_TWO, OUTPUT);
    digitalWrite(LED_ONE, LOW);
    digitalWrite(LED_TWO, LOW);

    Serial.begin(115200);
    Serial.println("BOOT");
}

void loop() {
    // リセットの判定と着順の読み取りを、子機の割り込みと競合しないように行う。
    portENTER_CRITICAL(&buttonMux);
    const bool resetPressed = resetButton.takePress();
    if (resetPressed) {
        ranking = {};
    }
    const Ranking snapshot = ranking;
    portEXIT_CRITICAL(&buttonMux);

    const uint32_t now = millis();
    if (resetPressed) {
        reportedCount = 0;
        Serial.println("RESET");
    }
    if (reportedCount == 0 && snapshot.count != 0) {
        firstPressedAt = now;
    }
    for (; reportedCount < snapshot.count; ++reportedCount) {
        Serial.printf("PRESS child=%u rank=%u\n",
                      static_cast<unsigned>(snapshot.order[reportedCount]),
                      static_cast<unsigned>(reportedCount) + 1);
    }

    const Child first = snapshot.order[0];
    const Child second = snapshot.order[1];

    const bool blinkOn = ((now - firstPressedAt) / BLINK_HALF_PERIOD_MS) % 2 == 0;
    digitalWrite(LED_ONE, (first == Child::One && blinkOn) || second == Child::One);
    digitalWrite(LED_TWO, (first == Child::Two && blinkOn) || second == Child::Two);
}
