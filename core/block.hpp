#pragma once
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include "types.hpp"

namespace solme {

struct Geometry {
    u32 cylinders = 0;
    u32 heads = 0;
    u32 sectors = 0;
};

class BlockImage {
public:
    static constexpr u32 kSectorSize = 512;

    ~BlockImage();

    bool open(const std::string& path, const std::string& format, bool snapshot, std::string& error);
    u64 sectorCount() const { return totalSectors; }
    const Geometry& geometry() const { return geom; }
    bool read(u64 lba, u32 count, u8* out);
    bool write(u64 lba, u32 count, const u8* in);

private:
    FILE* file = nullptr;
    u64 totalSectors = 0;
    u64 dataOffset = 0;
    bool useOverlay = false;
    Geometry geom;
    std::map<u64, std::vector<u8>> overlay;
};

}
