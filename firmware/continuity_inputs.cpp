#include "continuity_inputs.h"

#include <Arduino.h>
#include <Adafruit_AW9523.h>
#include <Adafruit_I2CDevice.h>
#include <Wire.h>

#include "buzzer_inputs.h"

namespace continuity_inputs {
namespace {

constexpr uint8_t ADDRESS = 0x59;
// P0_0/P0_1は0x59での電源投入・リセット時にHi-Z。外付けプルアップが必要。
constexpr uint8_t CONTINUITY_PINS[] = {0, 1};
constexpr uint32_t POLL_INTERVAL_MS = 1000;
constexpr uint32_t CONTINUITY_STABLE_MS = 20;

struct ContinuityState {
    bool known = false;
    bool connected = false;
    bool candidate = false;
    bool candidateValid = false;
    uint32_t changedAt = 0;
};
ContinuityState continuityStates[buzzer_inputs::CHILD_COUNT];
static_assert(sizeof(CONTINUITY_PINS) / sizeof(CONTINUITY_PINS[0]) == buzzer_inputs::CHILD_COUNT,
              "Each child needs a continuity pin");
Adafruit_AW9523 driver;
Adafruit_I2CDevice device(ADDRESS, &Wire);
bool initialized = false;
bool readError = false;
uint32_t lastPolledAt = 0;

void reportContinuity(uint8_t index) {
    const auto& state = continuityStates[index];
    Serial.printf("CONTINUITY child=%u connected=%s\n", static_cast<unsigned>(index) + 1U,
                  !state.known ? "unknown" : state.connected ? "1" : "0");
}

bool readInputs(uint8_t& inputs) {
    // inputGPIO()は通信エラーを返さないため、成功可否が分かる転送APIを使う。
    const uint8_t reg = AW9523_REG_INPUT0;
    return device.write_then_read(&reg, 1, &inputs, 1);
}

void reportReadError() {
    if (!readError) {
        Serial.println("ERROR continuity_read_failed");
        for (uint8_t index = 0; index < buzzer_inputs::CHILD_COUNT; ++index) {
            continuityStates[index].known = false;
            reportContinuity(index);
        }
    }
    for (auto& state : continuityStates) state.candidateValid = false;
    readError = true;
}

}  // namespace

void begin() {
    initialized = false;
    readError = false;
    for (auto& state : continuityStates) state = ContinuityState{};
    // WireはLED側で初期化済み。同じバスを使用する。
    // begin()内の設定の戻り値はライブラリで無視されるため、必要な設定を再確認する。
    if (!driver.begin(ADDRESS, &Wire) || !driver.configureDirection(0) ||
        !driver.configureLEDMode(0) || !driver.interruptEnableGPIO(0) || !device.begin()) {
        Serial.println("ERROR continuity_init_failed");
        for (uint8_t index = 0; index < buzzer_inputs::CHILD_COUNT; ++index) reportContinuity(index);
        return;
    }
    initialized = true;
    lastPolledAt = millis();
    uint8_t inputs;
    if (!readInputs(inputs)) {
        reportReadError();
        return;
    }
    for (uint8_t index = 0; index < buzzer_inputs::CHILD_COUNT; ++index) {
        auto& state = continuityStates[index];
        state.known = true;
        state.connected = (inputs & (1U << CONTINUITY_PINS[index])) == 0;
        state.candidate = state.connected;
        state.candidateValid = true;
        state.changedAt = millis();
        reportContinuity(index);
    }
}

void poll() {
    const uint32_t now = millis();
    if (!initialized || static_cast<uint32_t>(now - lastPolledAt) < POLL_INTERVAL_MS) return;
    lastPolledAt = now;
    uint8_t inputs;
    if (!readInputs(inputs)) {
        reportReadError();
        return;
    }
    readError = false;
    for (uint8_t index = 0; index < buzzer_inputs::CHILD_COUNT; ++index) {
        auto& state = continuityStates[index];
        const bool connected = (inputs & (1U << CONTINUITY_PINS[index])) == 0;
        if (!state.candidateValid || connected != state.candidate) {
            state.candidate = connected;
            state.candidateValid = true;
            state.changedAt = now;
        }
        if ((!state.known || state.candidate != state.connected) &&
            static_cast<uint32_t>(now - state.changedAt) >= CONTINUITY_STABLE_MS) {
            state.known = true;
            state.connected = state.candidate;
            reportContinuity(index);
        }
    }
}

bool isConnected(uint8_t childIndex) {
    return childIndex < buzzer_inputs::CHILD_COUNT && continuityStates[childIndex].known &&
           continuityStates[childIndex].connected;
}

}  // namespace continuity_inputs
