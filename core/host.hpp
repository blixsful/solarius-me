#pragma once
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "scheduler.hpp"
#include "types.hpp"

namespace solme {

class CharBackend {
public:
    virtual ~CharBackend() = default;
    virtual bool read(u8& byte) { (void)byte; return false; }
    virtual void write(u8 byte) { (void)byte; }
    virtual void flush() {}
};

class ScriptInput : public CharBackend {
public:
    explicit ScriptInput(Scheduler& scheduler) : sched(scheduler) {}
    void add(u64 atMs, const std::string& text);
    bool read(u8& byte) override;

private:
    struct Item {
        u64 readyNs;
        u8 byte;
    };
    Scheduler& sched;
    std::deque<Item> items;
    u64 lastNs = 0;
};

class Host {
public:
    static Host& instance();

    bool stdinIsTerminal() const;
    void enableRawInput();
    void restore();
    bool claimStdin();
    bool pollStdin(u8& byte);
    void writeOut(const char* data, size_t size);
    void flushOut();

private:
    Host() = default;
    bool rawEnabled = false;
    bool stdinClaimed = false;
    std::deque<u8> pending;
};

std::unique_ptr<CharBackend> makeCharBackend(const std::string& spec);
std::string unescapeText(const std::string& text);

}
