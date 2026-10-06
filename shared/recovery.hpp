#pragma once
#include <cstdint>

namespace td {
// A round includes discovery, connection, negotiation and retry waits.
struct RetryBudget {
    static constexpr uint64_t Duration=600000000, Interval=4000000;
    static constexpr unsigned Total=unsigned(Duration/Interval);
    uint64_t deadline=0, next=0;
    unsigned attempts=0;
    bool active=false;
    bool resumeStream=false;
    void begin(uint64_t now) {
        if(active) return;
        active=true; deadline=now+Duration; next=now; attempts=0;
    }
    bool expired(uint64_t now) const { return active && now>=deadline; }
    bool exhausted(uint64_t now) const { return expired(now) || attempts>=Total; }
    bool startAttempt(uint64_t now) {
        begin(now);
        if(exhausted(now) || now<next) return false;
        ++attempts; next=now+(resumeStream && attempts<=8 ? 250000 : Interval); return true;
    }
    void succeeded() { active=false; attempts=0; deadline=next=0; }
    void streamDisplayed() { resumeStream=true; }
};
struct VideoHealth {
    static constexpr uint64_t StaleAfter=3000000, RestartAfter=12000000;
    static bool fresh(uint64_t now,uint64_t last) { return last && now>=last && now-last<StaleAfter; }
    static bool stalled(uint64_t now,uint64_t last) { return now>=last && now-last>=RestartAfter; }
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
