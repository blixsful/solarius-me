#pragma once
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include "address_space.hpp"
#include "block.hpp"
#include "cpu.hpp"
#include "device.hpp"
#include "host.hpp"
#include "options.hpp"
#include "scheduler.hpp"
#include "signal.hpp"
#include "video.hpp"
#include <deque>

namespace solme {

struct RunLimits {
    u64 maxInstructions = 0;
    u64 maxVirtualMs = 0;
    bool realtime = false;
};

struct RunResult {
    int exitCode = 0;
    std::string reason;
    u64 instructions = 0;
    u64 virtualNs = 0;
};

class Machine {
public:
    Machine();

    AddressSpace& createSpace(const std::string& name, int addressBits, int pageShift);
    AddressSpace& space(const std::string& name);
    bool hasSpace(const std::string& name) const;

    Signal& signal(const std::string& name);
    Scheduler& scheduler() { return sched; }

    void setCpu(std::unique_ptr<Cpu> processor);
    Cpu& cpu() { return *processor; }

    Device* addDevice(const std::string& name, const Props& props);

    BlockImage* addDrive(const Spec& spec, std::string& error);
    BlockImage* drive(const std::string& id);

    void addPoller(std::function<void()> poller);
    void addDumper(std::function<void(std::ostream&)> dumper);
    void dumpAll(std::ostream& out);

    std::vector<CharBackend*> inputs();
    void addInput(CharBackend* backend) { inputBackends.push_back(backend); }
    CharBackend* openChar(const std::string& spec);
    ScriptInput& scriptInput();

    void registerVideo(VideoSource* source) { videoSources.push_back(source); }
    const std::vector<VideoSource*>& videos() const { return videoSources; }
    std::deque<u8>& scancodes() { return scancodeQueue; }

    void setPaused(bool value) { paused = value; }
    bool isPaused() const { return paused; }
    std::string describe();
    void addDescriber(std::function<std::string()> describer) { describers.push_back(std::move(describer)); }
    bool exitWasRequested() const { return exitRequested; }

    void requestExit(int code, const std::string& reason);
    bool interactive() const { return interactiveInput; }
    void setInteractive(bool value) { interactiveInput = value; }

    void setTraceStream(std::ostream* stream, u64 fromInstruction = 0) {
        trace = stream;
        traceFrom = fromInstruction;
    }
    void resetAll();
    void shutdown();
    RunResult run(const RunLimits& limits);

private:
    void pollHost();

    Scheduler sched;
    std::map<std::string, std::unique_ptr<AddressSpace>> spaces;
    std::map<std::string, std::unique_ptr<Signal>> signals;
    std::unique_ptr<Cpu> processor;
    std::vector<std::unique_ptr<Device>> devices;
    std::map<std::string, std::unique_ptr<BlockImage>> drives;
    std::vector<std::function<void()>> pollers;
    std::vector<std::function<void(std::ostream&)>> dumpers;
    std::vector<std::unique_ptr<CharBackend>> chars;
    std::vector<CharBackend*> inputBackends;
    std::unique_ptr<ScriptInput> script;
    std::vector<VideoSource*> videoSources;
    std::deque<u8> scancodeQueue;
    std::vector<std::function<std::string()>> describers;
    bool paused = false;
    bool exitRequested = false;
    int exitCode = 0;
    std::string exitReason;
    bool interactiveInput = false;
    std::ostream* trace = nullptr;
    u64 traceFrom = 0;
    int driveCounter = 0;
};

}
