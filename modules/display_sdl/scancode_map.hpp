#pragma once
#include <SDL.h>
#include "types.hpp"

namespace solme {

struct ScancodeSeq {
    u8 code = 0;
    bool extended = false;
    int length = 0;
};

inline ScancodeSeq sdlToSet1(int sc) {
    static const u8 letters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                   0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    static const u8 digits[10] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};
    static const u8 keypad[10] = {0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52};
    auto plain = [](u8 c) { return ScancodeSeq{c, false, 1}; };
    auto ext = [](u8 c) { return ScancodeSeq{c, true, 2}; };

    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        return plain(letters[sc - SDL_SCANCODE_A]);
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        return plain(digits[sc - SDL_SCANCODE_1]);
    }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) {
        return plain(static_cast<u8>(0x3B + (sc - SDL_SCANCODE_F1)));
    }
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) {
        return plain(keypad[sc - SDL_SCANCODE_KP_1]);
    }
    switch (sc) {
        case SDL_SCANCODE_0: return plain(0x0B);
        case SDL_SCANCODE_RETURN: return plain(0x1C);
        case SDL_SCANCODE_ESCAPE: return plain(0x01);
        case SDL_SCANCODE_BACKSPACE: return plain(0x0E);
        case SDL_SCANCODE_TAB: return plain(0x0F);
        case SDL_SCANCODE_SPACE: return plain(0x39);
        case SDL_SCANCODE_MINUS: return plain(0x0C);
        case SDL_SCANCODE_EQUALS: return plain(0x0D);
        case SDL_SCANCODE_LEFTBRACKET: return plain(0x1A);
        case SDL_SCANCODE_RIGHTBRACKET: return plain(0x1B);
        case SDL_SCANCODE_BACKSLASH: return plain(0x2B);
        case SDL_SCANCODE_SEMICOLON: return plain(0x27);
        case SDL_SCANCODE_APOSTROPHE: return plain(0x28);
        case SDL_SCANCODE_GRAVE: return plain(0x29);
        case SDL_SCANCODE_COMMA: return plain(0x33);
        case SDL_SCANCODE_PERIOD: return plain(0x34);
        case SDL_SCANCODE_SLASH: return plain(0x35);
        case SDL_SCANCODE_CAPSLOCK: return plain(0x3A);
        case SDL_SCANCODE_F11: return plain(0x57);
        case SDL_SCANCODE_F12: return plain(0x58);
        case SDL_SCANCODE_SCROLLLOCK: return plain(0x46);
        case SDL_SCANCODE_NUMLOCKCLEAR: return plain(0x45);
        case SDL_SCANCODE_INSERT: return ext(0x52);
        case SDL_SCANCODE_HOME: return ext(0x47);
        case SDL_SCANCODE_PAGEUP: return ext(0x49);
        case SDL_SCANCODE_DELETE: return ext(0x53);
        case SDL_SCANCODE_END: return ext(0x4F);
        case SDL_SCANCODE_PAGEDOWN: return ext(0x51);
        case SDL_SCANCODE_RIGHT: return ext(0x4D);
        case SDL_SCANCODE_LEFT: return ext(0x4B);
        case SDL_SCANCODE_DOWN: return ext(0x50);
        case SDL_SCANCODE_UP: return ext(0x48);
        case SDL_SCANCODE_KP_DIVIDE: return ext(0x35);
        case SDL_SCANCODE_KP_MULTIPLY: return plain(0x37);
        case SDL_SCANCODE_KP_MINUS: return plain(0x4A);
        case SDL_SCANCODE_KP_PLUS: return plain(0x4E);
        case SDL_SCANCODE_KP_ENTER: return ext(0x1C);
        case SDL_SCANCODE_KP_0: return plain(0x52);
        case SDL_SCANCODE_KP_PERIOD: return plain(0x53);
        case SDL_SCANCODE_LCTRL: return plain(0x1D);
        case SDL_SCANCODE_LSHIFT: return plain(0x2A);
        case SDL_SCANCODE_LALT: return plain(0x38);
        case SDL_SCANCODE_RCTRL: return ext(0x1D);
        case SDL_SCANCODE_RSHIFT: return plain(0x36);
        case SDL_SCANCODE_RALT: return ext(0x38);
        default: return ScancodeSeq{};
    }
}

}
