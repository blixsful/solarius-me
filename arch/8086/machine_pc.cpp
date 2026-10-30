#include <cstring>
#include <fstream>
#include <memory>
#include <vector>
#include "cpu8086.hpp"
#include "machine.hpp"
#include "solbios_rom.hpp"

namespace solme {

namespace {

void addDevice(Machine& m, const std::string& name, const std::string& spec) {
    Spec parsed = parseSpec(spec);
    m.addDevice(name, parsed.props);
}

void buildCommon(Machine& m, const Options& o, u64 defaultRam, const char* defaultCpu) {
    AddressSpace& mem = m.createSpace("mem", 20, 12);
    m.createSpace("io", 16, 0);
    u64 ram = o.opts.num("ram", defaultRam);
    if (ram > 0xA0000 && defaultRam <= 0xA0000) {
        ram = 0xA0000;
    }
    ram &= ~0xFFFULL;
    mem.mapRam(0, ram);
    u64 hz = o.opts.num("cpu-hz", 4772727);
    Cpu8086::Model model = o.opts.str("cpu", defaultCpu) == "80186" ? Cpu8086::Model::I80186 : Cpu8086::Model::I8086;
    m.setCpu(std::make_unique<Cpu8086>(mem, m.space("io"), m.signal("cpu.intr"), hz, model));
}

void buildBare(Machine& m, const Options& o) {
    buildCommon(m, o, 0x100000, "8086");
    addDevice(m, "debug", "chardev=" + o.opts.str("debugcon", "null"));
}

void buildPc(Machine& m, const Options& o) {
    buildCommon(m, o, 0xA0000, "80186");
    AddressSpace& mem = m.space("mem");

    std::vector<u8> rom(0x10000, 0xFF);
    if (o.opts.has("bios")) {
        std::ifstream in(o.opts.str("bios"), std::ios::binary);
        std::vector<u8> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (image.size() > rom.size()) {
            image.erase(image.begin(), image.end() - rom.size());
        }
        std::memcpy(rom.data() + rom.size() - image.size(), image.data(), image.size());
    } else {
        std::memcpy(rom.data() + rom.size() - kSolBiosRomSize, kSolBiosRom, kSolBiosRomSize);
    }
    mem.mapRom(0xF0000, rom.data(), rom.size());

    std::string display = o.opts.str("display", "none");
    std::string serial = o.opts.str("serial", "null");
    u64 ramKb = (o.opts.num("ram", 0xA0000) > 0xA0000 ? 0xA0000 : o.opts.num("ram", 0xA0000)) / 1024;

    addDevice(m, "pic8259", "io=0x20,first-irq=0,out=cpu.intr,vector=0x08");
    addDevice(m, "pic8259", "io=0xA0,first-irq=8,out=irq2,vector=0x70");
    addDevice(m, "pit8253", "io=0x40,irq=irq0");
    addDevice(m, "keyboard", "irq=irq1");
    addDevice(m, "serial16550", "io=0x3F8,irq=irq4,chardev=" + serial);
    addDevice(m, "vga-text", "display=" + display);
    addDevice(m, "ide-ata", "io=0x1F0,ctl=0x3F6,irq=irq14,drive=" + o.opts.str("hd", "drive0"));
    addDevice(m, "cmos-rtc", "base-kb=" + std::to_string(ramKb) + ",hd-type=47");
    addDevice(m, "regfile", "io=0x00,len=16");
    addDevice(m, "regfile", "io=0x80,len=16");
    addDevice(m, "regfile", "io=0xC0,len=32");
    addDevice(m, "regfile", "io=0x3F0,len=6,read-value=0x80");
    addDevice(m, "regfile", "io=0x3F7,len=1,read-value=0x00");
    addDevice(m, "regfile", "io=0x92,len=1");
    addDevice(m, "debug", "chardev=" + o.opts.str("debugcon", "null"));
}

}

SOLME_REGISTER_MACHINE("8086", "pc-at", "IBM PC/AT-class machine with SolBIOS, IDE disk, VGA text, 8259 x2", true, buildPc);
SOLME_REGISTER_MACHINE("8086", "bare", "flat 1 MB RAM, no firmware, no devices (use --load and --entry)", false, buildBare);

}
