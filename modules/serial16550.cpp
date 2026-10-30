#include <deque>
#include "machine.hpp"

namespace solme {

class Serial16550 : public Device, public IoHandler {
public:
    Serial16550(Machine& m, const Props& props) : machine(m) {
        base = props.num("io", 0x3F8);
        irq = &machine.signal(props.str("irq", "irq4"));
        backend = machine.openChar(props.str("chardev", "null"));
        machine.space("io").mapHandler(base, 8, this);
        machine.addPoller([this]() { poll(); });
    }

    u64 read(Addr offset, int) override {
        switch (offset) {
            case 0:
                if (dlab) {
                    return divisor & 0xFF;
                }
                return takeByte();
            case 1: return dlab ? (divisor >> 8) : ier;
            case 2: {
                u8 value = iir();
                if ((value & 0x0F) == 0x02) {
                    thriPending = false;
                    updateIrq();
                }
                return value;
            }
            case 3: return lcr;
            case 4: return mcr;
            case 5: return static_cast<u8>(0x60 | (rx.empty() ? 0 : 1));
            case 6: return 0xB0;
            default: return scratch;
        }
    }

    void write(Addr offset, int, u64 value) override {
        u8 v = static_cast<u8>(value);
        switch (offset) {
            case 0:
                if (dlab) {
                    divisor = static_cast<u16>((divisor & 0xFF00) | v);
                } else {
                    backend->write(v);
                    backend->flush();
                    thriPending = true;
                    updateIrq();
                }
                break;
            case 1:
                if (dlab) {
                    divisor = static_cast<u16>((divisor & 0x00FF) | (v << 8));
                } else {
                    ier = v & 0x0F;
                    if (ier & 0x02) {
                        thriPending = true;
                    }
                    updateIrq();
                }
                break;
            case 3: lcr = v; dlab = (v & 0x80) != 0; break;
            case 4: mcr = v; break;
            case 7: scratch = v; break;
            default: break;
        }
    }

private:
    u8 takeByte() {
        if (rx.empty()) {
            return 0;
        }
        u8 b = rx.front();
        rx.pop_front();
        updateIrq();
        return b;
    }

    u8 iir() const {
        if ((ier & 0x01) && !rx.empty()) {
            return 0x04;
        }
        if ((ier & 0x02) && thriPending) {
            return 0x02;
        }
        return 0x01;
    }

    void updateIrq() {
        bool want = (iir() & 1) == 0 && (mcr & 0x08);
        irq->set(false);
        irq->set(want);
    }

    void poll() {
        u8 byte;
        bool got = false;
        while (rx.size() < 16 && backend->read(byte)) {
            rx.push_back(byte);
            got = true;
        }
        if (got) {
            updateIrq();
        }
    }

    Machine& machine;
    Signal* irq = nullptr;
    CharBackend* backend = nullptr;
    Addr base = 0x3F8;
    std::deque<u8> rx;
    u8 ier = 0;
    u8 lcr = 0;
    u8 mcr = 0;
    u8 scratch = 0;
    u16 divisor = 12;
    bool dlab = false;
    bool thriPending = false;
};

SOLME_REGISTER_DEVICE("serial16550", Serial16550);

}
