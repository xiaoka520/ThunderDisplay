#include "control_outbox.hpp"
#include <cassert>
#include <future>
#include <iostream>

int main() {
    td::ControlOutbox queue;
    using Result=td::ControlOutbox::Result;
    // The frozen display gates input, never the handshake that resumes video.
    assert(queue.push(td::input(3,65,1))==Result::Ignored);
    assert(queue.push({uint8_t(td::Message::Ready)})==Result::Queued);
    assert(queue.take()->message[0]==uint8_t(td::Message::Ready));
    queue.allowInput(true);
    queue.push(td::input(1,0,0,10,10)); queue.push(td::input(1,0,0,20,20));
    queue.push(td::input(2,0,1,20,20)); queue.push(td::input(3,65,1)); queue.push(td::input(3,65,0));
    assert(queue.take()->message==td::input(1,0,0,20,20));
    assert(queue.take()->message==td::input(2,0,1,20,20));
    assert(queue.take()->message==td::input(3,65,1));
    assert(queue.take()->message==td::input(3,65,0));
    queue.replaceClipboard({{uint8_t(td::Message::ClipboardImage),1},{uint8_t(td::Message::ClipboardImage),2}});
    queue.push(td::input(3,66,1));
    auto retired=queue.take();
    queue.push(td::input(3,66,0));
    const auto ack=td::sessionTransition(123,true);
    queue.transition(ack);
    assert(!queue.current(*retired)); // Drop an old frame if no bytes were sent.
    assert(queue.take()->message==ack);
    assert(!queue.take()); // No old input or clipboard remains after the ACK.
    assert(queue.push(td::input(3,67,1))==Result::Ignored);
    queue.push({uint8_t(td::Message::Ready)});
    assert(queue.take()->message[0]==uint8_t(td::Message::Ready));
    queue.allowInput(true);
    // A blocked video consumer cannot block the independent control writer.
    std::promise<void> unblockVideo, videoBlocked;
    auto video=std::async(std::launch::async,[&]{videoBlocked.set_value(); unblockVideo.get_future().wait();});
    videoBlocked.get_future().wait();
    auto writer=std::async(std::launch::async,[&]{while(true) if(auto item=queue.take()) return item->message;});
    queue.push(td::input(3,65,1));
    const bool sent=writer.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    unblockVideo.set_value(); video.get();
    assert(sent && writer.get()==td::input(3,65,1));
    for(unsigned i=0;i<256;++i) assert(queue.push(td::input(3,65,i&1))==Result::Queued);
    assert(queue.push(td::input(3,65,0))==Result::Full);
    queue.stop(); assert(!queue.take());
    assert(queue.push({uint8_t(td::Message::Ready)})==Result::Ignored);
    td::ControlOutbox prioritization; prioritization.allowInput(true);
    prioritization.replaceClipboard({{uint8_t(td::Message::ClipboardImage),1}});
    prioritization.push(td::input(3,65,1));
    assert(prioritization.take()->message==td::input(3,65,1));
    assert(prioritization.take()->message[0]==uint8_t(td::Message::ClipboardImage));
    std::cout<<"Independent control writer and handover handshake tests passed\n";
}
