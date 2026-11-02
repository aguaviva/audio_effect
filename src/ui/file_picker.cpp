#include "file_picker.hpp"

#include "imgui.h"
#include "imfilebrowser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Opens the browser's file picker and copies the chosen file into dir in the page's file system.
EM_JS(void, upload_file_to, (const char* dir, const char* accept), {
    const d = UTF8ToString(dir);
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = UTF8ToString(accept);
    input.onchange = async () => {
        const file = input.files[0];
        if (!file) return;
        try {
            const bytes = new Uint8Array(await file.arrayBuffer());
            FS.mkdirTree(d);
            FS.writeFile(d + '/' + file.name, bytes);
        } catch (e) {
            console.error('Upload of ' + file.name + ' failed:', e);
        }
    };
    input.click();
});
#endif

void open_browser_near_path(ImGui::FileBrowser& browser, const char* path_hint,
                            bool enter_filename) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path hint = path_hint ? fs::path(path_hint) : fs::path{};
    if (!hint.empty()) {
        if (fs::is_directory(hint, ec)) {
            browser.SetDirectory(hint);
        } else if (hint.has_parent_path() && fs::is_directory(hint.parent_path(), ec)) {
            browser.SetDirectory(hint.parent_path());
        }
        if (enter_filename && hint.has_filename() && !fs::is_directory(hint, ec)) {
            browser.SetInputName(hint.filename().string());
        }
    }
    browser.Open();
}

FilePicker::FilePicker(const char* title, const char* extension)
    : title_(title), extension_(extension) {}

FilePicker::~FilePicker() = default;

#ifdef __EMSCRIPTEN__
void FilePicker::open(const std::string& /*current_path*/) {
    // A fresh directory per upload, so poll() finds only the file from this upload.
    static int uploads = 0;
    upload_dir_ = "/uploads/" + std::to_string(++uploads);
    upload_file_to(upload_dir_.c_str(), extension_.c_str());
}

std::string FilePicker::poll() {
    if (upload_dir_.empty()) {
        return {};
    }
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(upload_dir_, ec)) {
        upload_dir_.clear();
        return entry.path().string();
    }
    return {};
}
#else
void FilePicker::open(const std::string& current_path) {
    if (!browser_) {
        browser_ = std::make_unique<ImGui::FileBrowser>(ImGuiFileBrowserFlags_CloseOnEsc |
                                                        ImGuiFileBrowserFlags_ConfirmOnEnter |
                                                        ImGuiFileBrowserFlags_EditPathString);
        browser_->SetTitle(title_);
        std::string upper = extension_;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        browser_->SetTypeFilters({extension_, upper}); // matching is case-sensitive off Windows
    }
    open_browser_near_path(*browser_, current_path.c_str());
}

std::string FilePicker::poll() {
    if (!browser_) {
        return {};
    }
    browser_->Display();
    if (!browser_->HasSelected()) {
        return {};
    }
    std::string picked = browser_->GetSelected().string();
    browser_->ClearSelected();
    return picked;
}
#endif
