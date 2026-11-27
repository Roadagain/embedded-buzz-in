#include <Arduino.h>
#include <Adafruit_I2CDevice.h>
#include <Wire.h>

#include "led_outputs.h"

namespace led_outputs {
namespace {

// 3V3と同じ側のヘッダーに電源・GND・I²Cをまとめる。
constexpr uint8_t SDA_PIN = 25;
constexpr uint8_t SCL_PIN = 26;
constexpr uint8_t ADDRESS = 0x20;
constexpr uint8_t CHILD_ONE = 1U << 0;
constexpr uint8_t CHILD_TWO = 1U << 1;

// MCP23017、BANK=0のレジスタ。転送結果を返すBusIOで通信エラーを検出する。
constexpr uint8_t IODIRA = 0x00;
constexpr uint8_t IODIRB = 0x01;
constexpr uint8_t GPINTENA = 0x04;
constexpr uint8_t GPINTENB = 0x05;
constexpr uint8_t IOCON = 0x0A;
constexpr uint8_t GPPUA = 0x0C;
constexpr uint8_t GPPUB = 0x0D;
constexpr uint8_t OLATA = 0x14;
constexpr uint8_t OLATB = 0x15;

Adafruit_I2CDevice device(ADDRESS, &Wire);

bool writeRegister(uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    return device.write(data, sizeof(data));
}

bool initialized = false;
uint8_t lastOutput = 0;
bool outputKnown = false;

}  // namespace

bool begin() {
    initialized = false;
    outputKnown = false;
    if (!Wire.begin(SDA_PIN, SCL_PIN)) {
        return false;
    }
    Wire.setTimeOut(10);
    if (!device.begin()) return false;
    // ESP32だけの再起動でもBANK=0へ戻す。0x05はBANK=1ならIOCON、
    // BANK=0ならGPINTENBなので、どちらでも出力ラッチを変更しない。
    if (!writeRegister(0x05, 0) || !writeRegister(IOCON, 0) ||
        !writeRegister(GPINTENA, 0) || !writeRegister(GPINTENB, 0) ||
        !writeRegister(GPPUA, 0) || !writeRegister(GPPUB, 0) ||
        !writeRegister(OLATA, 0) || !writeRegister(OLATB, 0) ||
        // 消灯ラッチを設定してからGPA0/1を出力化する。
        // DS20001952Dに従い、出力専用のGPA7/GPB7もLOW出力にする。
        !writeRegister(IODIRA, 0x7C) || !writeRegister(IODIRB, 0x7F)) {
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
    const uint8_t output = (childOne ? CHILD_ONE : 0) | (childTwo ? CHILD_TWO : 0);
    if (outputKnown && output == lastOutput) {
        return true;
    }
    if (!writeRegister(OLATA, output)) {
        // 失敗が返ってもラッチが書き換わった可能性があるため、旧状態への復帰も再送する。
        outputKnown = false;
        return false;
    }
    lastOutput = output;
    outputKnown = true;
    return true;
}

}  // namespace led_outputs
