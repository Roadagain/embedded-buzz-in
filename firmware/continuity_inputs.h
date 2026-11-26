#pragma once

namespace continuity_inputs {

// 内部プルアップ入力を初期化し、各子機の現在の導通状態をシリアルへ出力する。
void begin();
// loopから呼び出し、20 ms安定した状態変化をシリアルへ出力する。
void poll();

}  // namespace continuity_inputs
