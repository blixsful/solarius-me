#include "block.hpp"
#include <cstring>

namespace solme {

BlockImage::~BlockImage() {
    if (file) {
        std::fclose(file);
    }
}

static u32 be32(const u8* p) { return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3]; }
static u64 be64(const u8* p) { return (u64(be32(p)) << 32) | be32(p + 4); }

bool BlockImage::open(const std::string& path, const std::string& format, bool snapshot, std::string& error) {
    file = std::fopen(path.c_str(), snapshot ? "rb" : "r+b");
    if (!file) {
        file = std::fopen(path.c_str(), "rb");
        if (!file) {
            error = "cannot open " + path;
            return false;
        }
        snapshot = true;
    }
    useOverlay = snapshot;
    fseeko(file, 0, SEEK_END);
    u64 size = static_cast<u64>(ftello(file));
    u64 usable = size;
    bool vhd = false;

    if ((format == "auto" || format == "vhd") && size >= 512) {
        u8 footer[512];
        fseeko(file, static_cast<off_t>(size - 512), SEEK_SET);
        if (std::fread(footer, 1, 512, file) == 512 && std::memcmp(footer, "conectix", 8) == 0) {
            u32 type = be32(footer + 60);
            if (type != 2) {
                error = "only fixed VHD images are supported";
                return false;
            }
            usable = be64(footer + 48);
            if (usable + 512 > size) {
                usable = size - 512;
            }
            geom.cylinders = (u32(footer[56]) << 8) | footer[57];
            geom.heads = footer[58];
            geom.sectors = footer[59];
            vhd = true;
        } else if (format == "vhd") {
            error = "not a VHD image";
            return false;
        }
    }

    totalSectors = usable / kSectorSize;
    if (!vhd) {
        geom.heads = 16;
        geom.sectors = 63;
        u64 cyl = totalSectors / (geom.heads * geom.sectors);
        geom.cylinders = static_cast<u32>(cyl ? cyl : 1);
    }
    return true;
}

bool BlockImage::read(u64 lba, u32 count, u8* out) {
    if (lba + count > totalSectors) {
        return false;
    }
    for (u32 i = 0; i < count; ++i) {
        u8* dst = out + static_cast<size_t>(i) * kSectorSize;
        auto it = overlay.find(lba + i);
        if (it != overlay.end()) {
            std::memcpy(dst, it->second.data(), kSectorSize);
            continue;
        }
        fseeko(file, static_cast<off_t>((lba + i) * kSectorSize + dataOffset), SEEK_SET);
        if (std::fread(dst, 1, kSectorSize, file) != kSectorSize) {
            std::memset(dst, 0, kSectorSize);
        }
    }
    return true;
}

bool BlockImage::write(u64 lba, u32 count, const u8* in) {
    if (lba + count > totalSectors) {
        return false;
    }
    for (u32 i = 0; i < count; ++i) {
        const u8* src = in + static_cast<size_t>(i) * kSectorSize;
        if (useOverlay) {
            overlay[lba + i].assign(src, src + kSectorSize);
            continue;
        }
        fseeko(file, static_cast<off_t>((lba + i) * kSectorSize + dataOffset), SEEK_SET);
        if (std::fwrite(src, 1, kSectorSize, file) != kSectorSize) {
            return false;
        }
    }
    if (!useOverlay) {
        std::fflush(file);
    }
    return true;
}

}
