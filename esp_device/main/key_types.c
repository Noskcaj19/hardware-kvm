#include <stdint.h>
#include <class/hid/hid.h>
#include "key_types.h"


uint8_t table[0x200] = {0};


void init_synergy_hid_key_table() {
    for (int i = 'A'; i < 'Z'; i++) {
        table[i] = HID_KEY_A + (i - 'A');
    }
    for (int i = 'a'; i < 'z'; i++) {
        table[i] = HID_KEY_A + (i - 'a');
    }


    for (int i = 0; i < 9; i++) {
        table['1' + i] = HID_KEY_1 + i;
    }
    table[' '] = HID_KEY_SPACE;
    table['/'] = HID_KEY_SLASH;
    table['?'] = HID_KEY_SLASH;
    table['0'] = HID_KEY_0;
    table['!'] = HID_KEY_1;
    table['@'] = HID_KEY_2;
    table['#'] = HID_KEY_3;
    table['$'] = HID_KEY_4;
    table['%'] = HID_KEY_5;
    table['^'] = HID_KEY_6;
    table['&'] = HID_KEY_7;
    table['*'] = HID_KEY_8;
    table['('] = HID_KEY_9;
    table[')'] = HID_KEY_0;
    table['['] = HID_KEY_BRACKET_LEFT;
    table[']'] = HID_KEY_BRACKET_RIGHT;
    table['{'] = HID_KEY_BRACKET_LEFT;
    table['}'] = HID_KEY_BRACKET_RIGHT;
    table['-'] = HID_KEY_MINUS;
    table['_'] = HID_KEY_MINUS;
    table['='] = HID_KEY_EQUAL;
    table['+'] = HID_KEY_EQUAL;
    table['.'] = HID_KEY_PERIOD;
    table[','] = HID_KEY_COMMA;
    table['`'] = HID_KEY_GRAVE;
    table['~'] = HID_KEY_GRAVE;
    table['\\'] = HID_KEY_BACKSLASH;
    table['|'] = HID_KEY_BACKSLASH;

    table[kKeyF1] = HID_KEY_F1;
    table[kKeyF2] = HID_KEY_F2;
    table[kKeyF3] = HID_KEY_F3;
    table[kKeyF4] = HID_KEY_F4;
    table[kKeyF5] = HID_KEY_F5;
    table[kKeyF6] = HID_KEY_F6;
    table[kKeyF7] = HID_KEY_F7;
    table[kKeyF8] = HID_KEY_F8;
    table[kKeyF9] = HID_KEY_F9;
    table[kKeyF10] = HID_KEY_F10;
    table[kKeyF11] = HID_KEY_F11;
    table[kKeyF12] = HID_KEY_F12;
    table[kKeyF13] = HID_KEY_F13;
    table[kKeyF14] = HID_KEY_F14;
    table[kKeyF15] = HID_KEY_F15;
    table[kKeyF16] = HID_KEY_F16;
    table[kKeyF17] = HID_KEY_F17;
    table[kKeyF18] = HID_KEY_F18;
    table[kKeyF19] = HID_KEY_F19;
    table[kKeyF20] = HID_KEY_F20;
    table[kKeyF21] = HID_KEY_F21;
    table[kKeyF22] = HID_KEY_F22;
    table[kKeyF23] = HID_KEY_F23;
    table[kKeyF24] = HID_KEY_F24;

    table[kKeyDelete] = HID_KEY_DELETE;
    table[kKeyCapsLock] = HID_KEY_CAPS_LOCK;
    table[kKeyTab] = HID_KEY_TAB;
    table[kKeyBackSpace] = HID_KEY_BACKSPACE;
    table[kKeyReturn] = HID_KEY_ENTER;
    table[kKeyShift_L] = HID_KEY_SHIFT_LEFT;
    table[kKeyShift_R] = HID_KEY_SHIFT_RIGHT;
    table[kKeyControl_L] = HID_KEY_CONTROL_LEFT;
    table[kKeyControl_R] = HID_KEY_CONTROL_RIGHT;
    table[kKeyAlt_L] = HID_KEY_ALT_LEFT;
    table[kKeyAlt_R] = HID_KEY_ALT_RIGHT;
    table[kKeySuper_L] = HID_KEY_GUI_RIGHT;
    table[kKeySuper_R] = HID_KEY_GUI_RIGHT;

    table[kKeyUp] = HID_KEY_ARROW_UP;
    table[kKeyDown] = HID_KEY_ARROW_DOWN;
    table[kKeyLeft] = HID_KEY_ARROW_LEFT;
    table[kKeyRight] = HID_KEY_ARROW_RIGHT;

    table[kKeyHome] = HID_KEY_HOME;
    table[kKeyEnd] = HID_KEY_END;
    table[kKeyInsert] = HID_KEY_INSERT;
    table[kKeyPageUp] = HID_KEY_PAGE_UP;
    table[kKeyPageDown] = HID_KEY_PAGE_DOWN;


    table[kKeyBrightnessDown] = 0x070;
    table[kKeyBrightnessUp] = 0x06F;
    table[kKeyAudioPlay] = 0x0CD;
    table[kKeyAudioNext] = 0x0B5;
    table[kKeyAudioPrev] = 0x0B6;

}

uint8_t synergy_to_hid(KeyID id) {
    return table[id];
}