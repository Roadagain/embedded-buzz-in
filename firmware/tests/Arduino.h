#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

#define ARDUINO_ISR_ATTR
#define INPUT_PULLUP 2
#define OUTPUT 1
#define LOW 0
#define HIGH 1
#define FALLING 2
#define CHANGE 3
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define portENTER_CRITICAL_ISR(mux) ((void)(mux))
#define portEXIT_CRITICAL_ISR(mux) ((void)(mux))

inline uint32_t testNow = 0;
// 未操作の子機入力はプルアップされている。LOWを設定すれば起動時押下も模擬できる。
inline auto levels = [] {
    std::array<int, 40> values{};
    values.fill(HIGH);
    return values;
}();
inline int interruptModes[40]{};
inline void (*interrupts[40])(void*){};
inline void* contexts[40]{};
inline uint32_t millis() { return testNow; }
inline void (*onTestDelay)(uint32_t) = nullptr;
inline void delay(uint32_t duration) {
    if (onTestDelay) onTestDelay(duration);
    testNow += duration;
}
inline void pinMode(uint8_t, int) {}
inline int digitalRead(uint8_t pin) { return levels[pin]; }
inline void digitalWrite(uint8_t pin, int level) { levels[pin] = level; }
inline void attachInterruptArg(uint8_t pin, void (*callback)(void*), void* context, int mode) {
    interrupts[pin] = callback;
    contexts[pin] = context;
    interruptModes[pin] = mode;
}

inline void setInputLevel(uint8_t pin, int level) {
    const int previous = levels[pin];
    levels[pin] = level;
    if (previous != level && interrupts[pin] &&
        (interruptModes[pin] == CHANGE || (interruptModes[pin] == FALLING && level == LOW))) {
        interrupts[pin](contexts[pin]);
    }
}

struct TestSerial {
    std::string output;
    std::deque<char> input;
    void begin(int) {}
    int read() {
        if (input.empty()) return -1;
        const char value = input.front();
        input.pop_front();
        return value;
    }
    void println(const char* line) { output += std::string(line) + "\n"; }
    template <typename... Args> void printf(const char* format, Args... args) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), format, args...);
        output += buffer;
    }
};
inline TestSerial Serial;
