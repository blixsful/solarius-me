#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include "machine.hpp"

#ifndef SOLME_ARCH
#error SOLME_ARCH must be defined by the build
#endif

namespace {

const std::set<std::string> kBooleanFlags = {"dump-regs", "dump-screen", "rt", "snapshot", "list-machines",
                                             "list-devices", "help", "stdin"};

void printUsage() {
    std::cout << "usage: solme-" << SOLME_ARCH << " [options] [disk-image]\n"
              << "  --machine NAME           machine type (see --list-machines)\n"
              << "  --ram SIZE               RAM size, e.g. 640K\n"
              << "  --drive file=PATH[,format=auto|raw|vhd][,snapshot][,id=NAME]\n"
              << "  --device NAME[,key=value...]   add an extra device (see --list-devices)\n"
              << "  --serial stdio|null|file:PATH\n"
              << "  --display sdl|term|none  display backend (sdl opens the SolME Display window)\n"
              << "  --zoom N, --fit auto|best|fixed   initial SDL display scaling\n"
              << "  --screenshot FILE.ppm    save the first video output at exit\n"
              << "  --load FILE@ADDR         load raw file into memory\n"
              << "  --entry TEXT             CPU entry point (arch specific)\n"
              << "  --type-at MS:TEXT        inject keyboard text at virtual time\n"
              << "  --max-instr N, --max-ms N, --rt\n"
              << "  --dump-regs, --dump-screen, --trace FILE\n";
}

bool parseArgs(int argc, char** argv, solme::Options& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0) {
            solme::Spec drive;
            drive.props.set("file", arg);
            options.drives.push_back(drive);
            continue;
        }
        std::string key = arg.substr(2);
        std::string value = "on";
        std::string inlineValue;
        size_t eq = key.find('=');
        if (eq != std::string::npos && key.compare(0, 5, "drive") != 0 && key.compare(0, 6, "device") != 0) {
            inlineValue = key.substr(eq + 1);
            key = key.substr(0, eq);
            value = inlineValue;
        } else if (!kBooleanFlags.count(key)) {
            if (i + 1 >= argc) {
                std::cerr << "missing value for --" << key << "\n";
                return false;
            }
            value = argv[++i];
        }
        if (key == "drive") {
            options.drives.push_back(solme::parseSpec(value));
        } else if (key == "device") {
            options.devices.push_back(solme::parseSpec(value));
        } else if (key == "load") {
            size_t at = value.rfind('@');
            if (at == std::string::npos) {
                std::cerr << "--load needs FILE@ADDR\n";
                return false;
            }
            solme::LoadSpec load;
            load.path = value.substr(0, at);
            load.space = "mem";
            if (!solme::parseNumber(value.substr(at + 1), load.address)) {
                std::cerr << "bad load address\n";
                return false;
            }
            options.loads.push_back(load);
        } else if (key == "type-at") {
            size_t colon = value.find(':');
            solme::TypeSpec typed;
            if (colon == std::string::npos || !solme::parseNumber(value.substr(0, colon), typed.atMs)) {
                std::cerr << "--type-at needs MS:TEXT\n";
                return false;
            }
            typed.text = solme::unescapeText(value.substr(colon + 1));
            options.typed.push_back(typed);
        } else {
            options.opts.set(key, value);
        }
    }
    return true;
}

}

