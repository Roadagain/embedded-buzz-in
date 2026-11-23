#pragma once

namespace led_outputs {

bool begin();
// 状態が変わったときだけ書き込む。失敗時は次回の呼び出しで再送する。
bool set(bool childOne, bool childTwo);

}  // namespace led_outputs
