#pragma once

#include <string>

// UI thread. Queues an error for the modal message box; several in one frame are shown together.
void show_error(const std::string& message);
// Draws the message box while an error is pending. Call once per frame.
void draw_message_box();
