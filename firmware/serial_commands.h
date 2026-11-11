#pragma once

#include "buzzer_inputs.h"

namespace serial_commands {

// 1文字受信してコマンドを返す。受信なし・CR・LF・エラー時はCommand::Noneを返す。
buzzer_inputs::Command poll();

}  // 名前空間 serial_commands
