#include <memory>
#include <sstream>
#include "cpu.hpp"
#include "machine.hpp"

namespace solme {

namespace {

class ToyCpu : public Cpu {
public:
    explicit ToyCpu(AddressSpace& memory) : mem(memory) {}

    void reset() override {
        pc = 0;
        accumulator = 0;
        stopped = false;
        count = 0;
    }

    u32 step() override {
        u8 op = static_cast<u8>(mem.read(pc++, 1));
        switch (op) {
            case 0x01: accumulator = static_cast<u8>(mem.read(pc++, 1)); break;
            case 0x02: mem.write(0xF000, 1, accumulator); break;
            default: stopped = true; break;
        }
        ++count;
        return 1;
    }

    bool idle() const override { return stopped; }
    u64 frequencyHz() const override { return 1000000; }
    u64 instructionCount() const override { return count; }

    void dumpState(std::ostream& out) const override {
        out << "PC=" << pc << " ACC=" << static_cast<int>(accumulator) << "\n";
    }

    std::string traceLine() const override {
        std::ostringstream line;
        line << "PC=" << pc << " ACC=" << static_cast<int>(accumulator);
        return line.str();
    }

    bool setEntry(const std::string& text) override {
        pc = std::stoull(text, nullptr, 0);
        return true;
    }

private:
    AddressSpace& mem;
    Addr pc = 0;
    u8 accumulator = 0;
    bool stopped = false;
    u64 count = 0;
};

class ConsoleOut : public Device, public IoHandler {
public:
    explicit ConsoleOut(Machine& machine) {
        backend = machine.openChar("stdio");
        machine.space("mem").mapHandler(0xF000, 0x1000, this);
    }
    u64 read(Addr, int) override { return 0; }
    void write(Addr, int, u64 value) override {
        backend->write(static_cast<u8>(value));
        backend->flush();
    }

private:
    CharBackend* backend;
};

class ConsoleDevice : public ConsoleOut {
public:
    ConsoleDevice(Machine& machine, const Props&) : ConsoleOut(machine) {}
};

SOLME_REGISTER_DEVICE("toy-console", ConsoleDevice);

void buildToy(Machine& machine, const Options&) {
    AddressSpace& mem = machine.createSpace("mem", 16, 12);
    mem.mapRam(0, 0xF000);
    machine.setCpu(std::make_unique<ToyCpu>(mem));
    machine.addDevice("toy-console", Props());
}

SOLME_REGISTER_MACHINE("toy", "toy", "tiny accumulator machine used to prove arch independence", true, buildToy);

}

}
