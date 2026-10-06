#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace td {
using Bytes = std::vector<uint8_t>;
constexpr uint16_t Port = 47990;
constexpr size_t HeaderSize = 40, FragmentSize = 1160, LegacyMaxFrameSize = 4 * 1024 * 1024, GigabitMaxFrameSize=16*1024*1024, MaxFrameSize = 64 * 1024 * 1024, ControlLimit = 4096;
constexpr uint64_t LegacyMaxBitrate=1000000000, MaxBitrate = 20000000000ULL;
enum class Message : uint8_t { Hello=1, Welcome, Input, RequestIDR, Ping, Pong, Failure, Ready, CapabilityQuery, Capabilities, ClipboardControl, ClipboardText, CursorImage, ClipboardImage, HelloWide, WelcomeWide, SessionTransition, SessionTransitionAck, VideoStatistics };
enum class Codec : uint8_t { H264=1, HEVC=2, HEVC10=4 };

// Optional UDP return-path probe; the existing TCP/UDP video wire format is unchanged.
inline std::string videoProbe(uint64_t session) { return "TDVIDEO1 "+std::to_string(session); }
inline std::optional<uint16_t> videoProbePort(const std::string& reply,uint64_t session) {
    auto prefix=videoProbe(session)+" ";
    if(reply.compare(0,prefix.size(),prefix)!=0) return {};
    unsigned port=0; auto end=reply.data()+reply.size();
    auto result=std::from_chars(reply.data()+prefix.size(),end,port);
    if(result.ec!=std::errc{} || result.ptr!=end || port==0 || port>65535) return {};
    return uint16_t(port);
}
enum class VideoStage { NoPackets, IncompleteFrame, DecoderInput, DecoderOutput, Presentation, Playing };
inline VideoStage videoStage(uint64_t packets,uint64_t frames,uint64_t submitted,uint64_t decoded,bool presented) {
    if(presented) return VideoStage::Playing;
    if(!packets) return VideoStage::NoPackets;
    if(!frames) return VideoStage::IncompleteFrame;
    if(!submitted) return VideoStage::DecoderInput;
    if(!decoded) return VideoStage::DecoderOutput;
    return VideoStage::Presentation;
}

