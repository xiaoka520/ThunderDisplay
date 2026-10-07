#pragma once
#include <cstdint>
#include <atomic>

namespace td {
// A round includes discovery, connection, negotiation and retry waits.
struct RetryBudget {
    static constexpr uint64_t Duration=600000000, Interval=4000000;
    static constexpr uint64_t HandoverDuration=15000000, HandoverInterval=250000;
    static constexpr unsigned Total=unsigned(Duration/Interval);
    uint64_t deadline=0, next=0, handoverUntil=0;
    unsigned attempts=0;
    bool active=false;
    bool resumeStream=false;
    void begin(uint64_t now) {
        if(active) return;
        active=true; deadline=now+Duration; next=now; attempts=0;
    }
    bool expired(uint64_t now) const { return active && now>=deadline; }
    bool exhausted(uint64_t now) const { return expired(now) || attempts>=Total; }
    bool fastHandover(uint64_t now) const { return handoverUntil && now<handoverUntil; }
    void sessionTransition(uint64_t now) {
        // A duplicate notice must not renew either recovery or its fast phase.
        if(handoverUntil) return;
        handoverUntil=now+HandoverDuration;
        next=now;
    }
    bool startAttempt(uint64_t now) {
        begin(now);
        if(exhausted(now) || now<next) return false;
        ++attempts;
        auto interval=handoverUntil ? (fastHandover(now)?HandoverInterval:Interval) :
            (resumeStream && attempts<=8 ? HandoverInterval : Interval);
        next=now+interval; return true;
    }
    void succeeded() { active=false; attempts=0; deadline=next=handoverUntil=0; }
    void streamDisplayed() { resumeStream=true; }
};
struct VideoHealth {
    static constexpr uint64_t StaleAfter=3000000, RestartAfter=12000000;
    static bool fresh(uint64_t now,uint64_t last) { return last && now>=last && now-last<StaleAfter; }
    static bool stalled(uint64_t now,uint64_t last) { return now>=last && now-last>=RestartAfter; }
};
// A saved image or a new TCP handshake cannot renew this deadline. Only a
// newly decoded frame may bring the remote window back from the setup screen.
struct RemoteWindowRecovery {
    static constexpr uint64_t Timeout=3000000;
    enum class Action { None, ReturnToSetup, RestoreRemote };
    uint64_t lastVideo=0, unavailableSince=0;
    bool watching=false, returned=false;
    void begin(uint64_t now) { lastVideo=now; unavailableSince=now; watching=true; returned=false; }
    void clear() { lastVideo=unavailableSince=0; watching=returned=false; }
    Action update(uint64_t now,uint64_t decodedAt,bool connected,uint64_t peerActivityAt=0) {
        if(!watching) return Action::None;
        const bool newVideo=connected && decodedAt>lastVideo && VideoHealth::fresh(now,decodedAt);
        if(newVideo) {
            lastVideo=decodedAt; unavailableSince=0;
            if(returned) { returned=false; return Action::RestoreRemote; }
        }
        if(!connected && !unavailableSince) unavailableSince=now;
        if(connected && peerActivityAt && now>=peerActivityAt && now-peerActivityAt>=Timeout &&
            !VideoHealth::fresh(now,decodedAt) && !unavailableSince) unavailableSince=peerActivityAt;
        // A static desktop in a healthy session must not close its window.
        // Once recovery starts, handshakes cannot renew the outage budget.
        if(!returned && unavailableSince && now>=unavailableSince && now-unavailableSince>=Timeout) {
            returned=true; return Action::ReturnToSetup;
        }
        return Action::None;
    }
};
// Visible pixels and live-stream health have different lifetimes. Repainting
// saved pixels must never refresh the clock that authorizes remote input.
class DisplayedFrame {
    enum class Kind { Empty, Live, Held };
    std::atomic<Kind> kind{Kind::Empty};
    std::atomic<uint64_t> time{0};
public:
    bool present(uint64_t now) {
        time=now;
        return kind.exchange(Kind::Live)!=Kind::Live;
    }
    void reset(bool hold) { kind=hold?Kind::Held:Kind::Empty; }
    bool hasImage() const { return kind!=Kind::Empty; }
    bool held() const { return kind==Kind::Held; }
    bool fresh(uint64_t now) const { return kind==Kind::Live && VideoHealth::fresh(now,time); }
    uint64_t lastPresentation() const { return time; }
};
// Explicit host notice only. Repeated reconnects cannot extend a frozen image
// indefinitely; it is not a live frame and must never permit remote input.
struct HandoverHold {
    static constexpr uint64_t Duration=30000000;
    uint64_t deadline=0;
    void begin(uint64_t now) { if(!deadline) deadline=now+Duration; }
    bool active(uint64_t now) const { return deadline && now<deadline; }
    void clear() { deadline=0; }
};
}
