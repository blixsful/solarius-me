#pragma once
#include <functional>
#include <queue>
#include <unordered_set>
#include <vector>
#include "types.hpp"

namespace solme {

class Scheduler {
public:
    using Callback = std::function<void()>;

    u64 now() const { return currentNs; }
    u64 schedule(u64 delayNs, Callback callback);
    void cancel(u64 id);
    bool hasEvents();
    u64 nextEventTime();
    void advance(u64 ns);
    void advanceToNextEvent();

private:
    struct Event {
        u64 when;
        u64 id;
        Callback callback;
        bool operator>(const Event& other) const {
            return when != other.when ? when > other.when : id > other.id;
        }
    };

    void dropCancelled();

    u64 currentNs = 0;
    u64 nextId = 1;
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> queue;
    std::unordered_set<u64> cancelled;
};

}
