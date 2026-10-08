#include "bitrate.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#define REQUIRE(expr) do { if(!(expr)) { std::cerr<<__LINE__<<": "<<#expr<<"\n"; std::exit(1); } } while(0)
int main() {
    using namespace td;
    for(auto rate:{uint64_t(0),uint64_t(9999999),std::numeric_limits<uint64_t>::max(),MaxBitrate+1})
        REQUIRE(linkBitrateLimit(rate)==20000000000ULL);
    REQUIRE(linkBitrateLimit(40000000000ULL)==40000000000ULL);
    REQUIRE(linkBitrateLimit(80000000000ULL)==80000000000ULL);
    REQUIRE(linkBitrateLimit(100500000)==100000000);
    REQUIRE(bitrateSnapStep(80000000)==10000000);
    REQUIRE(bitrateSnapStep(300000000)==50000000);
    REQUIRE(bitrateSnapStep(2000000000)==100000000);
    REQUIRE(bitrateSnapStep(7000000000)==500000000);
    REQUIRE(bitrateSnapStep(20000000000)==1000000000);
    REQUIRE(bitrateSnapStep(200000000000)==5000000000);
    REQUIRE(magneticBitrate(2020000000,20000000000)==2000000000);
    REQUIRE(magneticBitrate(2040000000,20000000000)==2040000000);
    REQUIRE(magneticBitrate(2030000000,20000000000,2000000000)==2000000000);
    REQUIRE(magneticBitrate(2033000000,20000000000,2000000000)==2033000000);
    REQUIRE(magneticBitrate(10020000000,20000000000)==10000000000);
    REQUIRE(nextSliderBitrate(0,true,20000000000)==10000000);
    REQUIRE(nextSliderBitrate(10000000,false,20000000000)==0);
    REQUIRE(nextSliderBitrate(2000000000,true,20000000000)==2100000000);
    REQUIRE(nextSliderBitrate(2100000000,false,20000000000)==2000000000);
    REQUIRE(nextSliderBitrate(5000000000,false,20000000000)==4900000000);
    REQUIRE(nextSliderBitrate(10000000,true,17000000)==17000000);
    REQUIRE(parseBitrateMbps("",20000000000)==0);
    REQUIRE(parseBitrateMbps("0",20000000000)==0);
    REQUIRE(parseBitrateMbps("2021",20000000000)==2021000000);
    REQUIRE(parseBitrateMbps("20000",20000000000)==20000000000);
    REQUIRE(parseBitrateMbps("40000",40000000000)==40000000000);
    for(const auto text:{"9","20001","2020 Mbps","12.5","-10","9999999999999999999999"})
        REQUIRE(!parseBitrateMbps(text,20000000000));
    for(auto limit:{uint64_t(10000000),uint64_t(100000000),uint64_t(1000000000),uint64_t(20000000000),uint64_t(40000000000),uint64_t(120000000000)}) {
        REQUIRE(sliderBitrate(0,limit)==0);
        REQUIRE(sliderBitrate(1,limit)==10000000);
        REQUIRE(sliderBitrate(BitrateSliderSteps,limit)==limit);
        REQUIRE(bitrateSliderPosition(0,limit)==0);
        REQUIRE(bitrateSliderPosition(limit,limit)==BitrateSliderSteps);
        uint64_t previous=0;
        uint64_t previousMagnet=0;
        for(int position=0;position<=BitrateSliderSteps;++position) {
            const auto rate=sliderBitrate(position,limit);
            REQUIRE(rate>=previous && rate<=limit);
            if(position) REQUIRE(rate>=10000000 && rate%1000000==0);
            previous=rate;
            const auto snapped=magneticBitrate(rate,limit,previousMagnet);
            REQUIRE(snapped>=previousMagnet && snapped<=limit);
            previousMagnet=snapped;
        }
        for(auto saved:{uint64_t(120000000),uint64_t(850000000),uint64_t(2000000000),uint64_t(40000000000)}) {
            const auto position=bitrateSliderPosition(saved,limit); const auto restored=sliderBitrate(position,limit);
            const auto capped=std::clamp(saved,MinimumBitrate,limit);
            REQUIRE(std::abs(double(restored)-capped)<=std::max(1000000.0,double(capped)*0.007));
        }
    }
    REQUIRE(automaticBitrate(4096,2560,60,7)==850000000);
    REQUIRE(automaticBitrate(4096,2560,60,7,100000000)==100000000);
    REQUIRE(automaticBitrate(4096,2560,60,7,10000000)==10000000);
    auto hello40=hello({2560,1600,60,40000000000ULL,2},50000,"",true);
    Reader read(hello40); read.pos=11; REQUIRE(read.get<uint64_t>()==40000000000ULL);
    std::cout<<"Link limits, slider endpoints, magnetic drag/release, keyboard steps, exact manual rates and 40 Gbps wire precision passed\n";
}
