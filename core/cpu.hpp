#pragma once
#include <ostream>
#include <string>
#include "types.hpp"

namespace solme {

class Cpu {
public:
    virtual ~Cpu() = default;

    virtual void reset() = 0;
    virtual u32 step() = 0;
    virtual bool idle() const = 0;
    virtual u64 frequencyHz() const = 0;
    virtual u64 instructionCount() const = 0;
    virtual void dumpState(std::ostream& out) const = 0;
    virtual std::string traceLine() const = 0;
    virtual bool setEntry(const std::string& text) = 0;
};

}
