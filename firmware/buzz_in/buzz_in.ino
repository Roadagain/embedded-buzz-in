#include <Arduino.h>

constexpr uint32_t DEBOUNCE_MS = 20;
constexpr uint32_t BLINK_HALF_PERIOD_MS = 250;

class Button {
public:
    explicit Button(uint8_t pin) : pin_(pin) {}

    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        rawPressed_ = pressed_ = (digitalRead(pin_) == LOW);
    }

    bool update(uint32_t now) {
        const bool rawPressed = digitalRead(pin_) == LOW;
        if (rawPressed != rawPressed_) {
            rawPressed_ = rawPressed;
            changedAt_ = now;
        }

        if (rawPressed_ == pressed_ || now - changedAt_ < DEBOUNCE_MS) {
            return false;
        }

        pressed_ = rawPressed_;
        return pressed_;
    }

    bool isPressed() const {
        return pressed_;
    }

private:
    const uint8_t pin_;
    bool rawPressed_ = false;
    bool pressed_ = false;
    uint32_t changedAt_ = 0;
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
    const uint32_t now = millis();
    const bool resetWasPressed = resetButton.isPressed();
    const bool resetPressed = resetButton.update(now);
    const bool onePressed = childOne.update(now);
    const bool twoPressed = childTwo.update(now);

    if (resetPressed) {
        first = Child::None;
        second = Child::None;
        Serial.println("RESET");
    } else if (!resetWasPressed) {
        // 同じ読み取り周期で両方の押下が確定した場合は、子機1を1着とする。
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