int main(int argc, char** argv) {
    using namespace solme;
    Options options;
    if (!parseArgs(argc, argv, options)) {
        return 2;
    }
    if (options.opts.flag("help")) {
        printUsage();
        return 0;
    }
    if (options.opts.flag("list-machines")) {
        for (const auto& type : MachineRegistry::instance().all()) {
            if (type.arch == SOLME_ARCH) {
                std::cout << type.name << (type.isDefault ? " (default)" : "") << " - " << type.description << "\n";
            }
        }
        return 0;
    }
    if (options.opts.flag("list-devices")) {
        for (const auto& name : DeviceRegistry::instance().names()) {
            std::cout << name << "\n";
        }
        return 0;
    }

    const MachineType* type = MachineRegistry::instance().find(SOLME_ARCH, options.opts.str("machine"));
    if (!type) {
        std::cerr << "no such machine type for " << SOLME_ARCH << "\n";
        return 2;
    }

    Machine machine;
    std::ofstream traceFile;
    try {
        std::string display = options.opts.str("display", "none");
        bool terminal = display == "term";
        if (terminal || options.opts.str("serial") == "stdio") {
            if (Host::instance().stdinIsTerminal()) {
                Host::instance().enableRawInput();
                machine.setInteractive(true);
            }
        }
        if (terminal) {
            machine.addInput(machine.openChar("stdio"));
        }
        for (const auto& typed : options.typed) {
            machine.scriptInput().add(typed.atMs, typed.text);
        }

        for (const auto& driveSpec : options.drives) {
            std::string error;
            if (!machine.addDrive(driveSpec, error)) {
                std::cerr << "drive: " << error << "\n";
                return 1;
            }
        }

        type->build(machine, options);

        if (display == "sdl") {
            Props displayProps;
            displayProps.set("zoom", options.opts.str("zoom", "1"));
            displayProps.set("fit", options.opts.str("fit", "auto"));
            if (options.opts.has("display-script")) {
                displayProps.set("script", options.opts.str("display-script"));
            }
            machine.addDevice("display-sdl", displayProps);
        }

        for (const auto& deviceSpec : options.devices) {
            machine.addDevice(deviceSpec.name, deviceSpec.props);
        }

        for (const auto& load : options.loads) {
            std::ifstream in(load.path, std::ios::binary);
            if (!in) {
                std::cerr << "cannot open " << load.path << "\n";
                return 1;
            }
            std::vector<u8> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            machine.space(load.space).poke(load.address, bytes.data(), bytes.size());
        }

        std::ofstream ioTrace;
        if (options.opts.has("trace-io")) {
            ioTrace.open(options.opts.str("trace-io"));
            Machine* mp = &machine;
            machine.space("io").setTracer([&ioTrace, mp](bool isWrite, Addr a, int size, u64 v) {
                ioTrace << (isWrite ? "W " : "R ") << std::hex << a << " " << size << " " << v << std::dec << " t="
                        << mp->scheduler().now() / 1000 << " i=" << mp->cpu().instructionCount() << "\n";
            });
        }
        machine.resetAll();
        if (options.opts.has("entry") && !machine.cpu().setEntry(options.opts.str("entry"))) {
            std::cerr << "bad entry point\n";
            return 1;
        }

        if (options.opts.has("trace")) {
            traceFile.open(options.opts.str("trace"));
            machine.setTraceStream(&traceFile, options.opts.num("trace-from", 0));
        }

        RunLimits limits;
        limits.maxInstructions = options.opts.num("max-instr", 0);
        limits.maxVirtualMs = options.opts.num("max-ms", 0);
        limits.realtime = options.opts.flag("rt");
        RunResult result = machine.run(limits);

        if (options.opts.has("screenshot")) {
            if (!machine.videos().empty()) {
                VideoFrame frame;
                if (machine.videos()[0]->captureFrame(frame)) {
                    std::ofstream ppm(options.opts.str("screenshot"), std::ios::binary);
                    ppm << "P6\n" << frame.width << " " << frame.height << "\n255\n";
                    for (int i = 0; i < frame.width * frame.height; ++i) {
                        u32 px = frame.pixels[i];
                        char rgb[3] = {static_cast<char>(px >> 16), static_cast<char>(px >> 8), static_cast<char>(px)};
                        ppm.write(rgb, 3);
                    }
                }
            }
        }
        machine.shutdown();
        Host::instance().flushOut();
        Host::instance().restore();
        if (options.opts.flag("dump-screen")) {
            machine.dumpAll(std::cout);
        }
        if (options.opts.has("dump-mem")) {
            std::string spec = options.opts.str("dump-mem");
            size_t colon = spec.find(':');
            u64 address = 0;
            u64 length = 0;
            if (colon != std::string::npos && parseNumber(spec.substr(0, colon), address) &&
                parseNumber(spec.substr(colon + 1), length)) {
                for (u64 i = 0; i < length; ++i) {
                    if (i % 16 == 0) {
                        std::cout << std::hex << std::setw(6) << std::setfill('0') << (address + i) << ":";
                    }
                    std::cout << " " << std::setw(2) << (unsigned)machine.space("mem").read8(address + i);
                    if (i % 16 == 15 || i + 1 == length) {
                        std::cout << "\n";
                    }
                }
                std::cout << std::dec << std::setfill(' ');
            }
        }
        if (options.opts.flag("dump-regs")) {
            machine.cpu().dumpState(std::cout);
        }
        std::cerr << "[solme] stopped: " << result.reason << " after " << result.instructions
                  << " instructions, " << result.virtualNs / 1000000 << " ms virtual\n";
        return result.exitCode;
    } catch (const std::exception& e) {
        Host::instance().restore();
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
