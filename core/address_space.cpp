#include "address_space.hpp"
#include <cstring>
#include <stdexcept>

namespace solme {

AddressSpace::AddressSpace(int addressBits, int pageShift) : bits(addressBits), shift(pageShift) {
    addrMask = bits >= 64 ? ~0ULL : ((1ULL << bits) - 1);
    pageMask = (1ULL << shift) - 1;
    u64 pages = 1ULL << (bits - shift);
    u64 chunks = (pages + kChunkPages - 1) / kChunkPages;
    if (chunks > (1ULL << 24)) {
        throw std::runtime_error("address space too large for flat page table");
    }
    top.resize(chunks);
}

Page& AddressSpace::pageForMap(Addr address) {
    Addr number = (address & addrMask) >> shift;
    auto& chunk = top[number >> kChunkBits];
    if (!chunk) {
        chunk.reset(new Page[kChunkPages]);
    }
    return chunk[number & (kChunkPages - 1)];
}

void AddressSpace::mapRam(Addr base, size_t size) {
    if ((base & pageMask) || (size & pageMask)) {
        throw std::runtime_error("RAM region must be page aligned");
    }
    std::unique_ptr<u8[]> block(new u8[size]());
    u8* raw = block.get();
    storage.push_back(std::move(block));
    for (size_t off = 0; off < size; off += (pageMask + 1)) {
        Page& p = pageForMap(base + off);
        p.data = raw + off;
        p.handler = nullptr;
        p.base = base;
        p.readOnly = false;
    }
}

void AddressSpace::mapRom(Addr base, const u8* data, size_t size) {
    if ((base & pageMask) || (size & pageMask)) {
        throw std::runtime_error("ROM region must be page aligned");
    }
    std::unique_ptr<u8[]> block(new u8[size]());
    std::memcpy(block.get(), data, size);
    u8* raw = block.get();
    storage.push_back(std::move(block));
    for (size_t off = 0; off < size; off += (pageMask + 1)) {
        Page& p = pageForMap(base + off);
        p.data = raw + off;
        p.handler = nullptr;
        p.base = base;
        p.readOnly = true;
    }
}

void AddressSpace::mapHandler(Addr base, Addr size, IoHandler* handler) {
    if ((base & pageMask) || (size & pageMask)) {
        throw std::runtime_error("handler region must be page aligned");
    }
    for (Addr off = 0; off < size; off += (pageMask + 1)) {
        Page& p = pageForMap(base + off);
        p.data = nullptr;
        p.handler = handler;
        p.base = base;
        p.end = base + size;
        p.readOnly = false;
    }
}

void AddressSpace::unmap(Addr base, Addr size) {
    for (Addr off = 0; off < size; off += (pageMask + 1)) {
        Page& p = pageForMap(base + off);
        p = Page();
    }
}

u64 AddressSpace::readSlow(Addr address, int size) {
    address &= addrMask;
    const Page& p = pageFor(address);
    Addr off = address & pageMask;
    if (p.data && off + size - 1 <= pageMask) {
        u64 value = 0;
        for (int i = 0; i < size; ++i) {
            value |= static_cast<u64>(p.data[off + i]) << (8 * i);
        }
        return value;
    }
    if (p.handler && (size == 1 || p.handler->wideAccess()) && (address & addrMask) + size <= p.end) {
        return p.handler->read(address - p.base, size) & (size == 8 ? ~0ULL : ((1ULL << (8 * size)) - 1));
    }
    if (size == 1) {
        return 0xFF;
    }
    u64 value = 0;
    for (int i = 0; i < size; ++i) {
        value |= (readSlow(address + i, 1) & 0xFF) << (8 * i);
    }
    return value;
}

void AddressSpace::writeSlow(Addr address, int size, u64 value) {
    address &= addrMask;
    const Page& p = pageFor(address);
    Addr off = address & pageMask;
    if (p.data) {
        if (p.readOnly) {
            return;
        }
        if (off + size - 1 <= pageMask) {
            for (int i = 0; i < size; ++i) {
                p.data[off + i] = static_cast<u8>(value >> (8 * i));
            }
            return;
        }
    }
    if (p.handler && (size == 1 || p.handler->wideAccess()) && (address & addrMask) + size <= p.end) {
        p.handler->write(address - p.base, size, value);
        return;
    }
    if (size == 1) {
        return;
    }
    for (int i = 0; i < size; ++i) {
        writeSlow(address + i, 1, (value >> (8 * i)) & 0xFF);
    }
}

u64 AddressSpace::read(Addr address, int size) {
    u64 value = readSlow(address, size);
    if (tracer) {
        tracer(false, address, size, value);
    }
    return value;
}

void AddressSpace::write(Addr address, int size, u64 value) {
    if (tracer) {
        tracer(true, address, size, value);
    }
    writeSlow(address, size, value);
}

void AddressSpace::poke(Addr address, const u8* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        Addr a = (address + i) & addrMask;
        const Page& p = pageFor(a);
        if (p.data) {
            p.data[a & pageMask] = bytes[i];
        } else {
            writeSlow(a, 1, bytes[i]);
        }
    }
}

void AddressSpace::peek(Addr address, u8* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<u8>(readSlow(address + i, 1));
    }
}

}
