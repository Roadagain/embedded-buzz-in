#pragma once

#include "buzzer_inputs.h"

namespace serial_commands {

// 改行まで受信したコマンドを返す。入力途中やエラー時はCommand::Noneを返す。
buzzer_inputs::Command poll();

}  // 名前空間 serial_commands
