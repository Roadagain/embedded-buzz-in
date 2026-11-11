#pragma once

#include <stdint.h>

namespace buzzer_inputs {

enum class Child : uint8_t { None = 0, One = 1, Two = 2 };

constexpr uint8_t CHILD_COUNT = 2;

struct Press {
    Child child = Child::None;
    uint32_t elapsedMs = 0;
};

struct Ranking {
    Press order[CHILD_COUNT]{};
    uint8_t count = 0;
};

struct Snapshot {
    Ranking ranking;
    bool resetPressed;
};

void begin();
Snapshot poll(bool resetRequested = false);

}  // 名前空間 buzzer_inputs
