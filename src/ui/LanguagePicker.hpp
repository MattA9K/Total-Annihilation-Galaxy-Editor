#pragma once

namespace galaxy {

// State for the first-launch language screen. selected is an index into
// languageAt(). confirm becomes true when the user accepts the highlighted row.
struct LanguagePickerState {
    int selected = 0;
    bool confirm = false;
    bool scrollToSelected = true;
};

// One frame of the picker. Call between NewFrame and Render.
void drawLanguagePicker(LanguagePickerState& state);

}  // namespace galaxy
