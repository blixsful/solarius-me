#include <ctime>
#include "machine.hpp"

namespace solme {

class CmosRtc : public Device, public IoHandler {
public:
    CmosRtc(Machine& m, const Props& props) : machine(m) {
        u64 ramKb = props.num("base-kb", 640);
        u64 extKb = props.num("ext-kb", 0);
        nv[0x0B] = 0x02;
        nv[0x0D] = 0x80;
        nv[0x14] = 0x20;
        nv[0x15] = static_cast<u8>(ramKb);
        nv[0x16] = static_cast<u8>(ramKb >> 8);
        nv[0x17] = nv[0x30] = static_cast<u8>(extKb);
        nv[0x18] = nv[0x31] = static_cast<u8>(extKb >> 8);
        if (props.has("hd-type")) {
            nv[0x12] = 0xF0;
            nv[0x19] = static_cast<u8>(props.num("hd-type"));
        }
        machine.space("io").mapHandler(0x70, 2, this);
    }

    u64 read(Addr offset, int) override {
        if (offset == 0) {
            return 0xFF;
        }
        time_t now = std::time(nullptr);
        std::tm tmv;
        gmtime_r(&now, &tmv);
        auto bcd = [](int v) { return static_cast<u8>(((v / 10) << 4) | (v % 10)); };
        switch (index) {
            case 0x00: return bcd(tmv.tm_sec);
            case 0x02: return bcd(tmv.tm_min);
            case 0x04: return bcd(tmv.tm_hour);
            case 0x06: return bcd(tmv.tm_wday + 1);
            case 0x07: return bcd(tmv.tm_mday);
            case 0x08: return bcd(tmv.tm_mon + 1);
            case 0x09: return bcd(tmv.tm_year % 100);
            case 0x32: return bcd((tmv.tm_year + 1900) / 100);
            case 0x0A: return 0x26;
            default: return nv[index & 0x7F];
        }
    }

    void write(Addr offset, int, u64 value) override {
        if (offset == 0) {
            index = static_cast<u8>(value & 0x7F);
        } else {
            nv[index & 0x7F] = static_cast<u8>(value);
        }
    }

private:
    Machine& machine;
    u8 nv[128] = {};
    u8 index = 0;
};

SOLME_REGISTER_DEVICE("cmos-rtc", CmosRtc);

}
