#include "latest_picture.hpp"
#include <cassert>
#include <future>
#include <iostream>
#include <memory>

int main() {
    td::LatestPicture<unsigned> pictures;
    assert(pictures.push(1)); auto inFlight=pictures.take(); assert(inFlight && pictures.current(*inFlight));
    std::promise<void> entered,release;
    auto display=std::async(std::launch::async,[&]{entered.set_value(); release.get_future().wait();});
    entered.get_future().wait();
    // A blocked display must not block decoding or retain every old surface.
    auto decode=std::async(std::launch::async,[&]{for(unsigned i=2;i<=1000;++i) assert(pictures.push(i));});
    auto responsive=decode.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    release.set_value(); display.get(); decode.get(); assert(responsive);
    assert(pictures.discardedPictures()==998);
    auto newest=pictures.take(); assert(newest && newest->picture==1000);
    assert(!pictures.take());
    // Session retirement invalidates both a popped item and queued pictures.
    assert(pictures.push(1001)); pictures.retire();
    assert(!pictures.current(*inFlight) && !pictures.current(*newest) && !pictures.take());
    assert(pictures.push(2000)); assert(pictures.take()->picture==2000);
    auto waiter=std::async(std::launch::async,[&]{return pictures.wait(std::chrono::seconds(5));});
    pictures.retire(true); assert(waiter.wait_for(std::chrono::milliseconds(100))==std::future_status::ready);
    assert(!waiter.get() && !pictures.push(2001));
    // Resource destruction may re-enter diagnostics; it must run outside lock.
    td::LatestPicture<std::shared_ptr<unsigned>> resources;
    bool released=false;
    auto first=std::shared_ptr<unsigned>(new unsigned(1),[&](unsigned* p){(void)resources.discardedPictures(); released=true; delete p;});
    resources.push(std::move(first)); resources.push(std::make_shared<unsigned>(2)); assert(released);
    std::cout<<"Latest decoded picture, independent display and session retirement tests passed\n";
}
