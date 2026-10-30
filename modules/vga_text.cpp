#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "machine.hpp"

namespace solme {

namespace {

const u8 kFont16[256 * 16] = {
#include "vga_font16.inc"
};

const u32 kPalette[16] = {0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA, 0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
                          0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF, 0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF};

const u32 kCp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

void appendUtf8(std::string& out, u32 cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void appendGlyph(std::string& out, u8 ch) {
    if (ch >= 0x20 && ch < 0x7F) {
        out.push_back(static_cast<char>(ch));
    } else if (ch >= 0x80) {
        appendUtf8(out, kCp437[ch - 0x80]);
    } else {
        out.push_back(' ');
    }
}

}

class VgaText : public Device, public VideoSource {
public:
    VgaText(Machine& m, const Props& props) : machine(m), memory(*this), ports(*this) {
        base = props.num("base", 0xB8000);
        terminal = props.str("display", "none") == "term";
        ram.assign(0x8000, 0);
        for (size_t i = 0; i < ram.size(); i += 2) {
            ram[i] = ' ';
            ram[i + 1] = 0x07;
        }
        machine.space("mem").mapHandler(base, 0x8000, &memory);
        machine.space("io").mapHandler(0x3D4, 12, &ports);
        machine.addPoller([this]() { refresh(false); });
        machine.addDumper([this](std::ostream& out) { dumpText(out); });
        machine.registerVideo(this);
        machine.addDescriber([this]() { return std::string("VGA text 80x25 (720x400) at 0x") + toHex(base) + "\n"; });
        crtc[10] = 0x0E;
        crtc[11] = 0x0F;
        frame.assign(720 * 400, 0xFF000000);
    }

    std::string videoName() const override { return "vga-text"; }

    bool captureFrame(VideoFrame& out) override {
        bool phase = blinkPhase();
        if (phase != lastPhase) {
            lastPhase = phase;
            frameDirty = true;
        }
        if (frameDirty) {
            renderFrame(phase);
            frameDirty = false;
            ++generation;
        }
        out.width = 720;
        out.height = 400;
        out.pixels = frame.data();
        out.generation = generation;
        return true;
    }

    void shutdown() override {
        if (terminal && active) {
            refresh(true);
            std::string tail = "\x1b[0m\x1b[?25h\x1b[?1049l";
            Host::instance().writeOut(tail.data(), tail.size());
            Host::instance().flushOut();
            active = false;
        }
    }

private:
    struct Memory : IoHandler {
        explicit Memory(VgaText& o) : owner(o) {}
        u64 read(Addr offset, int size) override {
            u64 value = 0;
            for (int i = 0; i < size; ++i) {
                value |= static_cast<u64>(owner.ram[(offset + i) & 0x7FFF]) << (8 * i);
            }
            return value;
        }
        void write(Addr offset, int size, u64 value) override {
            for (int i = 0; i < size; ++i) {
                owner.ram[(offset + i) & 0x7FFF] = static_cast<u8>(value >> (8 * i));
            }
            owner.dirty = true;
            owner.frameDirty = true;
        }
        bool wideAccess() const override { return true; }
        VgaText& owner;
    };

    struct Ports : IoHandler {
        explicit Ports(VgaText& o) : owner(o) {}
        u64 read(Addr offset, int) override {
            switch (offset) {
                case 0: return owner.index;
                case 1: return owner.index < 18 ? owner.crtc[owner.index] : 0;
                case 4: return owner.modeReg;
                case 5: return owner.colorReg;
                case 6: owner.statusToggle ^= 0x09; return owner.statusToggle;
                default: return 0xFF;
            }
        }
        void write(Addr offset, int, u64 value) override {
            u8 v = static_cast<u8>(value);
            switch (offset) {
                case 0: owner.index = v & 0x1F; break;
                case 1:
                    if (owner.index < 18) {
                        owner.crtc[owner.index] = v;
                        owner.dirty = true;
            owner.frameDirty = true;
                    }
                    break;
                case 4: owner.modeReg = v; break;
                case 5: owner.colorReg = v; break;
                default: break;
            }
        }
        VgaText& owner;
    };

    static std::string toHex(u64 value) {
        char buffer[24];
        std::snprintf(buffer, sizeof buffer, "%X", static_cast<unsigned>(value));
        return buffer;
    }

    static bool blinkPhase() {
        using Clock = std::chrono::steady_clock;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
        return (ms / 500) % 2 == 0;
    }

