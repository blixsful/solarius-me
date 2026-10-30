#pragma once
#include <string>
#include <vector>
#include "types.hpp"

namespace solme {

struct VideoFrame {
    int width = 0;
    int height = 0;
    const u32* pixels = nullptr;
    u64 generation = 0;
};

class VideoSource {
public:
    virtual ~VideoSource() = default;
    virtual std::string videoName() const = 0;
    virtual bool captureFrame(VideoFrame& frame) = 0;
};

}
