#pragma once
#include "protocol.hpp"
#if defined(__x86_64__) || defined(_M_X64)
#include <tmmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#define TD_RAW_SSSE3
#else
#define TD_RAW_SSSE3 __attribute__((target("ssse3")))
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif
namespace td {
inline bool raw10VectorAvailable() {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
    int registers[4]; __cpuid(registers,1); return (registers[2]&(1<<9))!=0;
#else
    return __builtin_cpu_supports("ssse3");
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}
#if defined(__x86_64__) || defined(_M_X64)
TD_RAW_SSSE3 inline unsigned unpackRaw10VectorRow(const uint8_t* input,uint8_t* output,unsigned width,unsigned rowBytes) {
    const auto order=_mm_setr_epi8(0,1,1,2,2,3,3,4,5,6,6,7,7,8,8,9);
    const auto m0=_mm_setr_epi16(-1,0,0,0,-1,0,0,0),m1=_mm_setr_epi16(0,-1,0,0,0,-1,0,0);
    const auto m2=_mm_setr_epi16(0,0,-1,0,0,0,-1,0),m3=_mm_setr_epi16(0,0,0,-1,0,0,0,-1);
    unsigned x=0,offset=0;
    // A 16-byte load consumes 10 bytes. Keep its lookahead inside this row.
    for(;x+8<=width && offset+16<=rowBytes;x+=8,offset+=10,output+=16) {
        const auto words=_mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(input+offset)),order);
        const auto values=_mm_or_si128(_mm_or_si128(_mm_and_si128(words,m0),_mm_and_si128(_mm_srli_epi16(words,2),m1)),
            _mm_or_si128(_mm_and_si128(_mm_srli_epi16(words,4),m2),_mm_and_si128(_mm_srli_epi16(words,6),m3)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(output),_mm_slli_epi16(_mm_and_si128(values,_mm_set1_epi16(1023)),6));
    }
    return x;
}
#elif defined(__aarch64__) || defined(_M_ARM64)
inline unsigned unpackRaw10VectorRow(const uint8_t* input,uint8_t* output,unsigned width,unsigned rowBytes) {
    const uint8_t orderBytes[16]={0,1,1,2,2,3,3,4,5,6,6,7,7,8,8,9};
    const int16_t shiftWords[8]={0,-2,-4,-6,0,-2,-4,-6};
    const auto order=vld1q_u8(orderBytes); const auto shifts=vld1q_s16(shiftWords); unsigned x=0,offset=0;
    for(;x+8<=width && offset+16<=rowBytes;x+=8,offset+=10,output+=16) {
        const auto words=vreinterpretq_u16_u8(vqtbl1q_u8(vld1q_u8(input+offset),order));
        vst1q_u8(output,vreinterpretq_u8_u16(vshlq_n_u16(vandq_u16(vshlq_u16(words,shifts),vdupq_n_u16(1023)),6)));
    }
    return x;
}
#else
inline unsigned unpackRaw10VectorRow(const uint8_t*,uint8_t*,unsigned,unsigned) { return 0; }
#endif
inline uint32_t rawP010Bytes(uint16_t width,uint16_t height) {
    if(width<320 || height<240 || width>4096 || height>4096 || width%2 || height%2)
        throw std::runtime_error("Invalid raw P010 dimensions");
    return uint32_t(width)*height*3;
}
inline uint64_t rawP010Rate(uint16_t width,uint16_t height,uint16_t fps) {
    if(!fps || fps>240) throw std::runtime_error("Invalid raw frame rate");
    return uint64_t(rawP010Bytes(width,height))*8*fps;
}
inline uint32_t rawPacked10Bytes(uint16_t width,uint16_t height) {
    rawP010Bytes(width,height); return ((uint32_t(width)*10+7)/8)*(height+height/2);
}
inline uint32_t rawVideoBytes(uint16_t width,uint16_t height,Codec codec) {
    if(!isRaw(codec)) throw std::runtime_error("Invalid raw video format");
    return codec!=Codec::RawP010?rawPacked10Bytes(width,height):rawP010Bytes(width,height);
}
inline uint64_t rawVideoRate(uint16_t width,uint16_t height,uint16_t fps,Codec codec) {
    if(!fps || fps>240) throw std::runtime_error("Invalid raw frame rate");
    return uint64_t(rawVideoBytes(width,height,codec))*8*fps;
}
// Four ten-bit samples occupy five bytes, least-significant bits first.
// Each row is independent; two trailing samples use three bytes (high nibble 0).
inline void unpackRaw10(const Bytes& packed,uint16_t width,uint16_t height,Bytes& p010,bool useVector=true) {
    if(packed.size()!=rawPacked10Bytes(width,height) || p010.size()!=rawP010Bytes(width,height))
        throw std::runtime_error("Invalid packed ten-bit frame size");
    const auto rowBytes=(uint32_t(width)*10+7)/8;
    for(unsigned row=0;row<unsigned(height)+height/2;++row) {
        const auto* input=packed.data()+size_t(row)*rowBytes;
        auto* output=p010.data()+size_t(row)*width*2;
        static const bool vector=raw10VectorAvailable();
        unsigned x=useVector && vector?unpackRaw10VectorRow(input,output,width,rowBytes):0;
        input+=size_t(x)*5/4; output+=size_t(x)*2;
        for(;x+4<=width;x+=4,input+=5,output+=8) {
            uint32_t lo; std::memcpy(&lo,input,4); // Supported arm64 / x64 targets are little-endian.
            const uint64_t bits=uint64_t(lo)|(uint64_t(input[4])<<32);
            const uint16_t words[4]={uint16_t((bits&1023)<<6),uint16_t(((bits>>10)&1023)<<6),
                uint16_t(((bits>>20)&1023)<<6),uint16_t(((bits>>30)&1023)<<6)};
            std::memcpy(output,words,8);
        }
        if(x<width) {
            if(input[2]&0xf0) throw std::runtime_error("Invalid packed row padding");
            const uint32_t bits=uint32_t(input[0])|(uint32_t(input[1])<<8)|(uint32_t(input[2])<<16);
            const uint16_t words[2]={uint16_t((bits&1023)<<6),uint16_t(((bits>>10)&1023)<<6)};
            std::memcpy(output,words,4);
        }
    }
}
inline Bytes rawHandshake(uint64_t session) {
    Writer w; w.put(uint32_t(0x54445241)); w.put(session); return w.data;
}
inline uint16_t rawEndpoint(const Bytes& data,uint64_t session) {
    Reader r(data);
    if(!session || r.get<uint8_t>()!=uint8_t(Message::RawVideoEndpoint) || r.get<uint64_t>()!=session)
        throw std::runtime_error("Invalid raw endpoint session");
    auto port=r.get<uint16_t>();
    if(!port || !r.end()) throw std::runtime_error("Invalid raw endpoint");
    return port;
}
struct RawFrameHeader {
    uint32_t id,size; uint64_t pts;
    RawFrameHeader(const Bytes& data,uint32_t expectedSize,bool updates=false) {
        Reader r(data);
        if(r.get<uint32_t>()!=0x54445246) throw std::runtime_error("Invalid raw frame magic");
        id=r.get<uint32_t>(); size=r.get<uint32_t>(); pts=r.get<uint64_t>();
        if(!id || (updates?(size<16 || size>expectedSize+16):size!=expectedSize) || !r.end()) throw std::runtime_error("Invalid raw frame size");
    }
};
}
