#include <Arduino.h>

#include "buzzer_inputs.h"
#include "serial_commands.h"

using buzzer_inputs::Child;
using buzzer_inputs::Command;
using buzzer_inputs::Mode;

constexpr uint32_t BLINK_HALF_PERIOD_MS = 250;
constexpr uint32_t WRONG_TRANSITION_MS = 1000;

constexpr uint8_t LED_ONE = 23;
constexpr uint8_t LED_TWO = 16;

uint8_t reportedCount = 0;
uint32_t firstPressedAt = 0;
bool wrongTransition = false;
uint32_t wrongAt = 0;
Child promotedChild = Child::None;

void reportMode(Mode mode) {
    Serial.printf("MODE mode=%s\n", mode == Mode::EndlessChance ? "endless_chance" : "first_only");
}

void setup() {
    buzzer_inputs::begin();

    pinMode(LED_ONE, OUTPUT);
    pinMode(LED_TWO, OUTPUT);
    digitalWrite(LED_ONE, LOW);
    digitalWrite(LED_TWO, LOW);

    Serial.begin(115200);
    Serial.println("BOOT");
    reportMode(Mode::EndlessChance);
}

void loop() {
    const Command command = serial_commands::poll();
    const auto input = buzzer_inputs::poll(command);
    const auto& ranking = input.ranking;
    const uint32_t now = millis();
    if (input.modeChanged) {
        reportMode(input.mode);
    }
    if (input.correctChild != Child::None) {
        Serial.printf("JUDGE result=correct child=%u\n", static_cast<unsigned>(input.correctChild));
    } else if (input.wrongChild != Child::None) {
        promotedChild = ranking.count > 0 ? ranking.order[0].child : Child::None;
        Serial.printf("JUDGE result=wrong child=%u next_child=%u\n",
                      static_cast<unsigned>(input.wrongChild),
                      static_cast<unsigned>(promotedChild));
        // 先頭の削除分だけ報告済み件数を減らし、繰り上げを新規押下として出力しない。
        if (reportedCount > 0) {
            --reportedCount;
        }
        wrongTransition = true;
        wrongAt = now;
    } else if ((command == Command::Correct || command == Command::Wrong) && !input.resetPressed) {
        Serial.println("ERROR no_answerer");
    }
    if (input.resetPressed) {
        reportedCount = 0;
        wrongTransition = false;
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

    // 元の2着は点灯を維持し、1秒後に点滅開始・後続の点灯を行う。
    if (wrongTransition && now - wrongAt >= WRONG_TRANSITION_MS) {
        wrongTransition = false;
        firstPressedAt = wrongAt + WRONG_TRANSITION_MS;
    }
    const bool blinkOn = ((now - firstPressedAt) / BLINK_HALF_PERIOD_MS) % 2 == 0;
    digitalWrite(LED_ONE, wrongTransition ? promotedChild == Child::One
                                          : (first == Child::One && blinkOn) || second == Child::One);
    digitalWrite(LED_TWO, wrongTransition ? promotedChild == Child::Two
                                          : (first == Child::Two && blinkOn) || second == Child::Two);
}
