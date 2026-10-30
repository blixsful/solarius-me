#include "machine.hpp"

namespace solme {

class RegisterFile : public Device, public IoHandler {
public:
    RegisterFile(Machine& m, const Props& props) {
        Addr base = props.num("io", 0);
        Addr length = props.num("len", 16);
        fill = static_cast<u8>(props.num("fill", 0));
        for (auto& v : values) {
            v = fill;
        }
        readOverride = props.has("read-value");
        readValue = static_cast<u8>(props.num("read-value", 0));
        m.space("io").mapHandler(base, length, this);
    }

    u64 read(Addr offset, int) override { return readOverride ? readValue : values[offset & 0xFF]; }

    void write(Addr offset, int, u64 value) override { values[offset & 0xFF] = static_cast<u8>(value); }

private:
    u8 values[256] = {};
    u8 fill = 0;
    bool readOverride = false;
    u8 readValue = 0;
};

SOLME_REGISTER_DEVICE("regfile", RegisterFile);

}
