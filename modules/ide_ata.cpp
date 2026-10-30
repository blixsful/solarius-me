#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "machine.hpp"

namespace solme {

class IdeAta : public Device {
public:
    IdeAta(Machine& m, const Props& props) : machine(m), regs(*this), control(*this) {
        base = props.num("io", 0x1F0);
        controlPort = props.num("ctl", 0x3F6);
        irq = &machine.signal(props.str("irq", "irq14"));
        image = machine.drive(props.str("drive", "drive0"));
        if (image) {
            const Geometry& g = image->geometry();
            heads = g.heads;
            spt = g.sectors;
        }
        machine.space("io").mapHandler(base, 8, &regs);
        machine.space("io").mapHandler(controlPort, 1, &control);
        reset();
    }

    void reset() override {
        status = 0x50;
        error = 0x01;
        nIen = false;
        transferLeft = 0;
        bufferPos = 0;
        irq->set(false);
    }

private:
    struct Regs : IoHandler {
        explicit Regs(IdeAta& o) : owner(o) {}
        u64 read(Addr offset, int size) override { return owner.readReg(static_cast<int>(offset), size); }
        void write(Addr offset, int size, u64 value) override { owner.writeReg(static_cast<int>(offset), size, value); }
        bool wideAccess() const override { return true; }
        IdeAta& owner;
    };

    struct Control : IoHandler {
        explicit Control(IdeAta& o) : owner(o) {}
        u64 read(Addr, int) override { return owner.selected() ? owner.status : 0; }
        void write(Addr, int, u64 value) override { owner.writeControl(static_cast<u8>(value)); }
        IdeAta& owner;
    };

    bool selected() const { return image != nullptr && ((driveHead >> 4) & 1) == 0; }

    void raiseIrq() {
        if (!nIen) {
            irq->set(false);
            irq->set(true);
        }
    }

    void writeControl(u8 v) {
        bool wasReset = srst;
        srst = (v & 0x04) != 0;
        nIen = (v & 0x02) != 0;
        if (nIen) {
            irq->set(false);
        }
        if (wasReset && !srst) {
            status = 0x50;
            error = 0x01;
            sectorCount = 1;
            sectorNumber = 1;
            cylLow = cylHigh = 0;
            driveHead = 0;
            transferLeft = 0;
        }
    }

    u64 readReg(int reg, int size) {
        if (reg == 0) {
            return readData(size);
        }
        if (!selected()) {
            return reg == 7 ? 0 : 0xFF;
        }
        switch (reg) {
            case 1: return error;
            case 2: return sectorCount;
            case 3: return sectorNumber;
            case 4: return cylLow;
            case 5: return cylHigh;
            case 6: return driveHead;
            default: irq->set(false); return status;
        }
    }

    void writeReg(int reg, int size, u64 value) {
        u8 v = static_cast<u8>(value);
        switch (reg) {
            case 0: writeData(size, static_cast<u16>(value)); break;
            case 1: features = v; break;
            case 2: sectorCount = v; break;
            case 3: sectorNumber = v; break;
            case 4: cylLow = v; break;
            case 5: cylHigh = v; break;
            case 6: driveHead = v; break;
            case 7: command(v); break;
        }
    }

    u64 readData(int size) {
        if (!(status & 0x08) || !selected()) {
            return 0xFFFF;
        }
        u64 value = 0;
        int bytes = size == 1 ? 1 : 2;
        for (int i = 0; i < bytes; ++i) {
            value |= static_cast<u64>(buffer[bufferPos++]) << (8 * i);
        }
        if (bufferPos >= 512) {
            bufferPos = 0;
            if (transferLeft > 0) {
                --transferLeft;
            }
            if (transferLeft > 0 && mode == Mode::Read) {
                advanceLba();
                loadSector();
                raiseIrq();
            } else {
                status = 0x50;
            }
        }
        return value;
    }

    void writeData(int size, u16 value) {
        if (!(status & 0x08) || mode != Mode::Write || !selected()) {
            return;
        }
        int bytes = size == 1 ? 1 : 2;
        for (int i = 0; i < bytes; ++i) {
            buffer[bufferPos++] = static_cast<u8>(value >> (8 * i));
        }
        if (bufferPos >= 512) {
            bufferPos = 0;
            if (!image->write(currentLba(), 1, buffer)) {
                status = 0x51;
                error = 0x04;
                raiseIrq();
                return;
            }
            --transferLeft;
            if (transferLeft > 0) {
                advanceLba();
                status = 0x58;
            } else {
                status = 0x50;
            }
            raiseIrq();
        }
    }

    u64 currentLba() const {
        if (driveHead & 0x40) {
            return (static_cast<u64>(driveHead & 0x0F) << 24) | (static_cast<u64>(cylHigh) << 16) |
                   (static_cast<u64>(cylLow) << 8) | sectorNumber;
        }
        u64 cyl = (static_cast<u64>(cylHigh) << 8) | cylLow;
        u64 head = driveHead & 0x0F;
        return (cyl * heads + head) * spt + (sectorNumber - 1);
    }

