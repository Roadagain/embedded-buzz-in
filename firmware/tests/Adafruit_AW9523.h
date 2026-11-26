#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Wire.h"

#define AW9523_REG_INPUT0 0x00
inline bool continuityBeginResult = true;
inline bool continuityConfigureResult = true;
inline uint16_t continuityDirection = 0xFFFF;
inline uint16_t continuityLedMode = 0xFFFF;
inline uint16_t continuityInterrupts = 0xFFFF;
inline unsigned continuityWrites = 0;

inline bool awBeginResult = true;
inline bool awWriteResult = true;
inline bool awConfigureResult = true;
inline uint8_t awAddress = 0;
inline TwoWire* awWire = nullptr;
inline uint16_t awDirection = 0;
inline uint16_t awLedMode = 0xFFFF;
inline bool awOpenDrain = true;
inline unsigned awWriteCount = 0;
inline std::array<int, 16> awLevels{};
inline std::vector<std::string> awCalls;

class Adafruit_AW9523 {
    uint8_t address_ = 0;
public:
    bool begin(uint8_t address, TwoWire* wire) {
        address_ = address;
        if (address == 0x59) return continuityBeginResult;
        awAddress = address;
        awWire = wire;
        return awBeginResult;
    }
    bool configureDirection(uint16_t pins) {
        if (address_ == 0x59) {
            continuityDirection = pins;
            return continuityConfigureResult;
        }
        awCalls.push_back("direction=" + std::to_string(pins));
        awDirection = pins;
        return awConfigureResult;
    }
    bool configureLEDMode(uint16_t pins) {
        if (address_ == 0x59) {
            continuityLedMode = pins;
            return continuityConfigureResult;
        }
        awLedMode = pins;
        return awConfigureResult;
    }
    bool openDrainPort0(bool value) {
        awOpenDrain = value;
        return awConfigureResult;
    }
    bool outputGPIO(uint16_t pins) {
        if (address_ == 0x59) { ++continuityWrites; return awWriteResult; }
        ++awWriteCount;
        awCalls.push_back("output=" + std::to_string(pins));
        // 実機では戻り値falseでも一部のレジスタが書き換わり得る。
        for (unsigned pin = 0; pin < awLevels.size(); ++pin) awLevels[pin] = (pins >> pin) & 1;
        return awWriteResult;
    }
    bool interruptEnableGPIO(uint16_t pins) {
        continuityInterrupts = pins;
        return continuityConfigureResult;
    }
};
