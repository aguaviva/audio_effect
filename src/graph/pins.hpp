#pragma once

// Pin IDs: node_id * 100 + code. code 0 = output; code 1+N = input slot N.
inline int output_pin_id(int node_id) { return node_id * 100; }
inline int input_pin_id(int node_id, int slot = 0) { return node_id * 100 + 1 + slot; }
inline int node_id_from_pin(int pin_id) { return pin_id / 100; }
inline int pin_code_from_pin(int pin_id) { return pin_id % 100; }
inline bool is_output_pin(int pin_id) { return pin_code_from_pin(pin_id) == 0; }
inline int input_slot_from_pin(int pin_id) { return pin_code_from_pin(pin_id) - 1; }
