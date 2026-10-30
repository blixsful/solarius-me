#include "cpu8086.hpp"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace solme {

namespace {

inline bool evenParity(u8 value) { return (__builtin_popcount(value) & 1) == 0; }

}

Cpu8086::Cpu8086(AddressSpace& memory, AddressSpace& ports, Signal& intr, u64 hz, Model cpuModel)
    : mem(memory), io(ports), intrLine(intr), clockHz(hz), model(cpuModel) {}

void Cpu8086::reset() {
    r.fill(0);
    s = {0, 0xFFFF, 0, 0};
    ip = 0;
    flags = 0xF002;
    halted = false;
    inhibitInterrupt = false;
    instrCount = 0;
}

bool Cpu8086::setEntry(const std::string& text) {
    size_t colon = text.find(':');
    char* end = nullptr;
    if (colon == std::string::npos) {
        unsigned long value = std::strtoul(text.c_str(), &end, 0);
        if (end == text.c_str()) {
            return false;
        }
        s[CS] = static_cast<u16>(value >> 4);
        ip = static_cast<u16>(value & 0xF);
    } else {
        s[CS] = static_cast<u16>(std::strtoul(text.substr(0, colon).c_str(), nullptr, 0));
        ip = static_cast<u16>(std::strtoul(text.substr(colon + 1).c_str(), nullptr, 0));
    }
    s[DS] = s[ES] = s[SS] = s[CS];
    r[SP] = 0xFFFE;
    return true;
}

bool Cpu8086::idle() const { return halted && !((flags & IF) && intrLine.level()); }

u8 Cpu8086::fetch8() {
    u8 value = mem.read8(linear(s[CS], ip));
    ip = static_cast<u16>(ip + 1);
    return value;
}

u16 Cpu8086::fetch16() {
    u16 low = fetch8();
    u16 high = fetch8();
    return static_cast<u16>(low | (high << 8));
}

u8 Cpu8086::rd8(Seg seg, u16 off) { return mem.read8(linear(s[seg], off)); }

u16 Cpu8086::rd16(Seg seg, u16 off) {
    u16 low = mem.read8(linear(s[seg], off));
    u16 high = mem.read8(linear(s[seg], static_cast<u16>(off + 1)));
    return static_cast<u16>(low | (high << 8));
}

void Cpu8086::wr8(Seg seg, u16 off, u8 value) { mem.write8(linear(s[seg], off), value); }

void Cpu8086::wr16(Seg seg, u16 off, u16 value) {
    mem.write8(linear(s[seg], off), static_cast<u8>(value));
    mem.write8(linear(s[seg], static_cast<u16>(off + 1)), static_cast<u8>(value >> 8));
}

void Cpu8086::push(u16 value) {
    r[SP] = static_cast<u16>(r[SP] - 2);
    wr16(SS, r[SP], value);
}

u16 Cpu8086::pop() {
    u16 value = rd16(SS, r[SP]);
    r[SP] = static_cast<u16>(r[SP] + 2);
    return value;
}

void Cpu8086::setReg8(int index, u8 value) {
    if (index < 4) {
        r[index] = static_cast<u16>((r[index] & 0xFF00) | value);
    } else {
        r[index - 4] = static_cast<u16>((r[index - 4] & 0x00FF) | (static_cast<u16>(value) << 8));
    }
}

Cpu8086::Operand Cpu8086::decode(u8 modrm) {
    modrmByte = modrm;
    Operand op;
    u8 mod = modrm >> 6;
    u8 rm = modrm & 7;
    if (mod == 3) {
        op.isReg = true;
        op.reg = rm;
        return op;
    }
    u16 base = 0;
    Seg def = DS;
    switch (rm) {
        case 0: base = static_cast<u16>(r[BX] + r[SI]); break;
        case 1: base = static_cast<u16>(r[BX] + r[DI]); break;
        case 2: base = static_cast<u16>(r[BP] + r[SI]); def = SS; break;
        case 3: base = static_cast<u16>(r[BP] + r[DI]); def = SS; break;
        case 4: base = r[SI]; break;
        case 5: base = r[DI]; break;
        case 6:
            if (mod == 0) {
                base = fetch16();
            } else {
                base = r[BP];
                def = SS;
            }
            break;
        case 7: base = r[BX]; break;
    }
    if (mod == 1) {
        base = static_cast<u16>(base + static_cast<u16>(static_cast<i16>(static_cast<i8>(fetch8()))));
    } else if (mod == 2) {
        base = static_cast<u16>(base + fetch16());
    }
    op.offset = base;
    op.seg = segOverride >= 0 ? static_cast<Seg>(segOverride) : def;
    cycles += 6;
    return op;
}

u16 Cpu8086::readOp(const Operand& op, bool word) {
    if (op.isReg) {
        return word ? r[op.reg] : reg8(op.reg);
    }
    cycles += 4;
    return word ? rd16(op.seg, op.offset) : rd8(op.seg, op.offset);
}

void Cpu8086::writeOp(const Operand& op, bool word, u16 value) {
    if (op.isReg) {
        if (word) {
            r[op.reg] = value;
        } else {
            setReg8(op.reg, static_cast<u8>(value));
        }
        return;
    }
    cycles += 4;
    if (word) {
        wr16(op.seg, op.offset, value);
    } else {
        wr8(op.seg, op.offset, static_cast<u8>(value));
    }
}

