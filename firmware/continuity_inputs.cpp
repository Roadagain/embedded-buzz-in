#include "continuity_inputs.h"

#include <Arduino.h>
#include <Adafruit_I2CDevice.h>
#include <Wire.h>

#include "buzzer_inputs.h"

namespace continuity_inputs {
namespace {

constexpr uint8_t ADDRESS = 0x21;
// GPA0/GPA1を内部プルアップ付き入力にする。GPA7/GPB7は出力専用なので使わない。
constexpr uint8_t CONTINUITY_PINS[] = {0, 1};
constexpr uint32_t POLL_INTERVAL_MS = 100;
constexpr uint32_t CONTINUITY_STABLE_MS = 100;

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
// MCP23017、BANK=0のレジスタ。
constexpr uint8_t IODIRA = 0x00;
constexpr uint8_t IODIRB = 0x01;
constexpr uint8_t IPOLA = 0x02;
constexpr uint8_t IPOLB = 0x03;
constexpr uint8_t GPINTENA = 0x04;
constexpr uint8_t GPINTENB = 0x05;
constexpr uint8_t IOCON = 0x0A;
constexpr uint8_t GPPUA = 0x0C;
constexpr uint8_t GPPUB = 0x0D;
constexpr uint8_t GPIOA = 0x12;
constexpr uint8_t OLATA = 0x14;
constexpr uint8_t OLATB = 0x15;
constexpr uint8_t INPUT_PULLUPS = (1U << CONTINUITY_PINS[0]) | (1U << CONTINUITY_PINS[1]);
Adafruit_I2CDevice device(ADDRESS, &Wire);
bool initialized = false;
bool readError = false;
uint32_t lastPolledAt = 0;

void reportContinuity(uint8_t index) {
    const auto& state = continuityStates[index];
    Serial.printf("CONTINUITY child=%u connected=%s\n", static_cast<unsigned>(index) + 1U,
                  !state.known ? "unknown" : state.connected ? "1" : "0");
}

bool writeRegister(uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return device.write(data, sizeof(data));
}

bool readInputs(uint8_t& inputs) {
    // 転送失敗をLOW（導通あり）と誤判定しない。
    const uint8_t reg = GPIOA;
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
    // 0x05はBANK=1ならIOCON、BANK=0ならGPINTENB。ESP32だけの再起動にも対応する。
    // ラッチをLOWにしてから方向を設定し、検知線をHIGH出力にしない。
    // 出力専用のGPA7/GPB7はLOW出力にし、未接続にしておく。
    if (!device.begin() || !writeRegister(0x05, 0) || !writeRegister(IOCON, 0) ||
        !writeRegister(GPINTENA, 0) || !writeRegister(GPINTENB, 0) ||
        !writeRegister(OLATA, 0) || !writeRegister(OLATB, 0) ||
        !writeRegister(IODIRA, 0x7F) || !writeRegister(IODIRB, 0x7F) ||
        !writeRegister(IPOLA, 0) || !writeRegister(IPOLB, 0) ||
        !writeRegister(GPPUA, INPUT_PULLUPS) || !writeRegister(GPPUB, 0)) {
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
