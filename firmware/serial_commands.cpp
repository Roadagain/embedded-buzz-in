#include "serial_commands.h"

#include <Arduino.h>

namespace serial_commands {

using buzzer_inputs::Command;

buzzer_inputs::Command poll() {
    // 1回に1文字だけ処理し、改行を待たずにコマンドを返す。
    switch (Serial.read()) {
        case -1:
        case '\r':
        case '\n':
            return Command::None;
        case 'r':
            return Command::Reset;
        case 'o':
            return Command::Correct;
        case 'x':
            return Command::Wrong;
        case 'm':
            return Command::ToggleMode;
        default:
            Serial.println("ERROR unknown_command");
            return Command::None;
    }
}

}  // 名前空間 serial_commands
