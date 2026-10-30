#include <cstdio>
#include <cstdlib>
#include <string>
#include "machine.hpp"

namespace solme {

class Pic8259 : public Device, public IoHandler {
public:
    Pic8259(Machine& m, const Props& props) : machine(m) {
        base = props.num("io", 0x20);
        first = static_cast<int>(props.num("first-irq", 0));
        prefix = props.str("line-prefix", "irq");
        outName = props.str("out", "cpu.intr");
        vectorBase = static_cast<u8>(props.num("vector", first == 0 ? 0x08 : 0x70));
        imr = static_cast<u8>(props.num("mask", 0xFF));
        out = &machine.signal(outName);
        out->setAcknowledge([this]() { return acknowledge(); });
        for (int i = 0; i < 8; ++i) {
            machine.signal(prefix + std::to_string(first + i)).watch([this, i](bool level) { onLine(i, level); });
        }
        machine.space("io").mapHandler(base, 2, this);
    }

    u64 read(Addr offset, int) override {
        if (offset == 0) {
            return readIsr ? isr : irr;
        }
        return imr;
    }

    void write(Addr offset, int, u64 value) override {
        u8 v = static_cast<u8>(value);
        if (offset == 0) {
            if (v & 0x10) {
                icw1 = v;
                imr = 0;
                isr = 0;
                irr = 0;
                state = 1;
                readIsr = false;
            } else if ((v & 0x18) == 0x08) {
                if (v & 0x02) {
                    readIsr = (v & 0x01) != 0;
                }
            } else {
                int cmd = v >> 5;
                if (cmd == 1 || cmd == 5) {
                    for (int i = 0; i < 8; ++i) {
                        if (isr & (1 << i)) {
                            isr &= static_cast<u8>(~(1 << i));
                            break;
                        }
                    }
                } else if (cmd == 3 || cmd == 7) {
                    isr &= static_cast<u8>(~(1 << (v & 7)));
                }
            }
        } else if (state == 1) {
            vectorBase = v & 0xF8;
            state = (icw1 & 0x02) ? ((icw1 & 0x01) ? 3 : 0) : 2;
        } else if (state == 2) {
            state = (icw1 & 0x01) ? 3 : 0;
        } else if (state == 3) {
            autoEoi = (v & 0x02) != 0;
            state = 0;
        } else {
            imr = v;
        }
        update();
    }

private:
    void onLine(int line, bool level) {
        if (level) {
            irr |= static_cast<u8>(1 << line);
        } else if (icw1 & 0x08) {
            irr &= static_cast<u8>(~(1 << line));
        }
        update();
    }

    int candidate() const {
        int blocked = 8;
        for (int j = 0; j < 8; ++j) {
            if (isr & (1 << j)) {
                blocked = j;
                break;
            }
        }
        u8 pending = irr & static_cast<u8>(~imr);
        for (int i = 0; i < blocked; ++i) {
            if (pending & (1 << i)) {
                return i;
            }
        }
        return -1;
    }

    void update() { out->set(candidate() >= 0); }

    int acknowledge() {
        int line = candidate();
        if (line < 0) {
            return vectorBase + 7;
        }
        irr &= static_cast<u8>(~(1 << line));
        if (!autoEoi) {
            isr |= static_cast<u8>(1 << line);
        }
        update();
        int vector = machine.signal(prefix + std::to_string(first + line)).acknowledge();
        return vector >= 0 ? vector : vectorBase + line;
    }

    Machine& machine;
    Signal* out = nullptr;
    std::string prefix;
    std::string outName;
    Addr base = 0x20;
    int first = 0;
    u8 vectorBase = 8;
    u8 irr = 0;
    u8 isr = 0;
    u8 imr = 0xFF;
    u8 icw1 = 0;
    int state = 0;
    bool readIsr = false;
    bool autoEoi = false;
};

SOLME_REGISTER_DEVICE("pic8259", Pic8259);

}
