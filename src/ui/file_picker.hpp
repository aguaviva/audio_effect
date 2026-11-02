#pragma once

#include <memory>
#include <string>

namespace ImGui {
class FileBrowser;
}

// Opens the browser in the directory of path_hint, or with its file name typed in when
// enter_filename is set (for saving).
void open_browser_near_path(ImGui::FileBrowser& browser, const char* path_hint,
                            bool enter_filename = false);

// Picks a file to open: the ImGui file browser on desktop, an upload from the user's machine
// on the web. The owner calls poll() every frame while drawing.
class FilePicker {
public:
    FilePicker(const char* title, const char* extension);
    ~FilePicker();

    void open(const std::string& current_path);
    // The picked path, once; empty otherwise.
    std::string poll();

private:
    std::string title_;
    std::string extension_;
#ifdef __EMSCRIPTEN__
    std::string upload_dir_;
#else
    std::unique_ptr<ImGui::FileBrowser> browser_; // created on first open; it scans a directory
#endif
};
