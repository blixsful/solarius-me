#pragma once
#include <functional>
#include <vector>

namespace solme {

class Signal {
public:
    bool level() const { return state; }

    void set(bool value) {
        if (value == state) {
            return;
        }
        state = value;
        for (auto& watcher : watchers) {
            watcher(value);
        }
    }

    void watch(std::function<void(bool)> watcher) { watchers.push_back(std::move(watcher)); }

    void setAcknowledge(std::function<int()> handler) { ackHandler = std::move(handler); }

    int acknowledge() { return ackHandler ? ackHandler() : -1; }

private:
    bool state = false;
    std::vector<std::function<void(bool)>> watchers;
    std::function<int()> ackHandler;
};

}
