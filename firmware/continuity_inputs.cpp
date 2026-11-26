#include "continuity_inputs.h"

#include <Arduino.h>

#include "buzzer_inputs.h"

namespace continuity_inputs {
namespace {

// 子機1/2のRing 2専用。各子機内でSleeve（GND）へ直結する。
constexpr uint8_t CONTINUITY_PINS[] = {12, 14};
constexpr uint32_t CONTINUITY_STABLE_MS = 20;

struct ContinuityState {
    bool connected = false;
    bool candidate = false;
    uint32_t changedAt = 0;
};
ContinuityState continuityStates[buzzer_inputs::CHILD_COUNT];
static_assert(sizeof(CONTINUITY_PINS) / sizeof(CONTINUITY_PINS[0]) == buzzer_inputs::CHILD_COUNT,
              "Each child needs a continuity pin");

void reportContinuity(uint8_t index) {
    Serial.printf("CONTINUITY child=%u connected=%u\n",
                  static_cast<unsigned>(index) + 1U, continuityStates[index].connected ? 1U : 0U);
}

}  // namespace

void begin() {
    for (uint8_t index = 0; index < buzzer_inputs::CHILD_COUNT; ++index) {
        pinMode(CONTINUITY_PINS[index], INPUT_PULLUP);
        auto& state = continuityStates[index];
        state.connected = digitalRead(CONTINUITY_PINS[index]) == LOW;
        state.candidate = state.connected;
        state.changedAt = millis();
        reportContinuity(index);
    }
}

void poll() {
    const uint32_t now = millis();
    for (uint8_t index = 0; index < buzzer_inputs::CHILD_COUNT; ++index) {
        auto& state = continuityStates[index];
        const bool connected = digitalRead(CONTINUITY_PINS[index]) == LOW;
        if (connected != state.candidate) {
            state.candidate = connected;
            state.changedAt = now;
        }
        if (state.candidate != state.connected &&
            static_cast<uint32_t>(now - state.changedAt) >= CONTINUITY_STABLE_MS) {
            state.connected = state.candidate;
            reportContinuity(index);
        }
    }
}

}  // namespace continuity_inputs
