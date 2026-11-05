#pragma once

namespace galaxy {

// State for the first-launch language screen. selected is an index into
// languageAt(). confirm becomes true when the user accepts the highlighted row.
struct LanguagePickerState {
    int selected = 0;
    bool confirm = false;
    bool scrollToSelected = true;
    // Height of this frame's contents, including the window padding. The caller
    // sizes the window to it so the area under Continue is only the bottom margin.
    float contentHeight = 0.0f;
};

// One frame of the picker. Call between NewFrame and Render.
void drawLanguagePicker(LanguagePickerState& state);

}  // namespace galaxy
