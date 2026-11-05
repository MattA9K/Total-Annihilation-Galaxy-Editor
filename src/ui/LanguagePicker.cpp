#include "ui/LanguagePicker.hpp"

#include "core/I18n.hpp"
#include "ui/Theme.hpp"

#include <imgui.h>

#include <algorithm>

namespace galaxy {

void drawLanguagePicker(LanguagePickerState& state) {
    const int count = languageCount();
    if (count <= 0) {
        return;
    }
    if (state.selected < 0 || state.selected >= count) {
        state.selected = 0;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        state.selected = std::min(state.selected + 1, count - 1);
        state.scrollToSelected = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        state.selected = std::max(state.selected - 1, 0);
        state.scrollToSelected = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
        state.selected = 0;
        state.scrollToSelected = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
        state.selected = count - 1;
        state.scrollToSelected = true;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(
        "##language-picker",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float column = std::min(440.0f * theme::unit(), std::max(220.0f, ImGui::GetContentRegionAvail().x - 32.0f));
    const float left = ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - column) * 0.5f);
    ImGui::Dummy(ImVec2(0.0f, 28.0f * theme::unit()));

    const char* chosenId = languageAt(state.selected).id;
    ImGui::SetCursorPosX(left);
    ImGui::PushFont(theme::boldFont());
    ImGui::PushTextWrapPos(left + column);
    ImGui::TextUnformatted(textIn(chosenId, Text::PickerTitle));
    ImGui::PopFont();
    ImGui::SetCursorPosX(left);
    ImGui::PushFont(theme::smallFont());
    ImGui::TextUnformatted(textIn(chosenId, Text::PickerBody));
    ImGui::PopFont();
    ImGui::PopTextWrapPos();

    ImGui::Dummy(ImVec2(0.0f, 14.0f * theme::unit()));
    ImGui::SetCursorPosX(left);
    const float row = ImGui::GetFrameHeightWithSpacing();
    const float reserve = ImGui::GetFrameHeight() + 28.0f * theme::unit();
    const float listHeight = std::max(row * 4.0f, std::min(row * 8.5f, ImGui::GetContentRegionAvail().y - reserve));
    ImGui::BeginChild("language-list", ImVec2(column, listHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (int i = 0; i < count; ++i) {
        const bool selected = i == state.selected;
        if (ImGui::Selectable(languageAt(i).nativeName, selected)) {
            state.selected = i;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            state.selected = i;
            state.confirm = true;
        }
        if (selected && state.scrollToSelected) {
            ImGui::SetScrollHereY(0.5f);
        }
    }
    state.scrollToSelected = false;
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0.0f, 14.0f * theme::unit()));
    const char* label = textIn(languageAt(state.selected).id, Text::PickerContinue);
    const float buttonWidth = theme::pushButtonWidth(label);
    ImGui::SetCursorPosX(left + std::max(0.0f, column - buttonWidth));
    if (theme::defaultButton(label, ImVec2(buttonWidth, 0.0f))) {
        state.confirm = true;
    }
    ImGui::End();
}

}  // namespace galaxy
