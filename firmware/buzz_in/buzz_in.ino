#include <Arduino.h>

constexpr uint32_t BLINK_HALF_PERIOD_MS = 250;

portMUX_TYPE buttonMux = portMUX_INITIALIZER_UNLOCKED;

class Button {
public:
    explicit Button(uint8_t pin) : pin_(pin) {}

    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        attachInterruptArg(pin_, onFalling, this, FALLING);
    }

    bool takePress() {
        const bool pressed = pendingPress_;
        pendingPress_ = false;
        return pressed;
    }

private:
    static void ARDUINO_ISR_ATTR onFalling(void* context) {
        auto& button = *static_cast<Button*>(context);
        portENTER_CRITICAL_ISR(&buttonMux);
        button.pendingPress_ = true;
        portEXIT_CRITICAL_ISR(&buttonMux);
    }

    const uint8_t pin_;
    bool pendingPress_ = false;
};

Button childOne(16);
Button childTwo(18);
Button resetButton(15);

constexpr uint8_t LED_ONE = 22;
constexpr uint8_t LED_TWO = 23;

enum class Child { None = 0, One = 1, Two = 2 };

Child first = Child::None;
Child second = Child::None;
uint32_t firstPressedAt = 0;

void recordPress(Child child, uint32_t now) {
    unsigned rank;
    if (first == Child::None) {
        first = child;
        firstPressedAt = now;
        rank = 1;
    } else if (first != child && second == Child::None) {
        second = child;
        rank = 2;
    } else {
        return;
    }

    Serial.printf("PRESS child=%u rank=%u\n", static_cast<unsigned>(child), rank);
}

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
    // 割り込みと競合しないように、3ボタンの入力をまとめて取り出す。
    portENTER_CRITICAL(&buttonMux);
    const bool resetPressed = resetButton.takePress();
    const bool onePressed = childOne.takePress();
    const bool twoPressed = childTwo.takePress();
    portEXIT_CRITICAL(&buttonMux);

    const uint32_t now = millis();
    if (resetPressed) {
        first = Child::None;
        second = Child::None;
        Serial.println("RESET");
    } else {
        // 同じループで両方の押下を受け取った場合は、子機1を先に扱う。
        if (onePressed) {
            recordPress(Child::One, now);
        }
        if (twoPressed) {
            recordPress(Child::Two, now);
        }
    }

    const bool blinkOn = ((now - firstPressedAt) / BLINK_HALF_PERIOD_MS) % 2 == 0;
    digitalWrite(LED_ONE, (first == Child::One && blinkOn) || second == Child::One);
    digitalWrite(LED_TWO, (first == Child::Two && blinkOn) || second == Child::Two);
}
