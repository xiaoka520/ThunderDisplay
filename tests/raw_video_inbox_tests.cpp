#include "raw_video_inbox.hpp"
#include <cassert>
#include <future>
#include <iostream>
#include <set>
#include <thread>

int main() {
    td::RawVideoInbox inbox(80);
    std::set<const uint8_t*> allocations;
    auto publish=[&](uint64_t id) {
        auto slot=inbox.acquire(); assert(slot);
        auto& pixels=inbox.pixels(*slot); allocations.insert(pixels.data());
        std::fill(pixels.begin(),pixels.end(),uint8_t(id));
        inbox.revisions(*slot)={id};
        assert(inbox.publish({*slot,id,id+10}));
    };
    publish(1); auto held=inbox.take(); assert(held && held->arrivedAt==1);
    // Receiving continues while the pixel worker holds its current frame.
    auto receiver=std::async(std::launch::async,[&]{for(uint64_t i=2;i<=1000;++i) publish(i);});
    assert(receiver.wait_for(std::chrono::seconds(2))==std::future_status::ready); receiver.get();
    for(auto byte:inbox.pixels(held->slot)) assert(byte==1);
    assert(inbox.revisions(held->slot)==std::vector<uint64_t>{1});
    assert(allocations.size()==3 && inbox.replacedFrames()==998);
    auto newest=inbox.take(); assert(newest && newest->arrivedAt==1000 && newest->receivedAt==1010);
    for(auto byte:inbox.pixels(newest->slot)) assert(byte==uint8_t(1000));
    assert(inbox.revisions(newest->slot)==std::vector<uint64_t>{1000});
    inbox.release(held->slot); inbox.release(newest->slot);
    auto waiting=std::async(std::launch::async,[&]{return inbox.take();});
    inbox.stop(); assert(waiting.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    assert(!waiting.get() && !inbox.acquire());

    // Concurrent reuse must never mutate an in-flight frame or its metadata.
    td::RawVideoInbox concurrent(256);
    std::promise<void> consumed;
    auto finished=consumed.get_future();
    uint64_t count=0,last=0;
    auto consumer=std::async(std::launch::async,[&] {
        while(auto frame=concurrent.take()) {
            assert(frame->arrivedAt>last && frame->receivedAt==frame->arrivedAt+1);
            for(unsigned repeat=0;repeat<3;++repeat) {
                std::this_thread::yield();
                for(auto byte:concurrent.pixels(frame->slot)) assert(byte==uint8_t(frame->arrivedAt));
                assert(concurrent.revisions(frame->slot)==std::vector<uint64_t>{frame->arrivedAt});
            }
            last=frame->arrivedAt; ++count; concurrent.release(frame->slot);
            if(last==5000) { consumed.set_value(); break; }
        }
    });
    for(uint64_t id=1;id<=5000;++id) {
        auto slot=concurrent.acquire(); assert(slot);
        std::fill(concurrent.pixels(*slot).begin(),concurrent.pixels(*slot).end(),uint8_t(id));
        concurrent.revisions(*slot)={id};
        assert(concurrent.publish({*slot,id,id+1}));
        if(id%7==0) std::this_thread::yield();
    }
    auto completed=finished.wait_for(std::chrono::seconds(2))==std::future_status::ready;
    concurrent.stop(); consumer.get(); assert(completed && count>0 && last==5000);

    // Stop can race with a partially received frame without publishing it.
    td::RawVideoInbox cancelled(80); auto receiving=cancelled.acquire(); assert(receiving);
    cancelled.stop(); assert(!cancelled.publish({*receiving,1,2}) && !cancelled.take());
    std::cout<<"Raw video independent reception, three-buffer ownership, latest frame and stop tests passed\n";
}
