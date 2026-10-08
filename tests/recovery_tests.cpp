#include "recovery.hpp"
#include <cassert>
#include <iostream>
int main() {
    td::RetryBudget budget;
    constexpr uint64_t start=1000000;
    assert(!td::VideoHealth::firstFrameTimedOut(start+12000000,start,false,0));
    assert(td::VideoHealth::firstFrameTimedOut(start+12000001,start,false,start+12000001));
    assert(!td::VideoHealth::firstFrameTimedOut(start+15000000,start,true,start+14000000));
    assert(td::VideoHealth::firstFrameTimedOut(start+15000000,start,true,0));
    assert(td::VideoHealth::firstFrameTimedOut(start+15000000,start,true,start+10000000));
    assert(!td::VideoHealth::firstFrameTimedOut(start+15000000,start,true,start+10000001));
    assert(td::VideoHealth::firstFrameTimedOut(start+15000000,start,true,start+15000001));
    assert(!td::VideoHealth::firstFrameTimedOut(start+59999999,start,true,start+59999999));
    assert(td::VideoHealth::firstFrameTimedOut(start+60000000,start,true,start+60000000));
    using WindowAction=td::RemoteWindowRecovery::Action;
    td::RemoteWindowRecovery window;
    assert(window.update(start+10000000,0,false)==WindowAction::None);
    window.begin(start);
    assert(window.update(start+2999999,0,false)==WindowAction::None);
    assert(window.update(start+3000000,0,false)==WindowAction::ReturnToSetup);
    assert(window.update(start+4000000,0,true)==WindowAction::None); // TCP alone isn't recovery.
    assert(window.update(start+5000000,start+1,true)==WindowAction::None); // Stale decoded image.
    assert(window.update(start+5000000,start+5000000,true)==WindowAction::RestoreRemote);
    assert(window.update(start+5000001,start+5000000,true)==WindowAction::None);
    assert(window.update(start+6000000,start+5000000,false)==WindowAction::None);
    assert(window.update(start+7000000,start+7000000,true)==WindowAction::None); // Short outage stays remote.
    assert(window.update(start+17000000,start+7000000,true)==WindowAction::None); // Static desktop is connected.
    assert(window.update(start+17000001,0,false)==WindowAction::None);
    assert(window.update(start+20000000,0,false)==WindowAction::None);
    assert(window.update(start+20000001,0,true)==WindowAction::ReturnToSetup); // Handshake cannot renew deadline.
    assert(window.update(start+20000002,start+7000000,true)==WindowAction::None); // Repaint isn't new video.
    window.clear();
    assert(window.update(start+20000000,start+20000000,true)==WindowAction::None); // Manual disconnect never reopens.
    window.begin(start);
    assert(window.update(start+1,start+1,true,start+1)==WindowAction::None);
    assert(window.update(start+10000000,start+1,true,start+10000000)==WindowAction::None); // Idle video, live heartbeat.
    assert(window.update(start+12999999,start+1,true,start+10000000)==WindowAction::None);
    assert(window.update(start+13000000,start+1,true,start+10000000)==WindowAction::ReturnToSetup); // Cable loss; TCP may not have failed yet.
    assert(window.update(start+13000001,start+1,true,start+13000001)==WindowAction::None); // Heartbeat isn't a new desktop.
    assert(window.update(start+13000002,start+13000002,true,start+13000002)==WindowAction::RestoreRemote);
    for(unsigned i=0;i<td::RetryBudget::Total;++i) {
        assert(budget.startAttempt(start+i*td::RetryBudget::Interval));
        assert(budget.attempts==i+1);
        assert(!budget.expired(start+i*td::RetryBudget::Interval));
    }
    assert(!budget.startAttempt(start+td::RetryBudget::Duration-1));
    assert(budget.exhausted(start+td::RetryBudget::Duration-1));
    budget.succeeded();
    assert(budget.startAttempt(start));
    assert(!budget.startAttempt(start+td::RetryBudget::Interval-1));
    // Discovery, handshake and a hung video stream cannot extend the round.
    assert(budget.expired(start+td::RetryBudget::Duration));
    assert(!budget.startAttempt(start+td::RetryBudget::Duration));
    budget.succeeded();
    assert(budget.startAttempt(start+td::RetryBudget::Duration));
    assert(budget.attempts==1);
    td::RetryBudget handover;
    handover.streamDisplayed();
    assert(handover.startAttempt(start));
    assert(!handover.startAttempt(start+249999));
    assert(handover.startAttempt(start+250000));
    for(unsigned i=2;i<8;++i) assert(handover.startAttempt(start+i*250000));
    assert(handover.startAttempt(start+8*250000));
    assert(!handover.startAttempt(start+8*250000+td::RetryBudget::Interval-1));
    assert(handover.expired(start+td::RetryBudget::Duration));
    // An authenticated login transition needs a time window, not just eight
    // fast attempts: a desktop that appears at 5–15 seconds must not wait 4s.
    td::RetryBudget login;
    login.streamDisplayed(); login.sessionTransition(start);
    for(unsigned i=0;i<60;++i) {
        assert(login.startAttempt(start+i*td::RetryBudget::HandoverInterval));
        assert(login.fastHandover(start+i*td::RetryBudget::HandoverInterval));
        assert(!login.startAttempt(start+i*td::RetryBudget::HandoverInterval+1));
        login.sessionTransition(start+i*td::RetryBudget::HandoverInterval+1);
        assert(login.handoverUntil==start+td::RetryBudget::HandoverDuration);
        assert(login.deadline==start+td::RetryBudget::Duration);
    }
    const auto end=start+td::RetryBudget::HandoverDuration;
    assert(!login.fastHandover(end)); assert(login.startAttempt(end));
    assert(!login.startAttempt(end+td::RetryBudget::Interval-1));
    assert(login.startAttempt(end+td::RetryBudget::Interval));
    while(login.attempts<td::RetryBudget::Total) assert(login.startAttempt(login.next));
    login.sessionTransition(login.next);
    assert(!login.startAttempt(login.next)); // Transition never resets total.
    login.succeeded(); assert(!login.fastHandover(end));
    assert(login.startAttempt(end)); assert(login.attempts==1);
    td::RetryBudget delayed;
    assert(delayed.startAttempt(start));
    delayed.sessionTransition(start+1000000);
    assert(delayed.startAttempt(start+1000000)); // Skip an already pending wait.
    assert(delayed.attempts==2 && delayed.deadline==start+td::RetryBudget::Duration);
    assert(delayed.expired(start+td::RetryBudget::Duration));
    assert(!td::VideoHealth::fresh(start,0));
    assert(!td::VideoHealth::fresh(start,start+1));
    assert(td::VideoHealth::fresh(start+td::VideoHealth::StaleAfter-1,start));
    assert(!td::VideoHealth::fresh(start+td::VideoHealth::StaleAfter,start));
    assert(!td::VideoHealth::stalled(start+td::VideoHealth::RestartAfter-1,start));
    assert(td::VideoHealth::stalled(start+td::VideoHealth::RestartAfter,start));
    td::DisplayedFrame image;
    assert(!image.hasImage() && !image.fresh(start));
    assert(image.present(start));
    assert(!image.present(start+1)); // A normal frame does not reopen the UI.
    assert(image.hasImage() && image.fresh(start+2));
    const auto last=start+1;
    assert(!image.fresh(last+td::VideoHealth::StaleAfter));
    assert(image.hasImage()); // Freshness expiration must not paint black.
    image.reset(true); // Brief interruption or authenticated session handover.
    assert(image.held() && image.hasImage());
    assert(!image.fresh(last+2)); // Saved pixels never authorize input.
    for(unsigned i=0;i<100;++i) {
        assert(image.hasImage()); // UI repaint/resize leaves the source clock alone.
        assert(image.lastPresentation()==last);
    }
    assert(td::VideoHealth::stalled(last+td::VideoHealth::RestartAfter,image.lastPresentation()));
    image.reset(false); // Actual disconnect/stall deadline clears visibility.
    assert(!image.hasImage() && !image.held() && !image.fresh(last+2));
    assert(image.present(last+td::VideoHealth::RestartAfter+1));
    assert(image.fresh(last+td::VideoHealth::RestartAfter+2));
    image.reset(true);
    assert(image.present(last+td::VideoHealth::RestartAfter+3)); // Resume notifies UI.
    assert(!image.held());
    td::HandoverHold hold;
    assert(!hold.active(start)); hold.begin(start); assert(hold.active(start));
    hold.begin(start+1000000); // A retry/duplicate notice cannot renew the hold.
    assert(hold.active(start+td::HandoverHold::Duration-1));
    assert(!hold.active(start+td::HandoverHold::Duration));
    hold.clear(); assert(!hold.active(start));
    hold.begin(start+td::HandoverHold::Duration); assert(hold.active(start+td::HandoverHold::Duration));
    std::cout<<"Recovery budget and video freshness tests passed\n";
}
