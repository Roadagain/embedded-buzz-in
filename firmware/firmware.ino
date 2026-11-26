#include <Arduino.h>

#include "buzzer_inputs.h"
#include "continuity_inputs.h"
#include "led_outputs.h"
#include "serial_commands.h"

using buzzer_inputs::Child;
using buzzer_inputs::Command;
using buzzer_inputs::Mode;

constexpr uint32_t BLINK_HALF_PERIOD_MS = 250;
constexpr uint32_t WRONG_TRANSITION_MS = 1000;
constexpr uint32_t STARTUP_LED_ON_MS = 200;

bool inputsReady = false;
bool ledWriteError = false;

uint8_t reportedCount = 0;
uint32_t firstPressedAt = 0;
bool wrongTransition = false;
uint32_t wrongAt = 0;
Child promotedChild = Child::None;

void reportMode(Mode mode) {
    Serial.printf("MODE mode=%s\n", mode == Mode::EndlessChance ? "endless_chance" : "first_only");
}

void setup() {
    Serial.begin(115200);
    Serial.println("BOOT");
    if (!led_outputs::begin()) {
        Serial.println("ERROR led_init_failed");
        return;
    }

    // 接続の有無によらず全ポートを順番に点灯する。入力割り込みは終了後に登録する。
    for (uint8_t child = 0; child < 2; ++child) {
        if (!led_outputs::set(child == 0, child == 1)) {
            Serial.println("ERROR led_startup_failed");
            return;
        }
        delay(STARTUP_LED_ON_MS);
        if (!led_outputs::set(false, false)) {
            Serial.println("ERROR led_startup_failed");
            return;
        }
    }

    buzzer_inputs::begin();
    inputsReady = true;
    reportMode(Mode::EndlessChance);
    continuity_inputs::begin();
}

void loop() {
    if (!inputsReady) {
        delay(10);
        return;
    }
    continuity_inputs::poll();
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
    } else if ((command == Command::Correct || command == Command::Wrong) && !input.resetNeeded) {
        Serial.println("ERROR no_answerer");
    }
    if (input.resetNeeded) {
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
    const Child second = input.mode == Mode::EndlessChance ? ranking.order[1].child : Child::None;

    // 元の2着は点灯を維持し、1秒後に点滅開始・後続の点灯を行う。
    if (wrongTransition && now - wrongAt >= WRONG_TRANSITION_MS) {
        wrongTransition = false;
        firstPressedAt = wrongAt + WRONG_TRANSITION_MS;
    }
    const bool blinkOn = ((now - firstPressedAt) / BLINK_HALF_PERIOD_MS) % 2 == 0;
    const bool oneOn = wrongTransition ? promotedChild == Child::One
                                      : (first == Child::One && blinkOn) || second == Child::One;
    const bool twoOn = wrongTransition ? promotedChild == Child::Two
                                      : (first == Child::Two && blinkOn) || second == Child::Two;
    if (!led_outputs::set(oneOn, twoOn)) {
        if (!ledWriteError) Serial.println("ERROR led_write_failed");
        ledWriteError = true;
    } else {
        ledWriteError = false;
    }
}