void Cpu8086::setSzp(u16 result, bool word) {
    u16 mask = word ? 0xFFFF : 0xFF;
    u16 sign = word ? 0x8000 : 0x80;
    setFlag(ZF, (result & mask) == 0);
    setFlag(SF, (result & sign) != 0);
    setFlag(PF, evenParity(static_cast<u8>(result)));
}

u16 Cpu8086::alu(int op, u16 a, u16 b, bool word) {
    u32 mask = word ? 0xFFFF : 0xFF;
    u32 sign = word ? 0x8000 : 0x80;
    u32 carryIn = getFlag(CF) ? 1 : 0;
    u32 res = 0;
    switch (op) {
        case 0:
        case 2: {
            u32 cin = op == 2 ? carryIn : 0;
            res = static_cast<u32>(a) + b + cin;
            setFlag(CF, res > mask);
            setFlag(AF, ((a ^ b ^ res) & 0x10) != 0);
            setFlag(OF, ((a ^ res) & (b ^ res) & sign) != 0);
            break;
        }
        case 3:
        case 5:
        case 7: {
            u32 cin = op == 3 ? carryIn : 0;
            res = static_cast<u32>(a) - b - cin;
            setFlag(CF, static_cast<u32>(a) < static_cast<u32>(b) + cin);
            setFlag(AF, ((a ^ b ^ res) & 0x10) != 0);
            setFlag(OF, ((a ^ b) & (a ^ res) & sign) != 0);
            break;
        }
        case 1: res = a | b; setFlag(CF, false); setFlag(OF, false); setFlag(AF, false); break;
        case 4: res = a & b; setFlag(CF, false); setFlag(OF, false); setFlag(AF, false); break;
        case 6: res = a ^ b; setFlag(CF, false); setFlag(OF, false); setFlag(AF, false); break;
    }
    res &= mask;
    setSzp(static_cast<u16>(res), word);
    return static_cast<u16>(res);
}

u16 Cpu8086::incdec(u16 value, bool dec, bool word) {
    u32 mask = word ? 0xFFFF : 0xFF;
    u32 sign = word ? 0x8000 : 0x80;
    u32 res = (dec ? value - 1u : value + 1u) & mask;
    setFlag(AF, ((value ^ res) & 0x10) != 0);
    setFlag(OF, dec ? (res == sign - 1) : (res == sign));
    setSzp(static_cast<u16>(res), word);
    return static_cast<u16>(res);
}

u16 Cpu8086::shiftRotate(int op, u16 value, unsigned count, bool word) {
    if (count == 0) {
        return value;
    }
    u32 mask = word ? 0xFFFF : 0xFF;
    u32 sign = word ? 0x8000 : 0x80;
    u32 v = value & mask;
    if (op == 6) {
        setFlag(CF, false);
        setFlag(OF, false);
        setFlag(AF, false);
        setSzp(static_cast<u16>(mask), word);
        return static_cast<u16>(mask);
    }
    bool cf = getFlag(CF);
    bool of = getFlag(OF);
    for (unsigned i = 0; i < count; ++i) {
        u32 before = v;
        switch (op) {
            case 0:
                cf = (v & sign) != 0;
                v = ((v << 1) | (cf ? 1 : 0)) & mask;
                of = ((v & sign) != 0) != cf;
                break;
            case 1:
                cf = (v & 1) != 0;
                v = (v >> 1) | (cf ? sign : 0);
                of = ((v & sign) != 0) != ((v & (sign >> 1)) != 0);
                break;
            case 2: {
                bool next = (v & sign) != 0;
                v = ((v << 1) | (cf ? 1 : 0)) & mask;
                cf = next;
                of = ((v & sign) != 0) != cf;
                break;
            }
            case 3: {
                bool next = (v & 1) != 0;
                v = (v >> 1) | (cf ? sign : 0);
                cf = next;
                of = ((v & sign) != 0) != ((v & (sign >> 1)) != 0);
                break;
            }
            case 4:
                cf = (v & sign) != 0;
                v = (v << 1) & mask;
                of = ((v & sign) != 0) != cf;
                break;
            case 5:
                cf = (v & 1) != 0;
                v >>= 1;
                of = (before & sign) != 0;
                break;
            case 7:
                cf = (v & 1) != 0;
                v = (v >> 1) | (v & sign);
                of = false;
                break;
        }
    }
    setFlag(CF, cf);
    setFlag(OF, of);
    if (op >= 4) {
        setSzp(static_cast<u16>(v), word);
    }
    return static_cast<u16>(v);
}

bool Cpu8086::condition(int code) const {
    bool value = false;
    switch (code >> 1) {
        case 0: value = getFlag(OF); break;
        case 1: value = getFlag(CF); break;
        case 2: value = getFlag(ZF); break;
        case 3: value = getFlag(CF) || getFlag(ZF); break;
        case 4: value = getFlag(SF); break;
        case 5: value = getFlag(PF); break;
        case 6: value = getFlag(SF) != getFlag(OF); break;
        case 7: value = getFlag(ZF) || (getFlag(SF) != getFlag(OF)); break;
    }
    return (code & 1) ? !value : value;
}

