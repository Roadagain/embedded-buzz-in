#pragma once

#include <stdint.h>

namespace buzzer_inputs {

enum class Child : uint8_t { None = 0, One = 1, Two = 2 };
enum class Command : uint8_t { None, Reset, Correct, Wrong, ToggleMode };
enum class Mode : uint8_t { EndlessChance, FirstOnly };

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
    bool resetNeeded;
    Child correctChild = Child::None;
    Child wrongChild = Child::None;
    Mode mode = Mode::EndlessChance;
    bool modeChanged = false;
};

void begin();
Snapshot poll(Command command = Command::None);

}  // 名前空間 buzzer_inputs
