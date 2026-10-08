#pragma once
#include "protocol.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace td {
// One writer owns TCP framing. Video decoding never owns this queue or lock.
class ControlOutbox {
public:
    struct Item { Bytes message; uint64_t generation; };
    enum class Result { Queued, Ignored, Full };
private:
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Bytes> priority, clipboard;
    std::optional<Bytes> diagnostics;
    uint64_t generation=0;
    bool inputEnabled=false, stopped=false;
public:
    Result push(Bytes message) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped || message.empty()) return Result::Ignored;
        if(message[0]==uint8_t(Message::ClientDiagnostics) && message.size()>=11) {
            if(message[10]==2) { diagnostics=std::move(message); changed.notify_one(); return Result::Queued; }
            diagnostics.reset();
        }
        // Releasing held input is safe while live-video input is paused.
        const bool release=message.size()==14 && message[0]==uint8_t(Message::Input) && message[1]==5;
        if(message[0]==uint8_t(Message::Input) && !inputEnabled && !release) return Result::Ignored;
        // Only adjacent moves can coalesce; retain key/button/release ordering.
        if(message.size()==14 && message[0]==uint8_t(Message::Input) && message[1]==1 &&
           !priority.empty() && priority.back().size()==14 &&
           priority.back()[0]==uint8_t(Message::Input) && priority.back()[1]==1) priority.back()=std::move(message);
        else {
            if(priority.size()>=256) return Result::Full;
            priority.push_back(std::move(message));
        }
        changed.notify_one(); return Result::Queued;
    }
    void allowInput(bool value) { std::lock_guard<std::mutex> lock(mutex); inputEnabled=value; }
    void replaceClipboard(std::deque<Bytes> packets) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped) return;
        clipboard=std::move(packets); changed.notify_one();
    }
    void clearClipboard() { std::lock_guard<std::mutex> lock(mutex); clipboard.clear(); }
    void transition(Bytes acknowledgment) {
        std::lock_guard<std::mutex> lock(mutex);
        if(stopped) return;
        inputEnabled=false; ++generation; priority.clear(); clipboard.clear(); diagnostics.reset();
        priority.push_back(std::move(acknowledgment)); changed.notify_one();
    }
    std::optional<Item> take() {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock,std::chrono::milliseconds(2),[&]{return stopped || !priority.empty() || diagnostics || !clipboard.empty();});
        if(stopped) return std::nullopt;
        if(priority.empty() && diagnostics) { Item item{std::move(*diagnostics),generation}; diagnostics.reset(); return item; }
        auto& queue=priority.empty()?clipboard:priority;
        if(queue.empty()) return std::nullopt;
        Item item{std::move(queue.front()),generation}; queue.pop_front(); return item;
    }
    bool current(const Item& item) { std::lock_guard<std::mutex> lock(mutex); return !stopped && item.generation==generation; }
    void stop() {
        std::lock_guard<std::mutex> lock(mutex);
        stopped=true; inputEnabled=false; ++generation; priority.clear(); clipboard.clear(); diagnostics.reset(); changed.notify_all();
    }
};
}
