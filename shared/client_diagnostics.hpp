#pragma once
#include "protocol.hpp"
#include <mutex>
#include <sstream>
#include <iomanip>

namespace td {
// Opt-in, bounded, performance-only log queue. Never reads historical disk logs.
class RemoteDiagnostics {
    mutable std::mutex mutex;
    bool enabled=false;
    std::deque<std::string> lines;
    unsigned dropped=0;
public:
    static constexpr size_t MaxLines=64,MaxLine=1024,MaxBatch=4000;
    static bool shareable(const std::string& event) {
        return event=="client.start" || event=="client.debug" || event=="network.link" || event=="connection.stream" ||
            event.rfind("video.",0)==0 || event.rfind("display.",0)==0;
    }
    void setEnabled(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        if(enabled!=value) { lines.clear(); dropped=0; enabled=value; }
    }
    bool isEnabled() const { std::lock_guard<std::mutex> lock(mutex); return enabled; }
    void push(const std::string& event,std::string line) {
        if(!shareable(event)) return;
        if(line.size()>MaxLine) line.resize(MaxLine);
        for(auto& c:line) if(static_cast<unsigned char>(c)<32 || static_cast<unsigned char>(c)>126) c=' ';
        std::lock_guard<std::mutex> lock(mutex);
        if(!enabled) return;
        if(lines.size()==MaxLines) { lines.pop_front(); ++dropped; }
        lines.push_back(std::move(line));
    }
    std::string take() {
        std::lock_guard<std::mutex> lock(mutex);
        if(!enabled) return {};
        std::string batch;
        if(dropped) { batch="diagnostics.queue dropped="+std::to_string(dropped)+"\n"; dropped=0; }
        size_t count=0;
        while(!lines.empty() && count<32 && batch.size()+lines.front().size()+1<=MaxBatch) {
            batch+=lines.front(); batch+='\n'; lines.pop_front(); ++count;
        }
        return batch;
    }
};
// state 0=stop, 1=start, 2=log batch; reliable, authenticated control session.
inline Bytes clientDiagnostics(uint64_t session,uint8_t state,const std::string& text={}) {
    if(!session || state>2 || (state<2 && !text.empty()) || (state==2 && (text.empty() || text.size()>RemoteDiagnostics::MaxBatch)))
        throw std::runtime_error("Invalid client diagnostic packet");
    Writer w; w.put(uint8_t(Message::ClientDiagnostics)); w.put(uint8_t(1)); w.put(session); w.put(state);
    w.data.insert(w.data.end(),text.begin(),text.end()); return std::move(w.data);
}
struct PerformanceCounters { uint64_t received=0,bytes=0,uploaded=0,presented=0,dropped=0,replaced=0,gaps=0; };
struct PerformanceWindow {
    uint64_t since=0;
    PerformanceCounters previous;
    void reset(uint64_t now,PerformanceCounters value) { since=now; previous=value; }
    std::string sample(uint64_t now,PerformanceCounters value,uint64_t maximumGap,uint16_t target,bool visible,double dxgiFPS) {
        const auto elapsed=now>since?now-since:0;
        auto difference=[](uint64_t a,uint64_t b) { return a>=b?a-b:0; };
        const auto rx=difference(value.received,previous.received),uploaded=difference(value.uploaded,previous.uploaded),presented=difference(value.presented,previous.presented);
        std::ostringstream out; out<<std::fixed<<std::setprecision(2);
        auto rate=[&](uint64_t count) { return elapsed?double(count)*1000000/double(elapsed):0; };
        out<<"window_us="<<elapsed<<" target_fps="<<target<<" receive_fps="<<rate(rx)<<" upload_fps="<<rate(uploaded)
            <<" present_fps="<<rate(presented)<<" dxgi_display_fps="<<dxgiFPS
            <<" receive_Gbps="<<(elapsed?double(difference(value.bytes,previous.bytes))*8/double(elapsed)/1000:0)
            <<" received="<<rx<<" uploaded="<<uploaded<<" presented="<<presented
            <<" busy_dropped="<<difference(value.dropped,previous.dropped)<<" pending_replaced="<<difference(value.replaced,previous.replaced)
            <<" late_intervals="<<difference(value.gaps,previous.gaps)<<" max_present_interval_us="<<maximumGap<<" visible="<<visible;
        reset(now,value); return out.str();
    }
};
}