void Cpu8086::interrupt(u8 vector) {
    push(getFlags());
    push(s[CS]);
    push(ip);
    setFlag(IF, false);
    setFlag(TF, false);
    ip = mem.read16(static_cast<Addr>(vector) * 4);
    s[CS] = mem.read16(static_cast<Addr>(vector) * 4 + 2);
}

void Cpu8086::jumpRel(i16 displacement, bool taken) {
    if (taken) {
        ip = static_cast<u16>(ip + displacement);
        cycles += 8;
    }
}

void Cpu8086::doMul(bool word, bool isSigned, u16 multiplier) {
    if (!word) {
        if (isSigned) {
            i16 result = static_cast<i16>(static_cast<i8>(r[AX] & 0xFF)) * static_cast<i16>(static_cast<i8>(multiplier));
            r[AX] = static_cast<u16>(result);
            bool overflow = result != static_cast<i16>(static_cast<i8>(result & 0xFF));
            setFlag(CF, overflow);
            setFlag(OF, overflow);
        } else {
            u16 result = static_cast<u16>((r[AX] & 0xFF) * (multiplier & 0xFF));
            r[AX] = result;
            setFlag(CF, (result >> 8) != 0);
            setFlag(OF, (result >> 8) != 0);
        }
        cycles += 70;
        return;
    }
    if (isSigned) {
        i32 result = static_cast<i32>(static_cast<i16>(r[AX])) * static_cast<i32>(static_cast<i16>(multiplier));
        r[AX] = static_cast<u16>(result);
        r[DX] = static_cast<u16>(static_cast<u32>(result) >> 16);
        bool overflow = result != static_cast<i32>(static_cast<i16>(result & 0xFFFF));
        setFlag(CF, overflow);
        setFlag(OF, overflow);
    } else {
        u32 result = static_cast<u32>(r[AX]) * multiplier;
        r[AX] = static_cast<u16>(result);
        r[DX] = static_cast<u16>(result >> 16);
        setFlag(CF, r[DX] != 0);
        setFlag(OF, r[DX] != 0);
    }
    cycles += 120;
}

void Cpu8086::doDiv(bool word, bool isSigned, u16 divisor) {
    cycles += 100;
    if (!word) {
        if (isSigned) {
            i16 dividend = static_cast<i16>(r[AX]);
            i8 d = static_cast<i8>(divisor);
            if (d == 0) {
                interrupt(0);
                return;
            }
            i16 q = static_cast<i16>(dividend / d);
            i16 rem = static_cast<i16>(dividend % d);
            if (repPrefix) {
                q = static_cast<i16>(-q);
            }
            if (q > 127 || q < -127) {
                interrupt(0);
                return;
            }
            r[AX] = static_cast<u16>((static_cast<u8>(rem) << 8) | static_cast<u8>(q));
        } else {
            u16 dividend = r[AX];
            u8 d = static_cast<u8>(divisor);
            if (d == 0 || dividend / d > 0xFF) {
                interrupt(0);
                return;
            }
            r[AX] = static_cast<u16>(((dividend % d) << 8) | (dividend / d));
        }
        return;
    }
    if (isSigned) {
        i32 dividend = static_cast<i32>((static_cast<u32>(r[DX]) << 16) | r[AX]);
        i16 d = static_cast<i16>(divisor);
        if (d == 0) {
            interrupt(0);
            return;
        }
        i32 q = dividend / d;
        i32 rem = dividend % d;
        if (repPrefix) {
            q = -q;
        }
        if (dividend == static_cast<i32>(0x80000000) && d == -1) {
            interrupt(0);
            return;
        }
        if (q > 32767 || q < -32767) {
            interrupt(0);
            return;
        }
        r[AX] = static_cast<u16>(q);
        r[DX] = static_cast<u16>(rem);
    } else {
        u32 dividend = (static_cast<u32>(r[DX]) << 16) | r[AX];
        if (divisor == 0 || dividend / divisor > 0xFFFF) {
            interrupt(0);
            return;
        }
        r[AX] = static_cast<u16>(dividend / divisor);
        r[DX] = static_cast<u16>(dividend % divisor);
    }
}

void Cpu8086::group3(bool word) {
    u8 modrm = fetch8();
    Operand e = decode(modrm);
    int sub = (modrm >> 3) & 7;
    u16 value = readOp(e, word);
    switch (sub) {
        case 0:
        case 1: {
            u16 imm = word ? fetch16() : fetch8();
            alu(4, value, imm, word);
            break;
        }
        case 2: writeOp(e, word, static_cast<u16>(~value)); break;
        case 3: {
            u16 mask = word ? 0xFFFF : 0xFF;
            u16 result = alu(5, 0, value, word);
            setFlag(CF, (value & mask) != 0);
            writeOp(e, word, result);
            break;
        }
        case 4: doMul(word, false, value); break;
        case 5: doMul(word, true, value); break;
        case 6: doDiv(word, false, value); break;
        case 7: doDiv(word, true, value); break;
    }
}

