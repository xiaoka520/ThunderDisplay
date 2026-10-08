#pragma once
#include "protocol.hpp"
#include <cmath>

namespace td {
constexpr uint64_t MinimumBitrate=10000000;
constexpr int BitrateSliderSteps=2000;
inline bool knownLinkRate(uint64_t rate) { return rate>=MinimumBitrate && rate<=MaxBitrate; }
inline uint64_t linkBitrateLimit(uint64_t receiveRate) {
    return knownLinkRate(receiveRate)?receiveRate/1000000*1000000:DefaultBitrateLimit;
}
// Zero is automatic. Logarithmic spacing keeps ordinary desktop bitrates easy
// to select even when a direct link supports tens of gigabits per second.
inline uint64_t sliderBitrate(int position,uint64_t limit) {
    limit=linkBitrateLimit(limit);
    if(position<=0) return 0;
    if(position>=BitrateSliderSteps) return limit;
    const auto fraction=double(position-1)/(BitrateSliderSteps-1);
    const auto rate=double(MinimumBitrate)*std::exp(std::log(double(limit)/MinimumBitrate)*fraction);
    return std::clamp(uint64_t(std::llround(rate/1000000))*1000000,MinimumBitrate,limit);
}
inline int bitrateSliderPosition(uint64_t rate,uint64_t limit) {
    limit=linkBitrateLimit(limit);
    if(!rate) return 0;
    if(rate>=limit) return BitrateSliderSteps;
    if(rate<=MinimumBitrate) return 1;
    const auto fraction=std::log(double(rate)/MinimumBitrate)/std::log(double(limit)/MinimumBitrate);
    return std::clamp(1+int(std::llround(fraction*(BitrateSliderSteps-1))),1,BitrateSliderSteps);
}
inline uint64_t bitrateSnapStep(uint64_t rate) {
    const auto mbps=rate/1000000;
    return (mbps<100?10:mbps<1000?50:mbps<5000?100:mbps<10000?500:mbps<100000?1000:mbps<500000?5000:10000)*1000000ULL;
}
// Only attract nearby round values; dragging away releases the magnet. The
// wider release distance avoids flicker when the pointer sits at a snap edge.
inline uint64_t magneticBitrate(uint64_t raw,uint64_t limit,uint64_t previous=0) {
    limit=linkBitrateLimit(limit);
    if(!raw) return 0;
    raw=std::clamp(raw,MinimumBitrate,limit);
    if(raw==limit) return limit;
    const auto distance=[](uint64_t a,uint64_t b){return a>b?a-b:b-a;};
    if(previous>=MinimumBitrate && previous<limit && previous%bitrateSnapStep(previous)==0 &&
       distance(raw,previous)<=bitrateSnapStep(previous)*32/100) return previous;
    const auto step=bitrateSnapStep(raw);
    const auto candidate=std::clamp((raw+step/2)/step*step,MinimumBitrate,limit);
    return distance(raw,candidate)<=step*22/100?candidate:raw;
}
// Native arrow-key increments must escape a magnetic stop on the first press.
inline uint64_t nextSliderBitrate(uint64_t rate,bool increase,uint64_t limit) {
    limit=linkBitrateLimit(limit);
    if(increase) {
        if(!rate) return MinimumBitrate;
        const auto step=bitrateSnapStep(rate);
        return std::min(limit,(rate/step+1)*step);
    }
    if(rate<=MinimumBitrate) return 0;
    const auto step=bitrateSnapStep(rate-1);
    return std::clamp((rate-1)/step*step,MinimumBitrate,limit);
}
// Manual entry remains exact to 1 Mbps. Empty or zero selects automatic bitrate.
inline std::optional<uint64_t> parseBitrateMbps(const std::string& text,uint64_t limit) {
    if(text.empty()) return uint64_t(0);
    uint64_t mbps=0;
    const auto maximum=linkBitrateLimit(limit)/1000000;
    for(char c:text) {
        if(c<'0' || c>'9' || mbps>maximum/10 || (mbps==maximum/10 && uint64_t(c-'0')>maximum%10)) return {};
        mbps=mbps*10+uint64_t(c-'0');
    }
    if(mbps && mbps<MinimumBitrate/1000000) return {};
    return mbps*1000000;
}
inline uint64_t automaticBitrate(uint16_t width,uint16_t height,uint16_t fps,uint8_t codecMask,uint64_t ceiling=LegacyMaxBitrate) {
    if(ceiling<MinimumBitrate || ceiling>LegacyMaxBitrate) throw std::runtime_error("Invalid automatic bitrate ceiling");
    const auto bitsPerPixel=(codecMask&6)?1.35:2.16;
    const auto mbps=uint64_t(std::ceil(double(width)*height*fps*bitsPerPixel/10000000))*10;
    return std::clamp(mbps*1000000,std::min<uint64_t>(160000000,ceiling),ceiling);
}
}
