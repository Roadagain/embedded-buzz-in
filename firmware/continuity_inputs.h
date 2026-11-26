#pragma once

#include <stdint.h>

namespace continuity_inputs {

// 初期化済みWireバス上のAW9523（0x59）を入力に設定し、導通状態を出力する。
// 初期化失敗時は状態不明とし、再起動まで検知を停止する。
void begin();
// loopから呼ぶ。1000 ms周期で読み取り、連続する読み取りで一致した状態変化を出力する。
// 読み取り失敗時は状態不明とし、復旧後に20 msの安定確認をやり直す。
void poll();
// 子機インデックス（0始まり）の確定済み導通状態。状態不明・範囲外はfalse。
bool isConnected(uint8_t childIndex);

}  // namespace continuity_inputs