struct Writer {
    Bytes data;
    template<typename T> void put(T v) {
        using U = std::make_unsigned_t<T>; auto bits = static_cast<U>(v);
        for (size_t i=sizeof(T); i>0; --i) data.push_back(uint8_t(bits >> ((i-1)*8)));
    }
    void bytes(const Bytes& b) { data.insert(data.end(), b.begin(), b.end()); }
};
struct Reader {
    const uint8_t* data; size_t size, pos=0;
    explicit Reader(const Bytes& b): data(b.data()), size(b.size()) {}
    Reader(const uint8_t* p, size_t n): data(p), size(n) {}
    template<typename T> T get() {
        if (sizeof(T)>size-pos) throw std::runtime_error("Truncated packet");
        using U = std::make_unsigned_t<T>; U value=0;
        for(size_t i=0; i<sizeof(T); ++i) value = U((value << 8) | data[pos++]);
        T result; std::memcpy(&result, &value, sizeof(T)); return result;
    }
    bool end() const { return pos==size; }
};
inline Bytes sessionTransition(uint64_t session,bool acknowledgment=false) {
    Writer w; w.put(uint8_t(acknowledgment?Message::SessionTransitionAck:Message::SessionTransition));
    w.put(session); w.put(uint8_t(1)); return w.data;
}
inline bool validSessionTransition(const Bytes& data,uint64_t session,bool acknowledgment=false) {
    return session && data==sessionTransition(session,acknowledgment);
}
struct Settings {
    uint16_t width=2560, height=1600, fps=120;
    uint64_t bitrate=120000000;
    uint8_t codecMask=3,bitDepth=8;
};
inline Bytes hello(const Settings& s, uint16_t udpPort, const std::string& token,bool wide=false) {
    if(!token.empty() && token.size()!=32) throw std::runtime_error("Pairing code must contain 32 hex characters");
    if(s.bitrate<10000000 || s.bitrate>(wide?MaxBitrate:LegacyMaxBitrate)) throw std::runtime_error("Bitrate requires a newer host protocol");
    Writer w; w.put(uint8_t(wide?Message::HelloWide:Message::Hello)); w.put(uint16_t(wide?2:1)); w.put(udpPort);
    w.put(s.width); w.put(s.height); w.put(s.fps);
    if(wide) w.put(s.bitrate); else w.put(uint32_t(s.bitrate));
    w.put(s.codecMask);
    if(token.empty()) w.data.insert(w.data.end(),32,0);
    else w.data.insert(w.data.end(), token.begin(), token.end());
    return w.data;
}
struct Welcome {
    uint64_t session; Codec codec; Settings settings;
    explicit Welcome(const Bytes& b) {
        Reader r(b);
        auto type=r.get<uint8_t>(); bool wide=type==uint8_t(Message::WelcomeWide);
        if((!wide && type!=uint8_t(Message::Welcome)) || r.get<uint16_t>()!=(wide?2:1)) throw std::runtime_error("Protocol mismatch");
        session=r.get<uint64_t>(); codec=Codec(r.get<uint8_t>());
        settings.bitDepth=codec==Codec::HEVC10?10:8;
        settings.width=r.get<uint16_t>(); settings.height=r.get<uint16_t>(); settings.fps=r.get<uint16_t>(); settings.bitrate=wide?r.get<uint64_t>():r.get<uint32_t>();
        if(!r.end() || session==0 || (codec!=Codec::H264 && codec!=Codec::HEVC && codec!=Codec::HEVC10) || settings.width<320 || settings.width>4096 ||
           settings.height<240 || settings.height>4096 || settings.width%2 || settings.height%2 ||
           settings.fps<1 || settings.fps>240 ||
           settings.bitrate<10000000 || settings.bitrate>(wide?MaxBitrate:LegacyMaxBitrate)) throw std::runtime_error("Invalid welcome");
    }
};
inline Bytes input(uint8_t kind, uint16_t code, uint16_t flags, int32_t x=0, int32_t y=0) {
    Writer w; w.put(uint8_t(Message::Input)); w.put(kind); w.put(code); w.put(flags); w.put(x); w.put(y); return w.data;
}
inline Bytes framed(const Bytes& b) {
    if(b.empty() || b.size()>ControlLimit) throw std::runtime_error("Invalid control size");
    Writer w; w.put(uint32_t(b.size())); w.bytes(b); return w.data;
}
struct ControlFramer {
    Bytes pending;
    std::vector<Bytes> push(const uint8_t* p, size_t n) {
        pending.insert(pending.end(), p, p+n); std::vector<Bytes> result; size_t pos=0;
        while(pending.size()-pos>=4) {
            Reader r(pending.data()+pos, 4); auto length=r.get<uint32_t>();
            if(length==0 || length>ControlLimit) throw std::runtime_error("Invalid control size");
            if(pending.size()-pos<length+4) break;
            result.emplace_back(pending.begin()+pos+4, pending.begin()+pos+4+length); pos+=4+length;
        }
        pending.erase(pending.begin(), pending.begin()+pos);
        if(pending.size()>ControlLimit+4) throw std::runtime_error("Control overflow");
        return result;
    }
};
struct VideoHeader {
    Codec codec; bool key; uint64_t session; uint32_t id; uint64_t pts;
    uint32_t size; uint16_t index, count, length;
    static std::optional<VideoHeader> parse(const uint8_t* p, size_t n) {
        if(n<HeaderSize || n>HeaderSize+FragmentSize) return {};
        Reader r(p,n); VideoHeader h{};
        if(r.get<uint32_t>()!=0x54444231 || r.get<uint8_t>()!=1) return {};
        h.codec=Codec(r.get<uint8_t>()); auto flags=r.get<uint16_t>(); h.key=(flags&1)!=0;
        h.session=r.get<uint64_t>(); h.id=r.get<uint32_t>(); h.pts=r.get<uint64_t>(); h.size=r.get<uint32_t>();
        h.index=r.get<uint16_t>(); h.count=r.get<uint16_t>(); h.length=r.get<uint16_t>(); auto reserved=r.get<uint16_t>();
        if((h.codec!=Codec::H264 && h.codec!=Codec::HEVC && h.codec!=Codec::HEVC10) || flags>1 || reserved || !h.size || h.size>MaxFrameSize ||
           h.count!=(h.size+FragmentSize-1)/FragmentSize || h.index>=h.count ||
           h.length!=std::min(FragmentSize, size_t(h.size)-h.index*FragmentSize) || n!=HeaderSize+h.length) return {};
        return h;
    }
};
struct Frame { Bytes data; uint32_t id; uint64_t pts; bool key; };
// Sequence comparisons permit uint32 wrap-around within half the sequence space.
inline bool newer(uint32_t a, uint32_t b) { return a!=b && uint32_t(a-b)<0x80000000u; }
class Reassembler {
    struct Pending { Frame frame; std::vector<bool> seen; size_t received=0; uint64_t since,deadline; };
    std::map<uint32_t,Pending> pending;
    uint64_t session; Codec codec; uint32_t last=0; bool haveLast=false, waitingKey=true, request=true;
    static constexpr uint64_t DeadlineUS=25000, LargeFrameDeadlineUS=50000;
    size_t frameLimit; uint64_t bitrate;
    void lose() { pending.clear(); waitingKey=true; request=true; }
public:
    Reassembler(uint64_t s, Codec c,size_t limit=LegacyMaxFrameSize,uint64_t rate=300000000): session(s), codec(c), frameLimit(std::min(limit,MaxFrameSize)),bitrate(std::max(rate,uint64_t(10000000))) {}
    bool takeIDRRequest() { bool v=request; request=false; return v; }
    void push(const uint8_t* p, size_t n, uint64_t now) {
        auto h=VideoHeader::parse(p,n);
        if(!h || h->size>frameLimit || h->session!=session || h->codec!=codec || (haveLast && !newer(h->id,last)) || (waitingKey && !h->key)) return;
        auto it=pending.find(h->id);
        if(it==pending.end()) {
            if(pending.size()>=3) { lose(); if(!h->key) return; }
            // Requested encoder bitrate is not the observed network rate.
            // Large desktop bursts and receiver scheduling can exceed 25 ms.
            // Allow 50 ms for modern frames; complete frames never wait here.
            // Missing references remain bounded by this expiry and three slots.
            const auto minimum=frameLimit>LegacyMaxFrameSize?LargeFrameDeadlineUS:DeadlineUS;
            auto deadline=h->key && frameLimit>LegacyMaxFrameSize?
                std::clamp<uint64_t>(uint64_t(h->size)*16000000/bitrate+10000,minimum,300000):minimum;
            it=pending.emplace(h->id, Pending{Frame{Bytes(h->size),h->id,h->pts,h->key},std::vector<bool>(h->count),0,now,deadline}).first;
        }
        auto& v=it->second;
        if(v.frame.data.size()!=h->size || v.frame.pts!=h->pts || v.frame.key!=h->key) { lose(); return; }
        if(!v.seen[h->index]) {
            std::memcpy(v.frame.data.data()+h->index*FragmentSize, p+HeaderSize, h->length);
            v.seen[h->index]=true; ++v.received;
        }
    }
    std::optional<Frame> next(uint64_t now) {
        // A completed IDR can restart the stream across lost or stale frames.
        auto key=pending.end();
        for(auto it=pending.begin(); it!=pending.end(); ++it)
            if(it->second.frame.key && it->second.received==it->second.seen.size() &&
               (key==pending.end() || newer(it->first,key->first))) key=it;
        if(key!=pending.end() && (waitingKey || !haveLast || key->first!=last+1)) {
            auto f=std::move(key->second.frame); pending.erase(key);
            for(auto it=pending.begin(); it!=pending.end();) {
                if(!newer(it->first,f.id)) it=pending.erase(it); else ++it;
            }
            last=f.id; haveLast=true; waitingKey=false; request=false; return f;
        }
        auto it=pending.find(last+1);
        if(!waitingKey && it!=pending.end() && it->second.received==it->second.seen.size()) {
            auto f=std::move(it->second.frame); pending.erase(it); last=f.id; return f;
        }
        for(auto& item: pending) if(now-item.second.since>=item.second.deadline) { lose(); break; }
        return {};
    }
};
} // namespace td
