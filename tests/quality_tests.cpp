#include "quality.hpp"
#include <cstdlib>
#include <iostream>
#define REQUIRE(expr) do { if(!(expr)) { std::cerr<<__LINE__<<": "<<#expr<<"\n"; std::exit(1); } } while(0)
int main() {
    using namespace td;
    REQUIRE(negotiatedFrameRate({4096,2560,75},{2560,1600,144})==75);
    REQUIRE(negotiatedFrameRate({4096,2560,120},{2560,1600,90})==90);
    REQUIRE(negotiatedFrameRate({4096,2560,360},{2560,1600,480})==240);
    auto settings=bestQuality({1984,1240,60},{2560,1600,240});
    REQUIRE(settings.width==1984 && settings.height==1240 && settings.fps==60 && settings.bitrate==160000000);
    settings=bestQuality({5120,3200,120},{2560,1600,240});
    REQUIRE(settings.width==2560 && settings.height==1600 && settings.fps==120 && settings.bitrate==450000000);
    settings=bestQuality({3840,2160,240},{2560,1600,144});
    REQUIRE(settings.width==2560 && settings.height==1440 && settings.fps==144);
    settings=bestQuality({7680,4320,360},{7680,4320,480});
    REQUIRE(settings.width==4096 && settings.height==2304 && settings.fps==240 && settings.bitrate==1000000000);
    auto native=bestQuality({4096,2560,60},{2560,1600,240},7,true);
    REQUIRE(native.width==4096 && native.height==2560 && native.fps==60 && native.bitrate==570000000);
    auto oldHost=bestQuality({4096,2560,60},{2560,1600,240},7,true,0,300000000);
    REQUIRE(oldHost.width==native.width && oldHost.height==native.height && oldHost.fps==native.fps && oldHost.bitrate==300000000);
    auto explicitLow=bestQuality({4096,2560,60},{2560,1600,240},7,true,120000000);
    REQUIRE(explicitLow.bitrate==120000000 && explicitLow.width==native.width);
    auto custom=bestQuality({4096,2560,75},{2560,1600,144},7,true,MaxBitrate);
    REQUIRE(custom.width==4096 && custom.height==2560 && custom.fps==75 && custom.bitrate==MaxBitrate);
    for(uint64_t rate:{uint64_t(9999999),MaxBitrate+1}) {
        bool rejected=false; try { bestQuality({4096,2560,75},{2560,1600,144},7,true,rate); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    auto panel=bestQuality({4096,2560,60},{2560,1600,240},7,false);
    REQUIRE(panel.width==2560 && panel.height==1600);
    native=bestQuality({5120,3200,120},{2560,1600,240},7,true);
    REQUIRE(native.width==4096 && native.height==2560 && native.bitrate==1000000000);
    auto small=bestQuality({1984,1240,60},{2560,1600,240},3,true);
    REQUIRE(small.width==1984 && small.height==1240);
    auto hevc=bestQuality({2560,1600,120},{2560,1600,240},2);
    auto h264=bestQuality({2560,1600,120},{2560,1600,240},1);
    REQUIRE(h264.bitrate>hevc.bitrate && h264.codecMask==1 && hevc.codecMask==2);
    for(uint64_t ceiling:{uint64_t(159999999),uint64_t(1000000001)}) {
        bool rejected=false; try { bestQuality({2560,1600,60},{2560,1600,240},3,false,0,ceiling); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    for(auto source:std::vector<DisplayLimits>{{0,0,0},{2560,1600,0},{40000,2400,240},{12000,240,240}}) {
        bool rejected=false; try { bestQuality(source,{2560,1600,240}); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    Writer w; w.put(uint8_t(Message::Capabilities)); w.put(uint16_t(1));
    w.put(uint32_t(2560)); w.put(uint32_t(1600)); w.put(uint16_t(144));
    w.put(uint32_t(3840)); w.put(uint32_t(2160)); w.put(uint16_t(240));
    w.put(uint8_t(3)); w.put(uint8_t(8)); w.put(uint8_t(1)); w.put(uint16_t(3)); w.data.insert(w.data.end(),{'M','a','c'});
    HostCapabilities capabilities(w.data); REQUIRE(capabilities.current.hz==144 && capabilities.streamBits==8 && capabilities.name=="Mac");
    auto features=w.data; features[25]=15; REQUIRE(HostCapabilities(features).flags==15);
    features[25]=63; REQUIRE(HostCapabilities(features).flags==63);
    features[25]=127; REQUIRE(HostCapabilities(features).flags==127);
    features[25]=255; REQUIRE(HostCapabilities(features).flags==255);
    features[25]=128; bool badFlag=false; try { HostCapabilities f(features); } catch(...) { badFlag=true; } REQUIRE(badFlag);
    for(size_t position:{size_t(0),size_t(2),size_t(24),size_t(26),size_t(27)}) {
        auto invalid=w.data; invalid[position]=0xff;
        bool rejected=false; try { HostCapabilities parsed(invalid); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    REQUIRE(negotiatedCodecMask(3,0,10,7)==7);
    REQUIRE(negotiatedCodecMask(3,0,8,7)==3);
    REQUIRE(negotiatedCodecMask(3,8,10,7)==3);
    REQUIRE(negotiatedCodecMask(2,10,8,7)==4);
    REQUIRE(negotiatedCodecMask(3,0,10,3)==3);
    for(auto pair:std::vector<std::pair<uint8_t,uint8_t>>{{1,7},{3,3}}) {
        bool rejected=false; try { negotiatedCodecMask(pair.first,10,10,pair.second); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    REQUIRE(fallbackCodecMask(Codec::HEVC10,7,0)==3);
    REQUIRE(fallbackCodecMask(Codec::HEVC10,4,10)==0);
    REQUIRE(fallbackCodecMask(Codec::HEVC,3,8)==1);
    auto main10=w.data; main10[23]=7; main10[24]=10;
    REQUIRE(HostCapabilities(main10).streamBits==10);
    main10[24]=8; bool rejected=false; try { HostCapabilities c(main10); } catch(...) { rejected=true; } REQUIRE(rejected);
    auto ten=bestQuality({2560,1600,60},{2560,1600,240},4); REQUIRE(ten.codecMask==4 && ten.width==2560);
    ControlFramer framer; auto packet=framed(w.data);
    for(auto byte:packet) { auto messages=framer.push(&byte,1); if(!messages.empty()) { REQUIRE(messages.size()==1); REQUIRE(messages[0]==w.data); } }
    std::cout<<"Automatic quality and capability tests passed\n";
}
