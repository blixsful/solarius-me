#include "machine.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace solme {

Machine::Machine() = default;

AddressSpace& Machine::createSpace(const std::string& name, int addressBits, int pageShift) {
    auto space = std::make_unique<AddressSpace>(addressBits, pageShift);
    AddressSpace& ref = *space;
    spaces[name] = std::move(space);
    return ref;
}

AddressSpace& Machine::space(const std::string& name) {
    auto it = spaces.find(name);
    if (it == spaces.end()) {
        throw std::runtime_error("no such address space: " + name);
    }
    return *it->second;
}

bool Machine::hasSpace(const std::string& name) const { return spaces.count(name) != 0; }

Signal& Machine::signal(const std::string& name) {
    auto& slot = signals[name];
    if (!slot) {
        slot = std::make_unique<Signal>();
    }
    return *slot;
}

void Machine::setCpu(std::unique_ptr<Cpu> cpuInstance) { processor = std::move(cpuInstance); }

Device* Machine::addDevice(const std::string& name, const Props& props) {
    auto device = DeviceRegistry::instance().create(name, *this, props);
    if (!device) {
        throw std::runtime_error("unknown device: " + name);
    }
    devices.push_back(std::move(device));
    return devices.back().get();
}

BlockImage* Machine::addDrive(const Spec& spec, std::string& error) {
    std::string path = spec.props.str("file");
    if (path.empty()) {
        error = "drive needs file=";
        return nullptr;
    }
    auto image = std::make_unique<BlockImage>();
    if (!image->open(path, spec.props.str("format", "auto"), spec.props.flag("snapshot", false), error)) {
        return nullptr;
    }
    std::string id = spec.props.str("id", "drive" + std::to_string(driveCounter));
    ++driveCounter;
    BlockImage* raw = image.get();
    drives[id] = std::move(image);
    return raw;
}

BlockImage* Machine::drive(const std::string& id) {
    auto it = drives.find(id);
    return it == drives.end() ? nullptr : it->second.get();
}

void Machine::addPoller(std::function<void()> poller) { pollers.push_back(std::move(poller)); }

void Machine::addDumper(std::function<void(std::ostream&)> dumper) { dumpers.push_back(std::move(dumper)); }

void Machine::dumpAll(std::ostream& out) {
    for (auto& dumper : dumpers) {
        dumper(out);
    }
}

std::vector<CharBackend*> Machine::inputs() { return inputBackends; }

CharBackend* Machine::openChar(const std::string& spec) {
    chars.push_back(makeCharBackend(spec));
    return chars.back().get();
}

ScriptInput& Machine::scriptInput() {
    if (!script) {
        script = std::make_unique<ScriptInput>(sched);
        inputBackends.push_back(script.get());
    }
    return *script;
}

void Machine::requestExit(int code, const std::string& reason) {
    if (!exitRequested) {
        exitRequested = true;
        exitCode = code;
        exitReason = reason;
    }
}

void Machine::resetAll() {
    for (auto& device : devices) {
        device->reset();
    }
    processor->reset();
}

std::string Machine::describe() {
    std::string text;
    for (auto& describer : describers) {
        text += describer();
    }
    return text;
}

void Machine::shutdown() {
    for (auto& device : devices) {
        device->shutdown();
    }
}

void Machine::pollHost() {
    for (auto& poller : pollers) {
        poller();
    }
}

RunResult Machine::run(const RunLimits& limits) {
    using Clock = std::chrono::steady_clock;
    auto wallStart = Clock::now();
    u64 hz = processor->frequencyHz();
    u64 milliNsPerCycle = 1000000000000ULL / hz;
    u64 accumulator = 0;
    u32 sincePoll = 0;
    u64 maxNs = limits.maxVirtualMs * 1000000ULL;

    auto throttle = [&]() {
        if (!limits.realtime) {
            return;
        }
        auto wall = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - wallStart).count();
        u64 virt = sched.now();
        if (virt > static_cast<u64>(wall) + 2000000ULL) {
            std::this_thread::sleep_for(std::chrono::nanoseconds(virt - wall));
        }
    };

    while (!exitRequested) {
        if (paused) {
            pollHost();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }
        if (processor->idle()) {
            if (!sched.hasEvents()) {
                if (!interactiveInput) {
                    requestExit(0, "cpu halted with no pending events");
                    break;
                }
                pollHost();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            sched.advanceToNextEvent();
            pollHost();
            throttle();
        } else {
            if (trace && processor->instructionCount() >= traceFrom) {
                *trace << processor->traceLine() << "\n";
            }
            u32 cycles = processor->step();
            accumulator += static_cast<u64>(cycles) * milliNsPerCycle;
            u64 ns = accumulator / 1000;
            accumulator -= ns * 1000;
            sched.advance(ns);
            if (++sincePoll >= 2048) {
                sincePoll = 0;
                pollHost();
                throttle();
            }
        }
        if (limits.maxInstructions && processor->instructionCount() >= limits.maxInstructions) {
            requestExit(0, "instruction limit reached");
        }
        if (maxNs && sched.now() >= maxNs) {
            requestExit(0, "virtual time limit reached");
        }
    }
    pollHost();

    RunResult result;
    result.exitCode = exitCode;
    result.reason = exitReason;
    result.instructions = processor->instructionCount();
    result.virtualNs = sched.now();
    return result;
}

}