void Cpu8086::group45(bool word, u8 opcode) {
    u8 modrm = fetch8();
    Operand e = decode(modrm);
    int sub = (modrm >> 3) & 7;
    if (opcode == 0xFE) {
        u16 value = readOp(e, false);
        if (sub <= 1) {
            writeOp(e, false, incdec(value, sub == 1, false));
        }
        return;
    }
    (void)word;
    switch (sub) {
        case 0:
        case 1: writeOp(e, true, incdec(readOp(e, true), sub == 1, true)); break;
        case 2: {
            u16 target = readOp(e, true);
            push(ip);
            ip = target;
            cycles += 12;
            break;
        }
        case 3: {
            if (e.isReg) {
                break;
            }
            u16 newIp = rd16(e.seg, e.offset);
            u16 newCs = rd16(e.seg, static_cast<u16>(e.offset + 2));
            push(s[CS]);
            push(ip);
            ip = newIp;
            s[CS] = newCs;
            cycles += 20;
            break;
        }
        case 4: ip = readOp(e, true); cycles += 8; break;
        case 5: {
            if (e.isReg) {
                break;
            }
            u16 newIp = rd16(e.seg, e.offset);
            u16 newCs = rd16(e.seg, static_cast<u16>(e.offset + 2));
            ip = newIp;
            s[CS] = newCs;
            cycles += 12;
            break;
        }
        default: {
            u16 value = readOp(e, true);
            if (e.isReg && e.reg == SP) {
                value = static_cast<u16>(value - 2);
            }
            push(value);
            break;
        }
    }
}

void Cpu8086::stringOp(u8 opcode, int rep) {
    bool word = (opcode & 1) != 0;
    u16 step = word ? 2 : 1;
    u16 delta = getFlag(DF) ? static_cast<u16>(-static_cast<i16>(step)) : step;
    Seg srcSeg = segOverride >= 0 ? static_cast<Seg>(segOverride) : DS;
    bool compares = (opcode & 0xFE) == 0xA6 || (opcode & 0xFE) == 0xAE;
    int ioSize = word ? 2 : 1;

    for (int n = 0; n < 256; ++n) {
        if (rep && r[CX] == 0) {
            return;
        }
        switch (opcode & 0xFE) {
            case 0x6C: {
                u16 v = static_cast<u16>(io.read(r[DX], ioSize));
                if (word) {
                    wr16(ES, r[DI], v);
                } else {
                    wr8(ES, r[DI], static_cast<u8>(v));
                }
                r[DI] = static_cast<u16>(r[DI] + delta);
                break;
            }
            case 0x6E: {
                u16 v = word ? rd16(srcSeg, r[SI]) : rd8(srcSeg, r[SI]);
                io.write(r[DX], ioSize, v);
                r[SI] = static_cast<u16>(r[SI] + delta);
                break;
            }
            case 0xA4: {
                u16 v = word ? rd16(srcSeg, r[SI]) : rd8(srcSeg, r[SI]);
                if (word) {
                    wr16(ES, r[DI], v);
                } else {
                    wr8(ES, r[DI], static_cast<u8>(v));
                }
                r[SI] = static_cast<u16>(r[SI] + delta);
                r[DI] = static_cast<u16>(r[DI] + delta);
                break;
            }
            case 0xA6: {
                u16 a = word ? rd16(srcSeg, r[SI]) : rd8(srcSeg, r[SI]);
                u16 b = word ? rd16(ES, r[DI]) : rd8(ES, r[DI]);
                alu(7, a, b, word);
                r[SI] = static_cast<u16>(r[SI] + delta);
                r[DI] = static_cast<u16>(r[DI] + delta);
                break;
            }
            case 0xAA: {
                if (word) {
                    wr16(ES, r[DI], r[AX]);
                } else {
                    wr8(ES, r[DI], static_cast<u8>(r[AX]));
                }
                r[DI] = static_cast<u16>(r[DI] + delta);
                break;
            }
            case 0xAC: {
                if (word) {
                    r[AX] = rd16(srcSeg, r[SI]);
                } else {
                    setReg8(0, rd8(srcSeg, r[SI]));
                }
                r[SI] = static_cast<u16>(r[SI] + delta);
                break;
            }
            case 0xAE: {
                u16 b = word ? rd16(ES, r[DI]) : rd8(ES, r[DI]);
                alu(7, word ? r[AX] : (r[AX] & 0xFF), b, word);
                r[DI] = static_cast<u16>(r[DI] + delta);
                break;
            }
        }
        cycles += 12;
        if (!rep) {
            return;
        }
        r[CX] = static_cast<u16>(r[CX] - 1);
        if (r[CX] == 0) {
            return;
        }
        if (compares) {
            bool zf = getFlag(ZF);
            if ((rep == 1 && !zf) || (rep == 2 && zf)) {
                return;
            }
        }
        if (intrLine.level() && getFlag(IF)) {
            ip = instrStart;
            return;
        }
    }
    ip = instrStart;
}

