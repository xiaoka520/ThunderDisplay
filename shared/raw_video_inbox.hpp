#pragma once
#include "protocol.hpp"
#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace td {
// One receiver and one pixel worker own separate buffers. The third holds the
// latest complete frame; replacing it never touches a buffer still in use.
class RawVideoInbox {
public:
    struct Frame { size_t slot; uint64_t arrivedAt,receivedAt; };
private:
    std::array<Bytes,3> buffers;
    std::array<std::vector<uint64_t>,3> tileRevisions;
    std::array<bool,3> used{};
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::optional<Frame> pending;
    bool stopped=false;
    uint64_t replaced=0;
public:
    explicit RawVideoInbox(size_t frameBytes) {
        for(auto& buffer:buffers) buffer.resize(frameBytes);
    }
    std::optional<size_t> acquire() {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped) return {};
        for(size_t slot=0;slot<buffers.size();++slot) if(!used[slot]) { used[slot]=true; return slot; }
        throw std::logic_error("Raw video buffer pool exhausted");
    }
    Bytes& pixels(size_t slot) { return buffers.at(slot); }
    std::vector<uint64_t>& revisions(size_t slot) { return tileRevisions.at(slot); }
    void release(size_t slot) {
        std::lock_guard<std::mutex> lock(mutex); used.at(slot)=false;
    }
    bool publish(Frame frame) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(stopped) { used.at(frame.slot)=false; return false; }
            if(pending) { used[pending->slot]=false; ++replaced; }
            pending=frame;
        }
        changed.notify_one(); return true;
    }
    std::optional<Frame> take() {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock,[&]{return stopped || pending.has_value();});
        if(stopped) return {};
        auto frame=pending; pending.reset(); return frame;
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex); stopped=true;
            if(pending) { used[pending->slot]=false; pending.reset(); }
        }
        changed.notify_all();
    }
    uint64_t replacedFrames() const { std::lock_guard<std::mutex> lock(mutex); return replaced; }
};
}
