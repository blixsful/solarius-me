#pragma once
#include <functional>
#include <memory>
#include <vector>
#include "types.hpp"

namespace solme {

class IoHandler {
public:
    virtual ~IoHandler() = default;
    virtual u64 read(Addr offset, int size) = 0;
    virtual void write(Addr offset, int size, u64 value) = 0;
    virtual bool wideAccess() const { return false; }
};

struct Page {
    u8* data = nullptr;
    IoHandler* handler = nullptr;
    Addr base = 0;
    Addr end = 0;
    bool readOnly = false;
};

class AddressSpace {
public:
    AddressSpace(int addressBits, int pageShift);

    void mapRam(Addr base, size_t size);
    void mapRom(Addr base, const u8* data, size_t size);
    void mapHandler(Addr base, Addr size, IoHandler* handler);
    void unmap(Addr base, Addr size);

    u64 read(Addr address, int size);
    void write(Addr address, int size, u64 value);

    u8 read8(Addr address) {
        const Page& p = pageFor(address);
        if (p.data) {
            return p.data[address & pageMask];
        }
        return static_cast<u8>(readSlow(address, 1));
    }

    u16 read16(Addr address) {
        const Page& p = pageFor(address);
        Addr off = address & pageMask;
        if (p.data && off + 1 <= pageMask) {
            return static_cast<u16>(p.data[off] | (p.data[off + 1] << 8));
        }
        return static_cast<u16>(readSlow(address, 2));
    }

    u32 read32(Addr address) { return static_cast<u32>(readSlow(address, 4)); }

    void write8(Addr address, u8 value) {
        const Page& p = pageFor(address);
        if (p.data && !p.readOnly) {
            p.data[address & pageMask] = value;
            return;
        }
        writeSlow(address, 1, value);
    }

    void write16(Addr address, u16 value) {
        const Page& p = pageFor(address);
        Addr off = address & pageMask;
        if (p.data && !p.readOnly && off + 1 <= pageMask) {
            p.data[off] = static_cast<u8>(value);
            p.data[off + 1] = static_cast<u8>(value >> 8);
            return;
        }
        writeSlow(address, 2, value);
    }

    void write32(Addr address, u32 value) { writeSlow(address, 4, value); }

    void poke(Addr address, const u8* bytes, size_t count);
    void peek(Addr address, u8* bytes, size_t count);

    void setTracer(std::function<void(bool, Addr, int, u64)> fn) { tracer = std::move(fn); }

    Addr addressMask() const { return addrMask; }
    int addressBits() const { return bits; }

private:
    static constexpr int kChunkBits = 10;
    static constexpr size_t kChunkPages = size_t(1) << kChunkBits;

    const Page& pageFor(Addr address) const {
        Addr number = (address & addrMask) >> shift;
        const auto& chunk = top[number >> kChunkBits];
        return chunk ? chunk[number & (kChunkPages - 1)] : emptyPage;
    }

    Page& pageForMap(Addr address);
    u64 readSlow(Addr address, int size);
    void writeSlow(Addr address, int size, u64 value);

    int bits;
    int shift;
    Addr addrMask;
    Addr pageMask;
    std::vector<std::unique_ptr<Page[]>> top;
    std::vector<std::unique_ptr<u8[]>> storage;
    Page emptyPage;
    std::function<void(bool, Addr, int, u64)> tracer;
};

}
