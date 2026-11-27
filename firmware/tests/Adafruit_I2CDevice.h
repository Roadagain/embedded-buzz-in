#pragma once

#include <array>
#include <string>
#include <vector>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "Wire.h"

inline bool continuityBeginResult = true;
inline bool continuityWriteResult = true;
inline int continuityFailRegister = -1;
inline unsigned continuityWrites = 0;
inline std::array<uint8_t, 0x16> continuityRegisters{};
inline bool continuityReadResult = true;
inline uint8_t continuityInputs = 0xFF;
inline unsigned continuityReads = 0;

inline bool mcpBeginResult = true;
inline bool mcpWriteResult = true;
inline uint8_t mcpAddress = 0;
inline TwoWire* mcpWire = nullptr;
inline unsigned mcpWriteCount = 0;
inline int mcpFailRegister = -1;
inline std::array<uint8_t, 0x16> mcpRegisters{};
inline std::array<int, 16> mcpLevels{};
inline std::vector<std::string> mcpCalls;

class Adafruit_I2CDevice {
public:
    Adafruit_I2CDevice(uint8_t address, TwoWire* wire) : address_(address), wire_(wire) {}
    bool begin() {
        assert(wire_ == &Wire);
        if (address_ == 0x21) return continuityBeginResult;
        assert(address_ == 0x20);
        mcpAddress = address_;
        mcpWire = wire_;
        return mcpBeginResult;
    }
    bool write(const uint8_t* buffer, size_t length) {
        assert((address_ == 0x20 || address_ == 0x21) && wire_ == &Wire && length == 2);
        const bool continuity = address_ == 0x21;
        auto& registers = continuity ? continuityRegisters : mcpRegisters;
        uint8_t reg = buffer[0];
        const uint8_t value = buffer[1];
        // BANK=1ではA/Bのレジスタが別ブロックに並ぶ。
        if (registers[0x0A] & 0x80) {
            assert((reg & 0x0F) <= 0x0A);
            reg = (reg & 0x0F) * 2 + (reg >= 0x10 ? 1 : 0);
        }
        assert(reg < registers.size());
        // エラーでも書き換わり得る。再送が必要なケースを模擬する。
        registers[reg] = value;
        if (continuity) {
            ++continuityWrites;
            return continuityWriteResult && reg != continuityFailRegister;
        }
        ++mcpWriteCount;
        mcpCalls.push_back("register=" + std::to_string(reg) + " value=" + std::to_string(value));
        if (reg == 0x14 || reg == 0x15) {
            const unsigned offset = reg == 0x14 ? 0 : 8;
            for (unsigned pin = 0; pin < 8; ++pin) mcpLevels[offset + pin] = (value >> pin) & 1;
        }
        return mcpWriteResult && reg != mcpFailRegister;
    }
    bool write_then_read(const uint8_t* writeBuffer, size_t writeLength,
                         uint8_t* readBuffer, size_t readLength) {
        assert(address_ == 0x21 && wire_ == &Wire);
        assert(writeLength == 1 && writeBuffer[0] == 0x12 && readLength == 1);
        ++continuityReads;
        // 失敗時のバッファ値を状態として扱わないことを確認する。
        *readBuffer = continuityReadResult ? continuityInputs : 0;
        return continuityReadResult;
    }
private:
    uint8_t address_;
    TwoWire* wire_;
};
