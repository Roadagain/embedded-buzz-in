#include "serial_commands.h"

#include <Arduino.h>
#include <string.h>

namespace serial_commands {

using buzzer_inputs::Command;

buzzer_inputs::Command poll() {
    static char command[32];
    static size_t length = 0;
    static bool overflow = false;

    // 1ループの受信量を制限し、入力途中でもLEDと早押しを処理する。
    for (size_t readCount = 0; readCount < sizeof(command) && Serial.available() > 0;
         ++readCount) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r' || ch == '\n') {
            if (length == 0 && !overflow) {
                continue;
            }
            command[length] = '\0';
            Command result = Command::None;
            if (!overflow && length == 5 && strcmp(command, "reset") == 0) {
                result = Command::Reset;
            } else if (!overflow && length == 7 && strcmp(command, "correct") == 0) {
                result = Command::Correct;
            } else if (!overflow && length == 5 && strcmp(command, "wrong") == 0) {
                result = Command::Wrong;
            } else {
                Serial.println(overflow ? "ERROR command_too_long" : "ERROR unknown_command");
            }
            length = 0;
            overflow = false;
            return result;
        }
        if (length < sizeof(command) - 1) {
            command[length++] = ch;
        } else {
            overflow = true;
        }
    }
    return Command::None;
}

}  // 名前空間 serial_commands
