#pragma once
#include "protocol.hpp"
#include <cmath>

namespace td {
struct DisplayLimits { uint32_t width=2560,height=1600; uint16_t hz=60; };
inline uint16_t negotiatedFrameRate(DisplayLimits source,DisplayLimits target) {
    if(!source.hz || !target.hz) throw std::runtime_error("Display refresh rate unavailable");
    return std::min<uint16_t>({source.hz,target.hz,240});
}
// Preserve the source aspect ratio and never invent detail by enlarging it.
inline Settings bestQuality(DisplayLimits source,DisplayLimits target,uint8_t codecMask=3,bool nativePixels=false,uint64_t bitrateOverride=0,uint64_t automaticCeiling=LegacyMaxBitrate) {
    if(source.width<320 || source.height<240 || source.width>32768 || source.height>32768 ||
       target.width<320 || target.height<240 || target.width>32768 || target.height>32768 ||
       source.hz==0 || target.hz==0 || !(codecMask&7) || (codecMask&~7))
        throw std::runtime_error("Invalid display capabilities");
    double scale=nativePixels?std::min({1.0,4096.0/source.width,4096.0/source.height}):
        std::min({1.0,double(target.width)/source.width,double(target.height)/source.height,4096.0/source.width,2304.0/source.height});
    Settings result;
    result.width=uint16_t(std::floor(source.width*scale/2)*2);
    result.height=uint16_t(std::floor(source.height*scale/2)*2);
    if(result.width<320 || result.height<240) throw std::runtime_error("Display aspect ratio is outside the supported range");
    result.fps=negotiatedFrameRate(source,target); result.codecMask=codecMask;
    // Thunderbolt direct-bridge budget: prioritize quality over bandwidth conservation.
    // Higher bitrate significantly improves text and UI clarity despite 4:2:0 chroma.
    // Increased from 0.90/1.44 to 1.35/2.16 for enhanced detail preservation.
    double bitsPerPixel=(codecMask&6)?1.35:2.16;
    auto mbps=unsigned(std::ceil(double(result.width)*result.height*result.fps*bitsPerPixel/10000000))*10;
    if(bitrateOverride && (bitrateOverride<10000000 || bitrateOverride>MaxBitrate)) throw std::runtime_error("Invalid custom bitrate");
    if(automaticCeiling<160000000 || automaticCeiling>LegacyMaxBitrate) throw std::runtime_error("Invalid automatic bitrate ceiling");
    result.bitrate=bitrateOverride?bitrateOverride:std::clamp(uint64_t(mbps)*1000000,uint64_t(160000000),automaticCeiling);
    return result;
}
// Requested depth: 0 auto, 8 compatible SDR, 10 explicitly required Main10.
inline uint8_t negotiatedCodecMask(uint8_t requested,uint8_t depth,unsigned displayBits,uint8_t hostMask) {
    if(!(requested&3) || (requested&~3) || (depth!=0 && depth!=8 && depth!=10) || (hostMask!=3 && hostMask!=7))
        throw std::runtime_error("Invalid precision negotiation");
    if(depth==10) {
        if(!(requested&2) || !(hostMask&4)) throw std::runtime_error("10-bit SDR requires a Main10-capable Mac and HEVC. Choose Auto or 8-bit SDR.");
        return 4;
    }
    return requested|((depth==0 && displayBits>=10 && (requested&2) && (hostMask&4))?4:0);
}
inline uint8_t fallbackCodecMask(Codec current,uint8_t mask,uint8_t depth) {
    if(current==Codec::HEVC10 && depth==0 && (mask&3)) return mask&3;
    if(current==Codec::HEVC && (mask&1)) return 1;
    return 0;
}
struct HostCapabilities {
    DisplayLimits current, maximum;
    uint8_t codecMask=0,streamBits=0,flags=0;
    std::string name;
    explicit HostCapabilities(const Bytes& data) {
        Reader r(data);
        if(r.get<uint8_t>()!=10 || r.get<uint16_t>()!=1) throw std::runtime_error("Invalid display capability reply");
        current.width=r.get<uint32_t>(); current.height=r.get<uint32_t>(); current.hz=r.get<uint16_t>();
        maximum.width=r.get<uint32_t>(); maximum.height=r.get<uint32_t>(); maximum.hz=r.get<uint16_t>();
        codecMask=r.get<uint8_t>(); streamBits=r.get<uint8_t>(); flags=r.get<uint8_t>(); auto length=r.get<uint16_t>();
        if(length>256 || length!=r.size-r.pos || current.width<320 || current.height<240 ||
            current.width>32768 || current.height>32768 || !current.hz || current.hz>1000 ||
            maximum.width<320 || maximum.height<240 || maximum.width>32768 || maximum.height>32768 ||
            !maximum.hz || maximum.hz>1000 || (codecMask!=3 && codecMask!=7) || streamBits!=((codecMask&4)?10:8) || ((flags&128) && !(flags&64)))
            throw std::runtime_error("Unsupported display capabilities");
        name.assign(reinterpret_cast<const char*>(r.data+r.pos),length);
    }
};
}
