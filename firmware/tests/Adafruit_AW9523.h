#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Wire.h"

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
public:
    bool begin(uint8_t address, TwoWire* wire) {
        awAddress = address;
        awWire = wire;
        return awBeginResult;
    }
    bool configureDirection(uint16_t pins) {
        awCalls.push_back("direction=" + std::to_string(pins));
        awDirection = pins;
        return awConfigureResult;
    }
    bool configureLEDMode(uint16_t pins) {
        awLedMode = pins;
        return awConfigureResult;
    }
    bool openDrainPort0(bool value) {
        awOpenDrain = value;
        return awConfigureResult;
    }
    bool outputGPIO(uint16_t pins) {
        ++awWriteCount;
        awCalls.push_back("output=" + std::to_string(pins));
        // 実機では戻り値falseでも一部のレジスタが書き換わり得る。
        for (unsigned pin = 0; pin < awLevels.size(); ++pin) awLevels[pin] = (pins >> pin) & 1;
        return awWriteResult;
    }
};
