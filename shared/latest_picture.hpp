#pragma once
#include <condition_variable>
#include <mutex>
#include <optional>
#include <chrono>
#include <cstdint>
#include <utility>

namespace td {
// Decoded pictures have no display-stage dependencies. While the display waits,
// keep only the newest and release previous samples outside the mailbox lock.
template<class Picture> class LatestPicture {
public:
    struct Item { Picture picture; uint64_t generation; };
private:
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::optional<Item> pending;
    uint64_t generation=0,discarded=0;
    bool stopped=false;
public:
    bool push(Picture picture) {
        std::optional<Item> retired;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(stopped) return false;
            if(pending) ++discarded;
            retired.swap(pending); pending.emplace(Item{std::move(picture),generation});
        }
        changed.notify_one(); return true;
    }
    bool wait(std::chrono::milliseconds timeout=std::chrono::milliseconds(20)) {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock,timeout,[&]{return stopped || pending.has_value();});
        return !stopped && pending.has_value();
    }
    std::optional<Item> take() {
        std::lock_guard<std::mutex> lock(mutex);
        auto item=std::move(pending); pending.reset(); return item;
    }
    bool current(const Item& item) const {
        std::lock_guard<std::mutex> lock(mutex); return !stopped && item.generation==generation;
    }
    void retire(bool close=false) {
        std::optional<Item> retired;
        { std::lock_guard<std::mutex> lock(mutex); ++generation; stopped=stopped || close; retired.swap(pending); }
        changed.notify_all();
    }
    uint64_t discardedPictures() const { std::lock_guard<std::mutex> lock(mutex); return discarded; }
};
}
