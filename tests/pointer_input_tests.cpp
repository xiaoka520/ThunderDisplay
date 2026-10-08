#include "pointer_input.hpp"
#include "protocol.hpp"
#include "control_outbox.hpp"
#include <cassert>
#include <iostream>

int main() {
    using Mode=td::PointerInputMode;
    assert(td::pointerInputMode(true,true,true,true,true)==Mode::Relative);
    assert(td::pointerInputMode(true,false,true,true,true)==Mode::Absolute);
    assert(td::pointerInputMode(false,true,true,true,true)==Mode::Absolute);
    for(bool fullscreen:{false,true}) {
        assert(td::pointerInputMode(true,fullscreen,false,true,true)==Mode::Released);
        assert(td::pointerInputMode(true,fullscreen,true,false,true)==Mode::Released);
        assert(td::pointerInputMode(true,fullscreen,true,true,false)==Mode::Released);
    }
    const uint64_t session=0x0102030405060708;
    const auto notice=td::mouseMode(session,true);
    assert((notice==td::Bytes{20,1,2,3,4,5,6,7,8,1}));
    assert(td::relativeMouseMode(notice,session));
    assert(!td::relativeMouseMode(td::mouseMode(session,false),session));
    auto invalid=notice; invalid.back()=2;
    for(auto bad:{invalid,td::Bytes(notice.begin(),notice.end()-1),td::mouseMode(session+1,true)}) {
        try { td::relativeMouseMode(bad,session); assert(false); } catch(const std::runtime_error&) {}
    }
    td::ControlOutbox queue; queue.allowInput(true);
    // Relative movements are increments. Replacing one loses distance; merging
    // opposite movements can change what happens at a display's outer edge.
    const auto right=td::input(6,0,0,100,0),left=td::input(6,0,0,-100,0),click=td::input(7,0,1);
    queue.push(right); queue.push(left); queue.push(click);
    assert(queue.take()->message==right); assert(queue.take()->message==left); assert(queue.take()->message==click);
    queue.allowInput(false);
    assert(queue.push(right)==td::ControlOutbox::Result::Ignored);
    assert(queue.push(click)==td::ControlOutbox::Result::Ignored);
    assert(queue.push(td::input(5,0,0))==td::ControlOutbox::Result::Queued);
    std::cout<<"Fullscreen/window pointer policy, session validation and relative input ordering passed\n";
}