    void renderFrame(bool phaseOn) {
        bool blinkEnabled = (modeReg & 0x20) != 0;
        for (int row = 0; row < 25; ++row) {
            for (int col = 0; col < 80; ++col) {
                u8 ch = cellChar(row, col);
                u8 attr = cellAttr(row, col);
                u32 fg = kPalette[attr & 0x0F];
                u32 bgIndex = (attr >> 4) & 0x0F;
                bool hidden = false;
                if (blinkEnabled) {
                    bgIndex &= 7;
                    hidden = (attr & 0x80) && !phaseOn;
                }
                u32 bg = kPalette[bgIndex];
                bool extend = ch >= 0xC0 && ch <= 0xDF;
                for (int line = 0; line < 16; ++line) {
                    u8 bits = hidden ? 0 : kFont16[ch * 16 + line];
                    u32* dst = &frame[(row * 16 + line) * 720 + col * 9];
                    for (int x = 0; x < 8; ++x) {
                        dst[x] = (bits & (0x80 >> x)) ? fg : bg;
                    }
                    dst[8] = (extend && (bits & 1)) ? fg : bg;
                }
            }
        }
        if (!(crtc[10] & 0x20) && phaseOn) {
            int cursor = static_cast<int>(((crtc[14] << 8) | crtc[15]) & 0x3FFF) - static_cast<int>(startWord());
            if (cursor >= 0 && cursor < 80 * 25) {
                int row = cursor / 80;
                int col = cursor % 80;
                int first = crtc[10] & 0x1F;
                int last = crtc[11] & 0x1F;
                u32 color = kPalette[cellAttr(row, col) & 0x0F];
                for (int line = first; line <= last && line < 16; ++line) {
                    u32* dst = &frame[(row * 16 + line) * 720 + col * 9];
                    for (int x = 0; x < 8; ++x) {
                        dst[x] = color;
                    }
                }
            }
        }
    }

    u16 startWord() const { return static_cast<u16>(((crtc[12] << 8) | crtc[13]) & 0x3FFF); }

    u8 cellChar(int row, int col) const {
        u32 word = (startWord() + row * 80 + col) & 0x3FFF;
        return ram[word * 2];
    }

    u8 cellAttr(int row, int col) const {
        u32 word = (startWord() + row * 80 + col) & 0x3FFF;
        return ram[word * 2 + 1];
    }

    void dumpText(std::ostream& out) const {
        for (int row = 0; row < 25; ++row) {
            std::string line;
            for (int col = 0; col < 80; ++col) {
                appendGlyph(line, cellChar(row, col));
            }
            size_t end = line.find_last_not_of(' ');
            out << (end == std::string::npos ? std::string() : line.substr(0, end + 1)) << "\n";
        }
    }

    void refresh(bool force) {
        if (!terminal || (!dirty && !force)) {
            return;
        }
        using Clock = std::chrono::steady_clock;
        auto now = Clock::now();
        if (!force && now - lastDraw < std::chrono::milliseconds(40)) {
            return;
        }
        lastDraw = now;
        dirty = false;
        std::string out;
        if (!active) {
            out += "\x1b[?1049h\x1b[2J";
            active = true;
        }
        out += "\x1b[?25l";
        static const int ansi[8] = {0, 4, 2, 6, 1, 5, 3, 7};
        for (int row = 0; row < 25; ++row) {
            out += "\x1b[" + std::to_string(row + 1) + ";1H";
            int lastAttr = -1;
            for (int col = 0; col < 80; ++col) {
                u8 attr = cellAttr(row, col);
                if (attr != lastAttr) {
                    int fg = ansi[attr & 7] + ((attr & 8) ? 90 : 30);
                    int bg = ansi[(attr >> 4) & 7] + ((attr & 0x80) ? 100 : 40);
                    out += "\x1b[" + std::to_string(fg) + ";" + std::to_string(bg) + "m";
                    lastAttr = attr;
                }
                appendGlyph(out, cellChar(row, col));
            }
        }
        u16 cursor = static_cast<u16>(((crtc[14] << 8) | crtc[15]) - startWord());
        int crow = cursor / 80;
        int ccol = cursor % 80;
        if (crow < 25) {
            out += "\x1b[0m\x1b[" + std::to_string(crow + 1) + ";" + std::to_string(ccol + 1) + "H\x1b[?25h";
        }
        Host::instance().writeOut(out.data(), out.size());
        Host::instance().flushOut();
    }

    Machine& machine;
    Memory memory;
    Ports ports;
    Addr base = 0xB8000;
    bool terminal = false;
    bool active = false;
    bool dirty = true;
    std::vector<u8> ram;
    std::vector<u32> frame;
    u64 generation = 0;
    bool frameDirty = true;
    bool lastPhase = false;
    u8 crtc[18] = {};
    u8 index = 0;
    u8 modeReg = 0x29;
    u8 colorReg = 0x30;
    u8 statusToggle = 0;
    std::chrono::steady_clock::time_point lastDraw{};
};

SOLME_REGISTER_DEVICE("vga-text", VgaText);

}
