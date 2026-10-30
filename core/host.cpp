#include "host.hpp"
#include <cstdio>
#include <cstdlib>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace solme {

namespace {

termios savedTermios;

void restoreAtExit() { Host::instance().restore(); }

class StdioBackend : public CharBackend {
public:
    StdioBackend() { hasInput = Host::instance().claimStdin(); }

    bool read(u8& byte) override { return hasInput && Host::instance().pollStdin(byte); }

    void write(u8 byte) override {
        char c = static_cast<char>(byte);
        Host::instance().writeOut(&c, 1);
    }

    void flush() override { Host::instance().flushOut(); }

private:
    bool hasInput = false;
};

class FileBackend : public CharBackend {
public:
    explicit FileBackend(const std::string& path) { file = std::fopen(path.c_str(), "wb"); }
    ~FileBackend() override {
        if (file) {
            std::fclose(file);
        }
    }
    void write(u8 byte) override {
        if (file) {
            std::fputc(byte, file);
        }
    }
    void flush() override {
        if (file) {
            std::fflush(file);
        }
    }

private:
    FILE* file = nullptr;
};

}

Host& Host::instance() {
    static Host host;
    return host;
}

bool Host::stdinIsTerminal() const { return isatty(0) != 0; }

void Host::enableRawInput() {
    if (rawEnabled || !isatty(0)) {
        return;
    }
    tcgetattr(0, &savedTermios);
    termios raw = savedTermios;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL | INLCR | ISTRIP);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
    rawEnabled = true;
    std::atexit(restoreAtExit);
}

void Host::restore() {
    if (rawEnabled) {
        tcsetattr(0, TCSANOW, &savedTermios);
        rawEnabled = false;
    }
}

bool Host::claimStdin() {
    if (stdinClaimed) {
        return false;
    }
    stdinClaimed = true;
    return true;
}

bool Host::pollStdin(u8& byte) {
    if (!pending.empty()) {
        byte = pending.front();
        pending.pop_front();
        return true;
    }
    pollfd pfd{0, POLLIN, 0};
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        u8 buffer[256];
        ssize_t n = ::read(0, buffer, sizeof buffer);
        for (ssize_t i = 0; i < n; ++i) {
            pending.push_back(buffer[i]);
        }
    }
    if (pending.empty()) {
        return false;
    }
    byte = pending.front();
    pending.pop_front();
    return true;
}

void Host::writeOut(const char* data, size_t size) { std::fwrite(data, 1, size, stdout); }

void Host::flushOut() { std::fflush(stdout); }

void ScriptInput::add(u64 atMs, const std::string& text) {
    u64 ready = atMs * 1000000ULL;
    if (ready < lastNs) {
        ready = lastNs;
    }
    for (char c : text) {
        items.push_back(Item{ready, static_cast<u8>(c)});
    }
    lastNs = ready;
}

bool ScriptInput::read(u8& byte) {
    if (items.empty() || items.front().readyNs > sched.now()) {
        return false;
    }
    byte = items.front().byte;
    items.pop_front();
    return true;
}

std::unique_ptr<CharBackend> makeCharBackend(const std::string& spec) {
    if (spec == "stdio") {
        return std::make_unique<StdioBackend>();
    }
    if (spec.rfind("file:", 0) == 0) {
        return std::make_unique<FileBackend>(spec.substr(5));
    }
    return std::make_unique<CharBackend>();
}

std::string unescapeText(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        char n = text[++i];
        switch (n) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'e': out.push_back(27); break;
            case '\\': out.push_back('\\'); break;
            case 'x':
                if (i + 2 < text.size() + 0) {
                    out.push_back(static_cast<char>(std::strtol(text.substr(i + 1, 2).c_str(), nullptr, 16)));
                    i += 2;
                }
                break;
            default: out.push_back(n); break;
        }
    }
    return out;
}

}
