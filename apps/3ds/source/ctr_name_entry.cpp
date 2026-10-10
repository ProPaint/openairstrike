#include "ctr_name_entry.h"

#include <3ds.h>

#include "name_entry.h"

namespace ctrapp {

namespace {

constexpr int kMaxName = 31; // HighScore::name (as3d/profile.h), profile_io's limit

struct Filter {
    float fieldWidth = 0.0f;
};

// The applet calls this when OK is pressed (libctru runs it in the app): the text must be one the
// field keeps whole, else the keyboard stays up with the message.
SwkbdCallbackResult filterName(void* user, const char** message, const char* text, size_t len) {
    const Filter& f = *static_cast<const Filter*>(user);
    const std::string s(text, len);
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7E) {
            *message = "Letters A-Z, digits and . - _ only\n(no accents).";
            return SWKBD_CALLBACK_CONTINUE;
        }
    }
    if (as3d_game::acceptedName(s, f.fieldWidth) != s) {
        *message = "This name is too long for the\nhigh-score table.";
        return SWKBD_CALLBACK_CONTINUE;
    }
    return SWKBD_CALLBACK_OK;
}

} // namespace

NameKeyboard askName(const std::string& current, float fieldWidth, std::string* name, int* detail) {
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, kMaxName);
    swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetHintText(&kb, "Your name");
    swkbdSetInitialText(&kb, current.c_str());
    swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0); // the original accepts an empty name
    swkbdSetFeatures(&kb, SWKBD_DEFAULT_QWERTY);
    Filter filter;
    filter.fieldWidth = fieldWidth;
    swkbdSetFilterCallback(&kb, filterName, &filter);
    char buf[4 * kMaxName + 1] = {}; // UTF-8 out
    const SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
    if (detail) *detail = static_cast<int>(swkbdGetResult(&kb));
    if (b == SWKBD_BUTTON_RIGHT) {
        if (name) *name = buf;
        return NameKeyboard::Ok;
    }
    return b == SWKBD_BUTTON_LEFT ? NameKeyboard::Cancel : NameKeyboard::Failed;
}

} // namespace ctrapp
