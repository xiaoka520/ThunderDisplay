#pragma once
#include "protocol.hpp"
#include <condition_variable>
#include <mutex>
#include <chrono>

namespace td {
// The socket thread never waits for a codec/GPU operation. Compressed frames
// cannot be dropped like decoded images: retire the chain and wait for an IDR.
class VideoInbox {
public:
    struct Item { Frame frame; uint64_t generation,queuedAt; bool flush; };
private:
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<Item> pending;
    uint64_t generation=0, discarded=0;
    bool stopped=false,waitingKey=true,flushRequired=false,request=false;
    void retireLocked() {
        discarded+=pending.size(); pending.clear(); ++generation;
        waitingKey=true; flushRequired=true; request=true;
    }
public:
    bool push(Frame frame,uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped) return false;
        if(pending.size()>=2) retireLocked();
        if(waitingKey && !frame.key) { ++discarded; request=true; return false; }
        waitingKey=false;
        pending.push_back({std::move(frame),generation,now,flushRequired}); flushRequired=false;
        changed.notify_one(); return true;
    }
    std::optional<Item> take(std::chrono::milliseconds wait=std::chrono::milliseconds(2)) {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock,wait,[&]{return stopped || !pending.empty();});
        if(stopped || pending.empty()) return std::nullopt;
        auto item=std::move(pending.front()); pending.pop_front(); return item;
    }
    bool current(const Item& item) const {
        std::lock_guard<std::mutex> lock(mutex); return !stopped && item.generation==generation;
    }
    void requestKey() { std::lock_guard<std::mutex> lock(mutex); if(!stopped) retireLocked(); }
    bool takeIDRRequest() { std::lock_guard<std::mutex> lock(mutex); auto value=request; request=false; return value; }
    uint64_t discardedFrames() const { std::lock_guard<std::mutex> lock(mutex); return discarded; }
    void stop() { std::lock_guard<std::mutex> lock(mutex); stopped=true; pending.clear(); ++generation; changed.notify_all(); }
};
}
