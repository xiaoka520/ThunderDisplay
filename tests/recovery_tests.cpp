#include "recovery.hpp"
#include <cassert>
#include <iostream>
int main() {
    td::RetryBudget budget;
    constexpr uint64_t start=1000000;
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
    assert(!td::VideoHealth::fresh(start,0));
    assert(!td::VideoHealth::fresh(start,start+1));
    assert(td::VideoHealth::fresh(start+td::VideoHealth::StaleAfter-1,start));
    assert(!td::VideoHealth::fresh(start+td::VideoHealth::StaleAfter,start));
    assert(!td::VideoHealth::stalled(start+td::VideoHealth::RestartAfter-1,start));
    assert(td::VideoHealth::stalled(start+td::VideoHealth::RestartAfter,start));
    td::HandoverHold hold;
    assert(!hold.active(start)); hold.begin(start); assert(hold.active(start));
    hold.begin(start+1000000); // A retry/duplicate notice cannot renew the hold.
    assert(hold.active(start+td::HandoverHold::Duration-1));
    assert(!hold.active(start+td::HandoverHold::Duration));
    hold.clear(); assert(!hold.active(start));
    hold.begin(start+td::HandoverHold::Duration); assert(hold.active(start+td::HandoverHold::Duration));
    std::cout<<"Recovery budget and video freshness tests passed\n";
}