bool Cpu8086::exec186(u8 op) {
    auto undefined = [&]() {
        ip = instrStart;
        interrupt(6);
    };
    switch (op) {
        case 0x0F: undefined(); return true;
        case 0x60: {
            u16 original = r[SP];
            push(r[AX]);
            push(r[CX]);
            push(r[DX]);
            push(r[BX]);
            push(original);
            push(r[BP]);
            push(r[SI]);
            push(r[DI]);
            return true;
        }
        case 0x61: {
            r[DI] = pop();
            r[SI] = pop();
            r[BP] = pop();
            r[SP] = static_cast<u16>(r[SP] + 2);
            r[BX] = pop();
            r[DX] = pop();
            r[CX] = pop();
            r[AX] = pop();
            return true;
        }
        case 0x62: {
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            if (e.isReg) {
                undefined();
                return true;
            }
            i16 index = static_cast<i16>(r[(modrm >> 3) & 7]);
            i16 low = static_cast<i16>(rd16(e.seg, e.offset));
            i16 high = static_cast<i16>(rd16(e.seg, static_cast<u16>(e.offset + 2)));
            if (index < low || index > high) {
                ip = instrStart;
                interrupt(5);
            }
            return true;
        }
        case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
            undefined();
            return true;
        case 0x68: push(fetch16()); return true;
        case 0x6A: push(static_cast<u16>(static_cast<i16>(static_cast<i8>(fetch8())))); return true;
        case 0x69:
        case 0x6B: {
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            u16 imm = op == 0x69 ? fetch16() : static_cast<u16>(static_cast<i16>(static_cast<i8>(fetch8())));
            i32 product = static_cast<i32>(static_cast<i16>(readOp(e, true))) * static_cast<i32>(static_cast<i16>(imm));
            r[(modrm >> 3) & 7] = static_cast<u16>(product);
            bool overflow = product != static_cast<i32>(static_cast<i16>(product));
            setFlag(CF, overflow);
            setFlag(OF, overflow);
            cycles += 30;
            return true;
        }
        case 0x6C: case 0x6D: case 0x6E: case 0x6F:
            stringOp(op, repPrefixActive);
            return true;
        case 0xC0:
        case 0xC1: {
            bool word = (op & 1) != 0;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            unsigned count = fetch8() & 0x1F;
            u16 v = readOp(e, word);
            writeOp(e, word, shiftRotate((modrm >> 3) & 7, v, count, word));
            return true;
        }
        case 0xC8: {
            u16 size = fetch16();
            unsigned level = fetch8() & 0x1F;
            push(r[BP]);
            u16 frame = r[SP];
            if (level > 0) {
                for (unsigned i = 1; i < level; ++i) {
                    r[BP] = static_cast<u16>(r[BP] - 2);
                    push(rd16(SS, r[BP]));
                }
                push(frame);
            }
            r[BP] = frame;
            r[SP] = static_cast<u16>(r[SP] - size);
            return true;
        }
        case 0xC9:
            r[SP] = r[BP];
            r[BP] = pop();
            return true;
        default: return false;
    }
}

