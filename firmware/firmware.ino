#include <Arduino.h>
#include <string.h>

#include "buzzer_inputs.h"

using buzzer_inputs::Child;

constexpr uint32_t BLINK_HALF_PERIOD_MS = 250;

constexpr uint8_t LED_ONE = 23;
constexpr uint8_t LED_TWO = 16;

uint8_t reportedCount = 0;
uint32_t firstPressedAt = 0;

bool takeSerialReset() {
    static char command[32];
    static size_t length = 0;
    static bool overflow = false;

    // 1ループの受信量を制限し、入力途中でもLEDと早押しを処理する。
    for (size_t readCount = 0; readCount < sizeof(command) && Serial.available() > 0;
         ++readCount) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r' || ch == '\n') {
            if (length == 0 && !overflow) {
                continue;
            }
            command[length] = '\0';
            const bool reset = !overflow && length == 5 && strcmp(command, "reset") == 0;
            if (!reset) {
                Serial.println(overflow ? "ERROR command_too_long" : "ERROR unknown_command");
            }
            length = 0;
            overflow = false;
            return reset;
        }
        if (length < sizeof(command) - 1) {
            command[length++] = ch;
        } else {
            overflow = true;
        }
    }
    return false;
}

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
    const bool resetRequested = takeSerialReset();
    const auto input = buzzer_inputs::poll(resetRequested);
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
        Serial.printf("PRESS child=%u rank=%u elapsed_ms=%lu\n",
                      static_cast<unsigned>(ranking.order[reportedCount].child),
                      static_cast<unsigned>(reportedCount) + 1,
                      static_cast<unsigned long>(ranking.order[reportedCount].elapsedMs));
    }

    const Child first = ranking.order[0].child;
    const Child second = ranking.order[1].child;

    const bool blinkOn = ((now - firstPressedAt) / BLINK_HALF_PERIOD_MS) % 2 == 0;
    digitalWrite(LED_ONE, (first == Child::One && blinkOn) || second == Child::One);
    digitalWrite(LED_TWO, (first == Child::Two && blinkOn) || second == Child::Two);
}
