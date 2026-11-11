#include <Arduino.h>

#include "buzzer_inputs.h"

using buzzer_inputs::Child;

constexpr uint32_t BLINK_HALF_PERIOD_MS = 250;

constexpr uint8_t LED_ONE = 23;
constexpr uint8_t LED_TWO = 16;

uint8_t reportedCount = 0;
uint32_t firstPressedAt = 0;

void setup() {
    buzzer_inputs::begin();

    pinMode(LED_ONE, OUTPUT);
    pinMode(LED_TWO, OUTPUT);
    digitalWrite(LED_ONE, LOW);
    digitalWrite(LED_TWO, LOW);

    Serial.begin(115200);
    Serial.println("BOOT");
}

void loop() {
    const auto input = buzzer_inputs::poll();
    const auto& ranking = input.ranking;
    const uint32_t now = millis();
    if (input.resetPressed) {
        reportedCount = 0;
        Serial.println("RESET");
    }
    if (reportedCount == 0 && ranking.count != 0) {
        firstPressedAt = now;
    }
    for (; reportedCount < ranking.count; ++reportedCount) {
        Serial.printf("PRESS child=%u rank=%u\n",
                      static_cast<unsigned>(ranking.order[reportedCount].child),
                      static_cast<unsigned>(reportedCount) + 1);
    }

    const Child first = ranking.order[0].child;
    const Child second = ranking.order[1].child;

    const bool blinkOn = ((now - firstPressedAt) / BLINK_HALF_PERIOD_MS) % 2 == 0;
    digitalWrite(LED_ONE, (first == Child::One && blinkOn) || second == Child::One);
    digitalWrite(LED_TWO, (first == Child::Two && blinkOn) || second == Child::Two);
}