void Cpu8086::executeOne() {
    instrStart = ip;
    segOverride = -1;
    int rep = 0;
    repPrefix = 0;
    u8 op;
    for (;;) {
        op = fetch8();
        if (op == 0x26) { segOverride = ES; continue; }
        if (op == 0x2E) { segOverride = CS; continue; }
        if (op == 0x36) { segOverride = SS; continue; }
        if (op == 0x3E) { segOverride = DS; continue; }
        if (op == 0xF0 || op == 0xF1) { continue; }
        if (op == 0xF2) { rep = 2; repPrefix = 2; continue; }
        if (op == 0xF3) { rep = 1; repPrefix = 1; continue; }
        break;
    }
    cycles += 4;
    repPrefixActive = rep;
    if (model == Model::I80186 && exec186(op)) {
        return;
    }

    if (op < 0x40 && (op & 7) < 6) {
        int aluOp = (op >> 3) & 7;
        int form = op & 7;
        bool word = (form & 1) != 0;
        if (form < 4) {
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            int reg = (modrm >> 3) & 7;
            if (form < 2) {
                u16 a = readOp(e, word);
                u16 b = word ? r[reg] : reg8(reg);
                u16 res = alu(aluOp, a, b, word);
                if (aluOp != 7) {
                    writeOp(e, word, res);
                }
            } else {
                u16 a = word ? r[reg] : reg8(reg);
                u16 b = readOp(e, word);
                u16 res = alu(aluOp, a, b, word);
                if (aluOp != 7) {
                    if (word) {
                        r[reg] = res;
                    } else {
                        setReg8(reg, static_cast<u8>(res));
                    }
                }
            }
        } else if (form == 4) {
            u16 res = alu(aluOp, r[AX] & 0xFF, fetch8(), false);
            if (aluOp != 7) {
                setReg8(0, static_cast<u8>(res));
            }
        } else {
            u16 res = alu(aluOp, r[AX], fetch16(), true);
            if (aluOp != 7) {
                r[AX] = res;
            }
        }
        return;
    }

    switch (op) {
        case 0x06: push(s[ES]); return;
        case 0x0E: push(s[CS]); return;
        case 0x16: push(s[SS]); return;
        case 0x1E: push(s[DS]); return;
        case 0x07: s[ES] = pop(); return;
        case 0x0F: s[CS] = pop(); return;
        case 0x17: s[SS] = pop(); inhibitInterrupt = true; return;
        case 0x1F: s[DS] = pop(); return;
        case 0x27: {
            u8 al = static_cast<u8>(r[AX]);
            u8 old = al;
            bool oldCf = getFlag(CF);
            bool oldAf = getFlag(AF);
            if ((al & 0x0F) > 9 || getFlag(AF)) {
                u16 sum = static_cast<u16>(al + 6);
                al = static_cast<u8>(sum);
                setFlag(CF, oldCf || sum > 0xFF);
                setFlag(AF, true);
            } else {
                setFlag(AF, false);
            }
            if (old > (oldAf ? 0x9F : 0x99) || oldCf) {
                al = static_cast<u8>(al + 0x60);
                setFlag(CF, true);
            } else {
                setFlag(CF, false);
            }
            setReg8(0, al);
            setSzp(al, false);
            return;
        }
        case 0x2F: {
            u8 al = static_cast<u8>(r[AX]);
            u8 old = al;
            bool oldCf = getFlag(CF);
            bool oldAf = getFlag(AF);
            if ((al & 0x0F) > 9 || getFlag(AF)) {
                setFlag(CF, oldCf || al < 6);
                al = static_cast<u8>(al - 6);
                setFlag(AF, true);
            } else {
                setFlag(AF, false);
            }
            if (old > (oldAf ? 0x9F : 0x99) || oldCf) {
                al = static_cast<u8>(al - 0x60);
                setFlag(CF, true);
            } else {
                setFlag(CF, false);
            }
            setReg8(0, al);
            setSzp(al, false);
            return;
        }
        case 0x37: {
            u8 al = static_cast<u8>(r[AX]);
            u8 ah = static_cast<u8>(r[AX] >> 8);
            if ((al & 0x0F) > 9 || getFlag(AF)) {
                al = static_cast<u8>(al + 6);
                ah = static_cast<u8>(ah + 1);
                setFlag(AF, true);
                setFlag(CF, true);
            } else {
                setFlag(AF, false);
                setFlag(CF, false);
            }
            r[AX] = static_cast<u16>(((ah) << 8) | (al & 0x0F));
            return;
        }
        case 0x3F: {
            u8 al = static_cast<u8>(r[AX]);
            u8 ah = static_cast<u8>(r[AX] >> 8);
            if ((al & 0x0F) > 9 || getFlag(AF)) {
                al = static_cast<u8>(al - 6);
                ah = static_cast<u8>(ah - 1);
                setFlag(AF, true);
                setFlag(CF, true);
            } else {
                setFlag(AF, false);
                setFlag(CF, false);
            }
            r[AX] = static_cast<u16>(((ah) << 8) | (al & 0x0F));
            return;
        }
        default: break;
    }

    if (op >= 0x40 && op <= 0x47) { r[op - 0x40] = incdec(r[op - 0x40], false, true); return; }
    if (op >= 0x48 && op <= 0x4F) { r[op - 0x48] = incdec(r[op - 0x48], true, true); return; }
    if (op >= 0x50 && op <= 0x57) {
        int reg = op - 0x50;
        push(reg == SP ? static_cast<u16>(r[SP] - 2) : r[reg]);
        return;
    }
    if (op >= 0x58 && op <= 0x5F) { r[op - 0x58] = pop(); return; }
    if ((op >= 0x60 && op <= 0x7F)) {
        i8 disp = static_cast<i8>(fetch8());
        jumpRel(disp, condition(op & 0x0F));
        return;
    }
    if (op >= 0x91 && op <= 0x97) {
        u16 tmp = r[AX];
        r[AX] = r[op - 0x90];
        r[op - 0x90] = tmp;
        return;
    }
    if (op >= 0xB0 && op <= 0xB7) { setReg8(op - 0xB0, fetch8()); return; }
    if (op >= 0xB8 && op <= 0xBF) { r[op - 0xB8] = fetch16(); return; }

    switch (op) {
        case 0x80:
        case 0x81:
        case 0x82:
        case 0x83: {
            bool word = (op & 1) != 0 && op != 0x82;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            int aluOp = (modrm >> 3) & 7;
            u16 a = readOp(e, word);
            u16 b;
            if (op == 0x81) {
                b = fetch16();
            } else if (op == 0x83) {
                b = static_cast<u16>(static_cast<i16>(static_cast<i8>(fetch8())));
            } else {
                b = fetch8();
            }
            u16 res = alu(aluOp, a, b, word);
            if (aluOp != 7) {
                writeOp(e, word, res);
            }
            return;
        }
        case 0x84:
        case 0x85: {
            bool word = (op & 1) != 0;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            int reg = (modrm >> 3) & 7;
            alu(4, readOp(e, word), word ? r[reg] : reg8(reg), word);
            return;
        }
        case 0x86:
        case 0x87: {
            bool word = (op & 1) != 0;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            int reg = (modrm >> 3) & 7;
            u16 a = readOp(e, word);
            u16 b = word ? r[reg] : reg8(reg);
            writeOp(e, word, b);
            if (word) {
                r[reg] = a;
            } else {
                setReg8(reg, static_cast<u8>(a));
            }
            return;
        }
        case 0x88:
        case 0x89: {
            bool word = (op & 1) != 0;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            int reg = (modrm >> 3) & 7;
            writeOp(e, word, word ? r[reg] : reg8(reg));
            return;
        }
        case 0x8A:
        case 0x8B: {
            bool word = (op & 1) != 0;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            int reg = (modrm >> 3) & 7;
            u16 v = readOp(e, word);
            if (word) {
                r[reg] = v;
            } else {
                setReg8(reg, static_cast<u8>(v));
            }
            return;
        }
        case 0x8C: {
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            writeOp(e, true, s[(modrm >> 3) & 3]);
            return;
        }
        case 0x8D: {
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            if (!e.isReg) {
                r[(modrm >> 3) & 7] = e.offset;
            }
            return;
        }
        case 0x8E: {
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            int idx = (modrm >> 3) & 3;
            s[idx] = readOp(e, true);
            if (idx == SS) {
                inhibitInterrupt = true;
            }
            return;
        }
        case 0x8F: {
            u16 v = pop();
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            writeOp(e, true, v);
            return;
        }
        case 0x90: return;
        case 0x98: r[AX] = static_cast<u16>(static_cast<i16>(static_cast<i8>(r[AX] & 0xFF))); return;
        case 0x99: r[DX] = (r[AX] & 0x8000) ? 0xFFFF : 0; return;
        case 0x9A: {
            u16 newIp = fetch16();
            u16 newCs = fetch16();
            push(s[CS]);
            push(ip);
            ip = newIp;
            s[CS] = newCs;
            cycles += 20;
            return;
        }
        case 0x9B: return;
        case 0x9C: push(getFlags()); return;
        case 0x9D: setFlags(pop()); return;
        case 0x9E: {
            u8 ah = static_cast<u8>(r[AX] >> 8);
            flags = static_cast<u16>((flags & 0xFF00) | (ah & 0xD5) | 0x02);
            return;
        }
        case 0x9F: {
            u8 low = static_cast<u8>((flags & 0xD5) | 0x02);
            r[AX] = static_cast<u16>((r[AX] & 0x00FF) | (static_cast<u16>(low) << 8));
            return;
        }
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3: {
            u16 off = fetch16();
            Seg seg = segOverride >= 0 ? static_cast<Seg>(segOverride) : DS;
            cycles += 6;
            switch (op) {
                case 0xA0: setReg8(0, rd8(seg, off)); break;
                case 0xA1: r[AX] = rd16(seg, off); break;
                case 0xA2: wr8(seg, off, static_cast<u8>(r[AX])); break;
                default: wr16(seg, off, r[AX]); break;
            }
            return;
        }
        case 0xA4: case 0xA5: case 0xA6: case 0xA7:
        case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            stringOp(op, rep);
            return;
        case 0xA8: alu(4, r[AX] & 0xFF, fetch8(), false); return;
        case 0xA9: alu(4, r[AX], fetch16(), true); return;
        case 0xC0:
        case 0xC2: {
            u16 n = fetch16();
            ip = pop();
            r[SP] = static_cast<u16>(r[SP] + n);
            cycles += 12;
            return;
        }
        case 0xC1:
        case 0xC3: ip = pop(); cycles += 12; return;
        case 0xC4:
        case 0xC5: {
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            if (e.isReg) {
                return;
            }
            r[(modrm >> 3) & 7] = rd16(e.seg, e.offset);
            u16 segment = rd16(e.seg, static_cast<u16>(e.offset + 2));
            if (op == 0xC4) {
                s[ES] = segment;
            } else {
                s[DS] = segment;
            }
            return;
        }
        case 0xC6:
        case 0xC7: {
            bool word = (op & 1) != 0;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            u16 imm = word ? fetch16() : fetch8();
            writeOp(e, word, imm);
            return;
        }
        case 0xC8:
        case 0xCA: {
            u16 n = fetch16();
            ip = pop();
            s[CS] = pop();
            r[SP] = static_cast<u16>(r[SP] + n);
            cycles += 20;
            return;
        }
        case 0xC9:
        case 0xCB: ip = pop(); s[CS] = pop(); cycles += 20; return;
        case 0xCC: interrupt(3); cycles += 50; return;
        case 0xCD: interrupt(fetch8()); cycles += 50; return;
        case 0xCE:
            if (getFlag(OF)) {
                interrupt(4);
            }
            return;
        case 0xCF:
            ip = pop();
            s[CS] = pop();
            setFlags(pop());
            cycles += 24;
            return;
        case 0xD0:
        case 0xD1:
        case 0xD2:
        case 0xD3: {
            bool word = (op & 1) != 0;
            u8 modrm = fetch8();
            Operand e = decode(modrm);
            unsigned count = (op & 2) ? (r[CX] & 0xFF) : 1;
            if (model == Model::I80186) {
                count &= 0x1F;
            }
            u16 v = readOp(e, word);
            u16 res = shiftRotate((modrm >> 3) & 7, v, count, word);
            writeOp(e, word, res);
            cycles += 2 * count;
            return;
        }
        case 0xD4: {
            u8 base = fetch8();
            if (base == 0) {
                setSzp(0, false);
                setFlag(CF, false);
                setFlag(OF, false);
                setFlag(AF, false);
                interrupt(0);
                return;
            }
            u8 al = static_cast<u8>(r[AX]);
            r[AX] = static_cast<u16>(((al / base) << 8) | (al % base));
            setSzp(r[AX] & 0xFF, false);
            return;
        }
        case 0xD5: {
            u8 base = fetch8();
            u8 al = static_cast<u8>(r[AX]);
            u8 ah = static_cast<u8>(r[AX] >> 8);
            u8 res = static_cast<u8>(al + ah * base);
            r[AX] = res;
            setSzp(res, false);
            return;
        }
        case 0xD6: setReg8(0, getFlag(CF) ? 0xFF : 0x00); return;
        case 0xD7: setReg8(0, rd8(segOverride >= 0 ? static_cast<Seg>(segOverride) : DS, static_cast<u16>(r[BX] + (r[AX] & 0xFF)))); return;
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF: {
            u8 modrm = fetch8();
            decode(modrm);
            return;
        }
        case 0xE0: {
            i8 d = static_cast<i8>(fetch8());
            r[CX] = static_cast<u16>(r[CX] - 1);
            jumpRel(d, r[CX] != 0 && !getFlag(ZF));
            return;
        }
        case 0xE1: {
            i8 d = static_cast<i8>(fetch8());
            r[CX] = static_cast<u16>(r[CX] - 1);
            jumpRel(d, r[CX] != 0 && getFlag(ZF));
            return;
        }
        case 0xE2: {
            i8 d = static_cast<i8>(fetch8());
            r[CX] = static_cast<u16>(r[CX] - 1);
            jumpRel(d, r[CX] != 0);
            return;
        }
        case 0xE3: {
            i8 d = static_cast<i8>(fetch8());
            jumpRel(d, r[CX] == 0);
            return;
        }
        case 0xE4: setReg8(0, static_cast<u8>(io.read(fetch8(), 1))); cycles += 10; return;
        case 0xE5: r[AX] = static_cast<u16>(io.read(fetch8(), 2)); cycles += 10; return;
        case 0xE6: io.write(fetch8(), 1, r[AX] & 0xFF); cycles += 10; return;
        case 0xE7: io.write(fetch8(), 2, r[AX]); cycles += 10; return;
        case 0xE8: {
            i16 d = static_cast<i16>(fetch16());
            push(ip);
            ip = static_cast<u16>(ip + d);
            cycles += 12;
            return;
        }
        case 0xE9: {
            i16 d = static_cast<i16>(fetch16());
            ip = static_cast<u16>(ip + d);
            cycles += 8;
            return;
        }
        case 0xEA: {
            u16 newIp = fetch16();
            u16 newCs = fetch16();
            ip = newIp;
            s[CS] = newCs;
            cycles += 8;
            return;
        }
        case 0xEB: {
            i8 d = static_cast<i8>(fetch8());
            ip = static_cast<u16>(ip + d);
            cycles += 8;
            return;
        }
        case 0xEC: setReg8(0, static_cast<u8>(io.read(r[DX], 1))); cycles += 8; return;
        case 0xED: r[AX] = static_cast<u16>(io.read(r[DX], 2)); cycles += 8; return;
        case 0xEE: io.write(r[DX], 1, r[AX] & 0xFF); cycles += 8; return;
        case 0xEF: io.write(r[DX], 2, r[AX]); cycles += 8; return;
        case 0xF4: halted = true; return;
        case 0xF5: setFlag(CF, !getFlag(CF)); return;
        case 0xF6: group3(false); return;
        case 0xF7: group3(true); return;
        case 0xF8: setFlag(CF, false); return;
        case 0xF9: setFlag(CF, true); return;
        case 0xFA: setFlag(IF, false); return;
        case 0xFB: setFlag(IF, true); inhibitInterrupt = true; return;
        case 0xFC: setFlag(DF, false); return;
        case 0xFD: setFlag(DF, true); return;
        case 0xFE: group45(false, 0xFE); return;
        case 0xFF: group45(true, 0xFF); return;
        default: return;
    }
}

