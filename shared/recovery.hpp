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
    void begin(uint64_t now) {
        if(active) return;
        active=true; deadline=now+Duration; next=now; attempts=0;
    }
    bool expired(uint64_t now) const { return active && now>=deadline; }
    bool exhausted(uint64_t now) const { return expired(now) || attempts>=Total; }
    bool startAttempt(uint64_t now) {
        begin(now);
        if(exhausted(now) || now<next) return false;
        ++attempts; next=now+Interval; return true;
    }
    void succeeded() { active=false; attempts=0; deadline=next=0; }
};
struct VideoHealth {
    static constexpr uint64_t StaleAfter=3000000, RestartAfter=12000000;
    static bool fresh(uint64_t now,uint64_t last) { return last && now>=last && now-last<StaleAfter; }
    static bool stalled(uint64_t now,uint64_t last) { return now>=last && now-last>=RestartAfter; }
};
}
