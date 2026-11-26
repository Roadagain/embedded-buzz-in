#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include "Wire.h"

inline bool continuityDeviceResult = true;
inline bool continuityReadResult = true;
inline uint8_t continuityInputs = 0xFF;
inline unsigned continuityReads = 0;

class Adafruit_I2CDevice {
public:
    Adafruit_I2CDevice(uint8_t address, TwoWire* wire) : address_(address), wire_(wire) {}
    bool begin() {
        assert(address_ == 0x59 && wire_ == &Wire);
        return continuityDeviceResult;
    }
    bool write_then_read(const uint8_t* writeBuffer, size_t writeLength,
                         uint8_t* readBuffer, size_t readLength) {
        assert(address_ == 0x59 && wire_ == &Wire);
        assert(writeLength == 1 && writeBuffer[0] == 0x00 && readLength == 1);
        ++continuityReads;
        // 失敗時のバッファ値を状態として扱わないことを確認する。
        *readBuffer = continuityReadResult ? continuityInputs : 0;
        return continuityReadResult;
    }
private:
    uint8_t address_;
    TwoWire* wire_;
};