u32 Cpu8086::step() {
    cycles = 0;
    bool shadow = inhibitInterrupt;
    inhibitInterrupt = false;

    if (!shadow && (flags & IF) && intrLine.level()) {
        int vector = intrLine.acknowledge();
        if (vector >= 0) {
            halted = false;
            interrupt(static_cast<u8>(vector));
            ++instrCount;
            return 61;
        }
    }
    if (halted) {
        return 1;
    }

    bool trap = (flags & TF) != 0;
    executeOne();
    ++instrCount;
    if (trap && (flags & TF) && !halted) {
        interrupt(1);
    }
    return cycles;
}

void Cpu8086::dumpState(std::ostream& out) const {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer,
                  "AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X\n"
                  "CS=%04X IP=%04X DS=%04X ES=%04X SS=%04X FLAGS=%04X\n",
                  r[AX], r[BX], r[CX], r[DX], r[SP], r[BP], r[SI], r[DI], s[CS], ip, s[DS], s[ES], s[SS],
                  getFlags());
    out << buffer;
}

std::string Cpu8086::traceLine() const {
    char buffer[200];
    std::snprintf(buffer, sizeof buffer,
                  "%04X:%04X AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X DS=%04X ES=%04X SS=%04X F=%04X",
                  s[CS], ip, r[AX], r[BX], r[CX], r[DX], r[SP], r[BP], r[SI], r[DI], s[DS], s[ES], s[SS], getFlags());
    return buffer;
}

}
