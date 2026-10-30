#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "address_space.hpp"
#include "cpu8086.hpp"
#include "signal.hpp"

using namespace solme;

namespace {

struct Case {
    std::string name;
    u64 init[14];
    u64 fin[14];
    std::vector<std::pair<u32, u8>> initRam;
    std::vector<std::pair<u32, u8>> finRam;
};

const char* kRegNames[14] = {"ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags"};

bool readRam(std::istringstream& in, std::vector<std::pair<u32, u8>>& out) {
    size_t n;
    in >> n;
    for (size_t i = 0; i < n; ++i) {
        u64 a, v;
        in >> a >> v;
        out.emplace_back(static_cast<u32>(a), static_cast<u8>(v));
    }
    return true;
}

void loadRegs(Cpu8086& cpu, const u64* v) {
    cpu.r[0] = v[0];
    cpu.r[3] = v[1];
    cpu.r[1] = v[2];
    cpu.r[2] = v[3];
    cpu.s[1] = v[4];
    cpu.s[2] = v[5];
    cpu.s[3] = v[6];
    cpu.s[0] = v[7];
    cpu.r[4] = v[8];
    cpu.r[5] = v[9];
    cpu.r[6] = v[10];
    cpu.r[7] = v[11];
    cpu.ip = v[12];
    cpu.flags = static_cast<u16>(v[13]);
}

void readRegs(const Cpu8086& cpu, u64* v) {
    v[0] = cpu.r[0];
    v[1] = cpu.r[3];
    v[2] = cpu.r[1];
    v[3] = cpu.r[2];
    v[4] = cpu.s[1];
    v[5] = cpu.s[2];
    v[6] = cpu.s[3];
    v[7] = cpu.s[0];
    v[8] = cpu.r[4];
    v[9] = cpu.r[5];
    v[10] = cpu.r[6];
    v[11] = cpu.r[7];
    v[12] = cpu.ip;
    v[13] = cpu.flags;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: singlestep FILE.txt... [-v N]\n";
        return 2;
    }
    int verbose = 3;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" && i + 1 < argc) {
            verbose = std::atoi(argv[++i]);
        } else {
            files.push_back(a);
        }
    }

    AddressSpace mem(20, 12);
    AddressSpace io(16, 0);
    mem.mapRam(0, 0x100000);
    Signal intr;
    Cpu8086 cpu(mem, io, intr, 4770000);

    int totalPass = 0;
    int totalFail = 0;
    std::vector<std::string> failedFiles;

    for (const auto& file : files) {
        std::ifstream in(file);
        std::string line;
        u64 mask = 0xFFFF;
        int pass = 0;
        int fail = 0;
        int shown = 0;
        Case c;
        bool haveCase = false;
        std::string stem = file.substr(file.find_last_of('/') + 1);
        bool skipPushedFlags = stem.rfind("F6.6", 0) == 0 || stem.rfind("F6.7", 0) == 0 ||
                               stem.rfind("F7.6", 0) == 0 || stem.rfind("F7.7", 0) == 0;
        auto runCase = [&]() {
            for (auto& e : c.initRam) {
                mem.write8(e.first & 0xFFFFF, e.second);
            }
            loadRegs(cpu, c.init);
            cpu.reset();
            loadRegs(cpu, c.init);
            cpu.executeOne();
            u64 got[14];
            readRegs(cpu, got);
            std::string diff;
            for (int i = 0; i < 14; ++i) {
                u64 want = c.fin[i];
                u64 have = got[i];
                if (i == 13) {
                    want &= mask;
                    have &= mask;
                }
                if (want != have) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, " %s: want %04llX got %04llX;", kRegNames[i],
                                  (unsigned long long)want, (unsigned long long)have);
                    diff += buf;
                }
            }
            u32 skipLow = 0xFFFFFFFF;
            u32 skipHigh = 0xFFFFFFFF;
            if (skipPushedFlags) {
                u32 base = ((static_cast<u32>(c.fin[5]) << 4) + ((c.fin[8] + 4) & 0xFFFF)) & 0xFFFFF;
                skipLow = base;
                skipHigh = (base + 1) & 0xFFFFF;
            }
            for (auto& e : c.finRam) {
                u8 have = mem.read8(e.first & 0xFFFFF);
                if (have != e.second && (e.first & 0xFFFFF) != skipLow && (e.first & 0xFFFFF) != skipHigh) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, " ram[%05X]: want %02X got %02X;", e.first, e.second, have);
                    diff += buf;
                }
            }
            for (auto& e : c.initRam) {
                mem.write8(e.first & 0xFFFFF, 0);
            }
            for (auto& e : c.finRam) {
                mem.write8(e.first & 0xFFFFF, 0);
            }
            if (diff.empty()) {
                ++pass;
            } else {
                ++fail;
                if (shown < verbose) {
                    ++shown;
                    std::cout << "  FAIL " << c.name << diff << "\n";
                }
            }
        };

        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string tag;
            ls >> tag;
            if (tag == "M") {
                ls >> mask;
            } else if (tag == "T") {
                if (haveCase) {
                    runCase();
                }
                c = Case();
                haveCase = true;
                ls >> c.name;
            } else if (tag == "I") {
                for (int i = 0; i < 14; ++i) {
                    ls >> c.init[i];
                }
            } else if (tag == "IR") {
                readRam(ls, c.initRam);
            } else if (tag == "F") {
                for (int i = 0; i < 14; ++i) {
                    ls >> c.fin[i];
                }
            } else if (tag == "FR") {
                readRam(ls, c.finRam);
            }
        }
        if (haveCase) {
            runCase();
        }
        std::string base = file.substr(file.find_last_of('/') + 1);
        std::printf("%-10s pass %4d fail %4d\n", base.c_str(), pass, fail);
        totalPass += pass;
        totalFail += fail;
        if (fail) {
            failedFiles.push_back(base);
        }
    }
    std::printf("TOTAL pass %d fail %d, files failing: %zu\n", totalPass, totalFail, failedFiles.size());
    return totalFail ? 1 : 0;
}
