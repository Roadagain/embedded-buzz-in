#pragma once

#include <cassert>
#include <cstdint>
#include "Wire.h"

#define AW9523_REG_INPUT0 0x00
inline bool continuityBeginResult = true;
inline bool continuityConfigureResult = true;
inline uint16_t continuityDirection = 0xFFFF;
inline uint16_t continuityLedMode = 0xFFFF;
inline uint16_t continuityInterrupts = 0xFFFF;
inline unsigned continuityWrites = 0;

class Adafruit_AW9523 {
public:
    bool begin(uint8_t address, TwoWire* wire) {
        assert(address == 0x59 && wire == &Wire);
        return continuityBeginResult;
    }
    bool configureDirection(uint16_t pins) {
        continuityDirection = pins;
        return continuityConfigureResult;
    }
    bool configureLEDMode(uint16_t pins) {
        continuityLedMode = pins;
        return continuityConfigureResult;
    }
    bool interruptEnableGPIO(uint16_t pins) {
        continuityInterrupts = pins;
        return continuityConfigureResult;
    }
};
