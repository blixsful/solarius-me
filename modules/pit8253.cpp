#include "machine.hpp"

namespace solme {

class Pit8253 : public Device, public IoHandler {
public:
    Pit8253(Machine& m, const Props& props) : machine(m) {
        base = props.num("io", 0x40);
        clockHz = props.num("clock-hz", 1193182);
        irq = &machine.signal(props.str("irq", "irq0"));
        machine.space("io").mapHandler(base, 4, this);
    }

    u64 read(Addr offset, int) override {
        if (offset == 3) {
            return 0xFF;
        }
        Channel& ch = channels[offset];
        u16 value = ch.latched ? ch.latch : currentCount(static_cast<int>(offset));
        u8 mode = ch.access;
        u8 result;
        if (mode == 3) {
            result = ch.readHigh ? static_cast<u8>(value >> 8) : static_cast<u8>(value);
            ch.readHigh = !ch.readHigh;
            if (!ch.readHigh) {
                ch.latched = false;
            }
        } else if (mode == 2) {
            result = static_cast<u8>(value >> 8);
            ch.latched = false;
        } else {
            result = static_cast<u8>(value);
            ch.latched = false;
        }
        return result;
    }

    void write(Addr offset, int, u64 value) override {
        u8 v = static_cast<u8>(value);
        if (offset == 3) {
            int index = v >> 6;
            if (index == 3) {
                return;
            }
            Channel& ch = channels[index];
            int access = (v >> 4) & 3;
            if (access == 0) {
                ch.latch = currentCount(index);
                ch.latched = true;
                ch.readHigh = false;
                return;
            }
            ch.access = static_cast<u8>(access);
            ch.mode = (v >> 1) & 7;
            if (ch.mode >= 6) {
                ch.mode -= 4;
            }
            ch.writeHigh = false;
            ch.readHigh = false;
            ch.latched = false;
            if (index == 0) {
                stop();
            }
            return;
        }
        Channel& ch = channels[offset];
        if (ch.access == 3) {
            if (!ch.writeHigh) {
                ch.pendingLow = v;
                ch.writeHigh = true;
                return;
            }
            ch.writeHigh = false;
            load(static_cast<int>(offset), static_cast<u16>((v << 8) | ch.pendingLow));
        } else if (ch.access == 1) {
            load(static_cast<int>(offset), v);
        } else {
            load(static_cast<int>(offset), static_cast<u16>(v << 8));
        }
    }

    void reset() override { stop(); }

private:
    struct Channel {
        u8 access = 3;
        u8 mode = 3;
        bool writeHigh = false;
        bool readHigh = false;
        bool latched = false;
        u16 latch = 0;
        u8 pendingLow = 0;
        u32 reload = 65536;
        u64 loadedAtNs = 0;
    };

    u64 ticksBetween(u64 fromNs, u64 toNs) const {
        unsigned __int128 delta = toNs - fromNs;
        return static_cast<u64>(delta * clockHz / 1000000000ULL);
    }

    u64 nsForTicks(u64 ticks) const {
        unsigned __int128 value = static_cast<unsigned __int128>(ticks) * 1000000000ULL;
        return static_cast<u64>((value + clockHz - 1) / clockHz);
    }

    void stop() {
        if (eventId) {
            machine.scheduler().cancel(eventId);
            eventId = 0;
        }
    }

    void load(int index, u16 count) {
        Channel& ch = channels[index];
        ch.reload = count == 0 ? 65536 : count;
        ch.loadedAtNs = machine.scheduler().now();
        if (index == 0) {
            stop();
            schedule();
        }
    }

    void schedule() {
        Channel& ch = channels[0];
        eventId = machine.scheduler().schedule(nsForTicks(ch.reload), [this]() { fire(); });
    }

    void fire() {
        eventId = 0;
        Channel& ch = channels[0];
        irq->set(false);
        irq->set(true);
        ch.loadedAtNs = machine.scheduler().now();
        if (ch.mode == 2 || ch.mode == 3) {
            schedule();
        }
    }

    u16 currentCount(int index) {
        Channel& ch = channels[index];
        u64 elapsed = ticksBetween(ch.loadedAtNs, machine.scheduler().now());
        u32 period = ch.reload;
        if (ch.mode == 3) {
            u64 pos = elapsed % period;
            return static_cast<u16>((period - pos) & 0xFFFF);
        }
        u64 pos = elapsed % period;
        return static_cast<u16>((period - pos) & 0xFFFF);
    }

    Machine& machine;
    Signal* irq = nullptr;
    Addr base = 0x40;
    u64 clockHz = 1193182;
    Channel channels[3];
    u64 eventId = 0;
};

SOLME_REGISTER_DEVICE("pit8253", Pit8253);

}
