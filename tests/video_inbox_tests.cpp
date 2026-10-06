#include "video_inbox.hpp"
#include <cassert>
#include <future>
#include <iostream>

static td::Frame frame(uint32_t id,bool key=false) { return {{1,2,3},id,id*1000,key}; }
int main() {
    td::VideoInbox inbox;
    assert(!inbox.push(frame(1),100)); // No references before the first IDR.
    assert(inbox.takeIDRRequest());
    assert(inbox.push(frame(2,true),200));
    auto first=inbox.take(); assert(first && first->frame.key && !first->flush);
    // Pretend Present is stalled. A receiver still returns immediately and
    // cannot build an unbounded queue of dependent frames.
    std::promise<void> blocked,release;
    auto consumer=std::async(std::launch::async,[&]{blocked.set_value(); release.get_future().wait();});
    blocked.get_future().wait();
    auto receiver=std::async(std::launch::async,[&]{
        assert(inbox.push(frame(3),300)); assert(inbox.push(frame(4),400));
        assert(!inbox.push(frame(5),500));
        assert(!inbox.push(frame(6),600)); return true;
    });
    const bool responsive=receiver.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    release.set_value(); consumer.get(); assert(responsive && receiver.get());
    assert(!inbox.current(*first)); assert(inbox.takeIDRRequest());
    assert(!inbox.take(std::chrono::milliseconds(0)));
    assert(inbox.push(frame(7,true),700));
    auto recovery=inbox.take(); assert(recovery && recovery->flush && recovery->frame.id==7);
    assert(inbox.push(frame(8),800)); assert(inbox.push(frame(9),900));
    // A replacement IDR can resume immediately even when the inbox was full.
    assert(inbox.push(frame(10,true),1000));
    auto replacement=inbox.take(); assert(replacement && replacement->flush && replacement->frame.id==10);
    assert(!inbox.current(*recovery)); assert(inbox.discardedFrames()==7);
    inbox.requestKey(); assert(inbox.takeIDRRequest());
    assert(!inbox.push(frame(11),1100)); assert(inbox.push(frame(12,true),1200));
    auto lost=inbox.take(); assert(lost && lost->flush);
    // A session retirement wakes a waiting media thread and rejects old work.
    auto waiter=std::async(std::launch::async,[&]{return inbox.take(std::chrono::seconds(5));});
    inbox.stop(); assert(waiter.wait_for(std::chrono::milliseconds(100))==std::future_status::ready);
    assert(!waiter.get() && !inbox.current(*lost)); assert(!inbox.push(frame(13,true),1300));
    std::cout<<"Bounded asynchronous video inbox and dependency recovery tests passed\n";
}
