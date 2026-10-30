#include "machine.hpp"

namespace solme {

class DebugPorts : public Device, public IoHandler {
public:
    DebugPorts(Machine& m, const Props& props) : machine(m) {
        exitPort = props.num("exit-port", 0xF4);
        consolePort = props.num("console-port", 0xE9);
        console = machine.openChar(props.str("chardev", "null"));
        machine.space("io").mapHandler(exitPort, 1, &exitHandler);
        exitHandler.owner = this;
        machine.space("io").mapHandler(consolePort, 1, this);
    }

    u64 read(Addr, int) override { return 0xE9; }

    void write(Addr, int, u64 value) override {
        console->write(static_cast<u8>(value));
        console->flush();
    }

private:
    struct ExitHandler : IoHandler {
        DebugPorts* owner = nullptr;
        u64 read(Addr, int) override { return 0; }
        void write(Addr, int, u64 value) override {
            owner->machine.requestExit(static_cast<int>(value & 0xFF), "debug exit port");
        }
    };

    Machine& machine;
    Addr exitPort;
    Addr consolePort;
    CharBackend* console;
    ExitHandler exitHandler;
};

SOLME_REGISTER_DEVICE("debug", DebugPorts);

}
