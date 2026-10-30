#pragma once
#include <array>
#include <string>
#include "address_space.hpp"
#include "cpu.hpp"
#include "signal.hpp"

namespace solme {

class Cpu8086 : public Cpu {
public:
    enum class Model { I8086, I80186 };

    Cpu8086(AddressSpace& memory, AddressSpace& ports, Signal& intr, u64 hz, Model cpuModel = Model::I8086);

    void reset() override;
    u32 step() override;
    bool idle() const override;
    u64 frequencyHz() const override { return clockHz; }
    u64 instructionCount() const override { return instrCount; }
    void dumpState(std::ostream& out) const override;
    std::string traceLine() const override;
    bool setEntry(const std::string& text) override;

    enum Flag : u16 {
        CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040, SF = 0x0080,
        TF = 0x0100, IF = 0x0200, DF = 0x0400, OF = 0x0800
    };

    std::array<u16, 8> r{};
    std::array<u16, 4> s{};
    u16 ip = 0;
    u16 flags = 0xF002;

    u16 getFlags() const { return static_cast<u16>((flags & 0x0FD5) | 0xF002); }
    void setFlags(u16 value) { flags = static_cast<u16>((value & 0x0FD5) | 0xF002); }
    void executeOne();

private:
    enum Reg { AX, CX, DX, BX, SP, BP, SI, DI };
    enum Seg { ES, CS, SS, DS };

    struct Operand {
        bool isReg = false;
        u8 reg = 0;
        Seg seg = DS;
        u16 offset = 0;
    };

    AddressSpace& mem;
    AddressSpace& io;
    Signal& intrLine;
    u64 clockHz;
    Model model;
    u64 instrCount = 0;
    u32 cycles = 0;
    bool halted = false;
    bool inhibitInterrupt = false;
    bool trapPending = false;
    int segOverride = -1;
    int repPrefix = 0;
    int repPrefixActive = 0;
    u16 instrStart = 0;
    u8 modrmByte = 0;

    u32 linear(u16 segment, u16 offset) const { return ((static_cast<u32>(segment) << 4) + offset) & 0xFFFFF; }
    u8 fetch8();
    u16 fetch16();
    u8 rd8(Seg seg, u16 off);
    u16 rd16(Seg seg, u16 off);
    void wr8(Seg seg, u16 off, u8 value);
    void wr16(Seg seg, u16 off, u16 value);
    void push(u16 value);
    u16 pop();

    u8 reg8(int index) const { return index < 4 ? static_cast<u8>(r[index]) : static_cast<u8>(r[index - 4] >> 8); }
    void setReg8(int index, u8 value);

    Operand decode(u8 modrm);
    u16 readOp(const Operand& op, bool word);
    void writeOp(const Operand& op, bool word, u16 value);

    void setFlag(u16 mask, bool on) { flags = on ? static_cast<u16>(flags | mask) : static_cast<u16>(flags & ~mask); }
    bool getFlag(u16 mask) const { return (flags & mask) != 0; }
    void setSzp(u16 result, bool word);
    u16 alu(int op, u16 a, u16 b, bool word);
    u16 incdec(u16 value, bool dec, bool word);
    u16 shiftRotate(int op, u16 value, unsigned count, bool word);
    bool condition(int code) const;

    void interrupt(u8 vector);
    void checkInterrupts();
    void stringOp(u8 opcode, int rep);
    bool exec186(u8 op);
    void group3(bool word);
    void group45(bool word, u8 opcode);
    void doDiv(bool word, bool isSigned, u16 divisor);
    void doMul(bool word, bool isSigned, u16 multiplier);
    void jumpRel(i16 displacement, bool taken);
};

}
