#include <Arduino.h>
#include <Adafruit_AW9523.h>
#include <Wire.h>

#include "led_outputs.h"

namespace led_outputs {
namespace {

// 3V3と同じ側のヘッダーに電源・GND・I²Cをまとめる。
constexpr uint8_t SDA_PIN = 25;
constexpr uint8_t SCL_PIN = 26;
constexpr uint8_t ADDRESS = 0x58;
constexpr uint16_t CHILD_ONE = 1U << 0;
constexpr uint16_t CHILD_TWO = 1U << 1;

Adafruit_AW9523 driver;
bool initialized = false;
uint16_t lastOutput = 0;
bool outputKnown = false;

}  // namespace

bool begin() {
    initialized = false;
    outputKnown = false;
    if (!Wire.begin(SDA_PIN, SCL_PIN)) {
        return false;
    }
    Wire.setTimeOut(10);
    // 通常GPIO・プッシュプル。出力ラッチをLOWにしてから0/1だけ出力化する。
    if (!driver.begin(ADDRESS, &Wire) || !driver.configureDirection(0) ||
        !driver.configureLEDMode(0) || !driver.openDrainPort0(false) ||
        !driver.outputGPIO(0) || !driver.configureDirection(CHILD_ONE | CHILD_TWO)) {
        return false;
    }
    lastOutput = 0;
    outputKnown = true;
    initialized = true;
    return true;
}

bool set(bool childOne, bool childTwo) {
    if (!initialized) {
        return false;
    }
    const uint16_t output = (childOne ? CHILD_ONE : 0) | (childTwo ? CHILD_TWO : 0);
    if (outputKnown && output == lastOutput) {
        return true;
    }
    if (!driver.outputGPIO(output)) {
        // 複数レジスタの一部だけ書けた可能性があるため、旧状態への復帰も再送する。
        outputKnown = false;
        return false;
    }
    lastOutput = output;
    outputKnown = true;
    return true;
}

}  // namespace led_outputs
