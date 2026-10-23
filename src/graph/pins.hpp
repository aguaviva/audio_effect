#pragma once

// Pin IDs: node_id * 100 + code. Codes 1..49 are input slots 0..48; codes 50..99 are output
// slots 0..49.
constexpr int kOutputPinCode = 50;
inline int output_pin_id(int node_id, int slot = 0) { return node_id * 100 + kOutputPinCode + slot; }
inline int input_pin_id(int node_id, int slot = 0) { return node_id * 100 + 1 + slot; }
inline int node_id_from_pin(int pin_id) { return pin_id / 100; }
inline int pin_code_from_pin(int pin_id) { return pin_id % 100; }
inline bool is_output_pin(int pin_id) { return pin_code_from_pin(pin_id) >= kOutputPinCode; }
inline int input_slot_from_pin(int pin_id) { return pin_code_from_pin(pin_id) - 1; }
inline int output_slot_from_pin(int pin_id) { return pin_code_from_pin(pin_id) - kOutputPinCode; }
