#include "scheduler.hpp"

namespace solme {

u64 Scheduler::schedule(u64 delayNs, Callback callback) {
    u64 id = nextId++;
    queue.push(Event{currentNs + delayNs, id, std::move(callback)});
    return id;
}

void Scheduler::cancel(u64 id) { cancelled.insert(id); }

void Scheduler::dropCancelled() {
    while (!queue.empty() && cancelled.count(queue.top().id)) {
        cancelled.erase(queue.top().id);
        queue.pop();
    }
}

bool Scheduler::hasEvents() {
    dropCancelled();
    return !queue.empty();
}

u64 Scheduler::nextEventTime() {
    dropCancelled();
    return queue.empty() ? ~0ULL : queue.top().when;
}

void Scheduler::advance(u64 ns) {
    u64 target = currentNs + ns;
    while (true) {
        dropCancelled();
        if (queue.empty() || queue.top().when > target) {
            break;
        }
        Event event = queue.top();
        queue.pop();
        currentNs = event.when;
        event.callback();
    }
    currentNs = target;
}

void Scheduler::advanceToNextEvent() {
    dropCancelled();
    if (queue.empty()) {
        return;
    }
    u64 when = queue.top().when;
    if (when > currentNs) {
        advance(when - currentNs);
    } else {
        advance(0);
    }
}

}