    void advanceLba() {
        if (driveHead & 0x40) {
            u64 lba = currentLba() + 1;
            sectorNumber = static_cast<u8>(lba);
            cylLow = static_cast<u8>(lba >> 8);
            cylHigh = static_cast<u8>(lba >> 16);
            driveHead = static_cast<u8>((driveHead & 0xF0) | ((lba >> 24) & 0x0F));
            return;
        }
        if (sectorNumber < static_cast<int>(spt)) {
            ++sectorNumber;
            return;
        }
        sectorNumber = 1;
        u8 head = driveHead & 0x0F;
        if (static_cast<u32>(head) + 1 < heads) {
            driveHead = static_cast<u8>((driveHead & 0xF0) | (head + 1));
            return;
        }
        driveHead = static_cast<u8>(driveHead & 0xF0);
        u16 cyl = static_cast<u16>(((cylHigh << 8) | cylLow) + 1);
        cylLow = static_cast<u8>(cyl);
        cylHigh = static_cast<u8>(cyl >> 8);
    }

    bool loadSector() {
        if (!image->read(currentLba(), 1, buffer)) {
            status = 0x51;
            error = 0x04;
            transferLeft = 0;
            return false;
        }
        bufferPos = 0;
        status = 0x58;
        return true;
    }

    void putString(u16* words, int firstWord, int count, const char* text) {
        size_t len = std::strlen(text);
        for (int i = 0; i < count; ++i) {
            char a = (size_t(2 * i) < len) ? text[2 * i] : ' ';
            char b = (size_t(2 * i + 1) < len) ? text[2 * i + 1] : ' ';
            words[firstWord + i] = static_cast<u16>((static_cast<u8>(a) << 8) | static_cast<u8>(b));
        }
    }

    void identify() {
        u16 words[256] = {};
        const Geometry& g = image->geometry();
        u32 cyl = g.cylinders > 16383 ? 16383 : g.cylinders;
        u64 total = image->sectorCount();
        words[0] = 0x0040;
        words[1] = static_cast<u16>(cyl);
        words[3] = static_cast<u16>(g.heads);
        words[6] = static_cast<u16>(g.sectors);
        putString(words, 10, 10, "SOLME0001");
        putString(words, 23, 4, "1.0");
        putString(words, 27, 20, "SolME virtual disk");
        words[49] = 0x0200;
        words[53] = 0x0001;
        words[54] = static_cast<u16>(cyl);
        words[55] = static_cast<u16>(heads);
        words[56] = static_cast<u16>(spt);
        u32 chs = cyl * heads * spt;
        words[57] = static_cast<u16>(chs);
        words[58] = static_cast<u16>(chs >> 16);
        words[60] = static_cast<u16>(total);
        words[61] = static_cast<u16>(total >> 16);
        std::memcpy(buffer, words, 512);
        bufferPos = 0;
        status = 0x58;
        mode = Mode::Identify;
        transferLeft = 1;
    }

    void command(u8 cmd) {
        if (!selected()) {
            return;
        }
        irq->set(false);
        error = 0;
        switch (cmd) {
            case 0x20:
            case 0x21: {
                transferLeft = sectorCount == 0 ? 256 : sectorCount;
                mode = Mode::Read;
                if (loadSector()) {
                    raiseIrq();
                } else {
                    raiseIrq();
                }
                break;
            }
            case 0x30:
            case 0x31:
                transferLeft = sectorCount == 0 ? 256 : sectorCount;
                mode = Mode::Write;
                bufferPos = 0;
                status = 0x58;
                break;
            case 0xEC:
                identify();
                raiseIrq();
                break;
            case 0x91:
                heads = (driveHead & 0x0F) + 1;
                spt = sectorCount;
                status = 0x50;
                raiseIrq();
                break;
            case 0x40:
            case 0x41:
            case 0x70:
            case 0x90:
            case 0xE7:
            case 0xEF:
            case 0xC6:
            case 0xE0:
            case 0xE1:
            case 0xE2:
            case 0xE3:
                status = 0x50;
                if (cmd == 0x90) {
                    error = 0x01;
                }
                raiseIrq();
                break;
            default:
                if ((cmd & 0xF0) == 0x10) {
                    status = 0x50;
                    raiseIrq();
                } else {
                    status = 0x51;
                    error = 0x04;
                    raiseIrq();
                }
                break;
        }
    }

    enum class Mode { None, Read, Write, Identify };

    Machine& machine;
    Regs regs;
    Control control;
    Signal* irq = nullptr;
    BlockImage* image = nullptr;
    Addr base = 0x1F0;
    Addr controlPort = 0x3F6;
    u8 buffer[512] = {};
    int bufferPos = 0;
    int transferLeft = 0;
    Mode mode = Mode::None;
    u8 status = 0x50;
    u8 error = 0;
    u8 features = 0;
    u8 sectorCount = 1;
    u8 sectorNumber = 1;
    u8 cylLow = 0;
    u8 cylHigh = 0;
    u8 driveHead = 0;
    u32 heads = 16;
    u32 spt = 63;
    bool nIen = false;
    bool srst = false;
};

SOLME_REGISTER_DEVICE("ide-ata", IdeAta);

}
