#include "message_box.hpp"

#include "imgui.h"

namespace {

std::string& pending() {
    static std::string message;
    return message;
}

} // namespace

void show_error(const std::string& message) {
    std::string& text = pending();
    text += (text.empty() ? "" : "\n\n") + message;
}

void draw_message_box() {
    std::string& text = pending();
    if (text.empty()) {
        return;
    }
    if (!ImGui::IsPopupOpen("Error")) {
        ImGui::OpenPopup("Error");
    }
    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            text.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
