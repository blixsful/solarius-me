#include <deque>
#include "machine.hpp"

namespace solme {

namespace {

struct KeyInfo {
    u8 scancode;
    bool shift;
};

KeyInfo asciiToKey(u8 c) {
    static const char* lower = "\x1b" "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char* upper = "\x1b" "!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    for (int i = 0; i < 58; ++i) {
        if (lower[i] && static_cast<u8>(lower[i]) == c) {
            return {static_cast<u8>(i + 1), false};
        }
    }
    for (int i = 0; i < 58; ++i) {
        if (upper[i] && static_cast<u8>(upper[i]) == c) {
            return {static_cast<u8>(i + 1), true};
        }
    }
    return {0, false};
}

}

class Keyboard : public Device, public IoHandler {
public:
    Keyboard(Machine& m, const Props& props) : machine(m) {
        irq = &machine.signal(props.str("irq", "irq1"));
        machine.space("io").mapHandler(0x60, 1, &dataPort);
        machine.space("io").mapHandler(0x64, 1, this);
        machine.space("io").mapHandler(0x61, 1, &portB);
        dataPort.owner = this;
        portB.owner = this;
        useHostInput = props.flag("host-input", true);
        machine.addPoller([this]() { poll(); });
    }

    u64 read(Addr, int) override {
        u8 status = 0x10;
        if (outputFull) {
            status |= 0x01;
        }
        return status;
    }

    void write(Addr, int, u64 value) override {
        u8 cmd = static_cast<u8>(value);
        switch (cmd) {
            case 0x20: respondController(commandByte); break;
            case 0x60: expecting = Expect::CommandByte; break;
            case 0xAA: respondController(0x55); break;
            case 0xAB: respondController(0x00); break;
            case 0xA7:
            case 0xA8:
            case 0xAD:
            case 0xAE: break;
            case 0xD1: expecting = Expect::OutputPort; break;
            case 0xFE: machine.requestExit(0, "keyboard controller reset"); break;
            default: break;
        }
    }

    void injectScancodes(std::initializer_list<u8> codes) {
        for (u8 c : codes) {
            queue.push_back(c);
        }
        deliver();
    }

private:
    enum class Expect { None, CommandByte, OutputPort, KbdParam };

    struct DataPort : IoHandler {
        Keyboard* owner = nullptr;
        u64 read(Addr, int) override { return owner->readData(); }
        void write(Addr, int, u64 value) override { owner->writeData(static_cast<u8>(value)); }
    };

    struct PortB : IoHandler {
        Keyboard* owner = nullptr;
        u8 value = 0;
        u8 toggle = 0;
        u64 read(Addr, int) override {
            toggle ^= 0x10;
            return static_cast<u8>((value & 0x0F) | toggle);
        }
        void write(Addr, int, u64 v) override { value = static_cast<u8>(v); }
    };

    u8 readData() {
        u8 value = outputBuffer;
        outputFull = false;
        irq->set(false);
        machine.scheduler().schedule(50000, [this]() { deliver(); });
        return value;
    }

    void writeData(u8 v) {
        if (expecting == Expect::CommandByte) {
            commandByte = v;
            expecting = Expect::None;
            return;
        }
        if (expecting == Expect::OutputPort) {
            expecting = Expect::None;
            return;
        }
        if (expecting == Expect::KbdParam) {
            expecting = Expect::None;
            queue.push_front(0xFA);
            deliver();
            return;
        }
        switch (v) {
            case 0xFF:
                queue.push_back(0xFA);
                queue.push_back(0xAA);
                break;
            case 0xED:
            case 0xF0:
            case 0xF3:
                expecting = Expect::KbdParam;
                queue.push_back(0xFA);
                break;
            case 0xF2:
                queue.push_back(0xFA);
                queue.push_back(0xAB);
                queue.push_back(0x83);
                break;
            case 0xEE: queue.push_back(0xEE); break;
            default: queue.push_back(0xFA); break;
        }
        deliver();
    }

    void respondController(u8 value) {
        queue.push_front(value);
        deliver();
    }

    void deliver() {
        if (outputFull || queue.empty()) {
            return;
        }
        outputBuffer = queue.front();
        queue.pop_front();
        outputFull = true;
        if (commandByte & 0x01) {
            irq->set(false);
            irq->set(true);
        }
    }

    void typeByte(u8 c) {
        if (c == 0x1B) {
            queue.push_back(0x01);
            queue.push_back(0x81);
            return;
        }
        if (c == 0x7F) {
            c = '\b';
        }
        if (c == '\n') {
            c = '\r';
        }
        if (c >= 1 && c <= 26 && c != '\r' && c != '\t' && c != '\b') {
            KeyInfo k = asciiToKey(static_cast<u8>('a' + c - 1));
            queue.push_back(0x1D);
            queue.push_back(k.scancode);
            queue.push_back(static_cast<u8>(k.scancode | 0x80));
            queue.push_back(0x9D);
            return;
        }
        KeyInfo k = asciiToKey(c);
        if (k.scancode == 0) {
            return;
        }
        if (k.shift) {
            queue.push_back(0x2A);
        }
        queue.push_back(k.scancode);
        queue.push_back(static_cast<u8>(k.scancode | 0x80));
        if (k.shift) {
            queue.push_back(0xAA);
        }
    }

    void poll() {
        auto& raw = machine.scancodes();
        while (!raw.empty() && queue.size() < 64) {
            queue.push_back(raw.front());
            raw.pop_front();
        }
        if (useHostInput) {
            for (CharBackend* in : machine.inputs()) {
                u8 b;
                while (queue.size() < 64 && in->read(b)) {
                    typeByte(b);
                }
            }
        }
        deliver();
    }

    Machine& machine;
    Signal* irq = nullptr;
    DataPort dataPort;
    PortB portB;
    std::deque<u8> queue;
    u8 outputBuffer = 0;
    bool outputFull = false;
    u8 commandByte = 0x45;
    Expect expecting = Expect::None;
    bool useHostInput = true;
};

SOLME_REGISTER_DEVICE("keyboard", Keyboard);

}
