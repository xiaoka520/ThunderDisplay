#include "protocol.hpp"
#include <cstdlib>
#include <iostream>

#define REQUIRE(expr) do { if(!(expr)) { std::cerr<<__LINE__<<": "<<#expr<<"\n"; std::exit(1); } } while(0)
using namespace td;
Bytes packet(const Bytes& frame,uint32_t id,bool key,size_t index=0,uint64_t session=0x0102030405060708) {
    auto count=(frame.size()+FragmentSize-1)/FragmentSize,offset=index*FragmentSize,length=std::min(FragmentSize,frame.size()-offset);
    Writer w; w.put(uint32_t(0x54444231)); w.put(uint8_t(1)); w.put(uint8_t(Codec::HEVC)); w.put(uint16_t(key?1:0));
    w.put(session); w.put(id); w.put(uint64_t(id)*1000); w.put(uint32_t(frame.size()));
    w.put(uint16_t(index)); w.put(uint16_t(count)); w.put(uint16_t(length)); w.put(uint16_t(0));
    w.data.insert(w.data.end(),frame.begin()+offset,frame.begin()+offset+length); return w.data;
}
void push(Reassembler& a,const Bytes& p,uint64_t t) { a.push(p.data(),p.size(),t); }
void control() {
    auto h=hello(Settings{},50000,"0123456789abcdef0123456789abcdef");
    Bytes expected={1,0,1,195,80,10,0,6,64,0,120,7,39,14,0,3};
    REQUIRE(h.size()==48); REQUIRE(std::equal(expected.begin(),expected.end(),h.begin()));
    auto all=framed(h),ping=framed({uint8_t(Message::Ping)}); all.insert(all.end(),ping.begin(),ping.end());
    for(size_t i=0;i<=all.size();++i) {
        ControlFramer f; auto first=f.push(all.data(),i),second=f.push(all.data()+i,all.size()-i);
        first.insert(first.end(),second.begin(),second.end()); REQUIRE(first.size()==2); REQUIRE(first[0]==h); REQUIRE(first[1]==Bytes{5});
    }
    for(auto b:std::vector<Bytes>{{0,0,0,0},{0,0,16,1},{255,255,255,255}}) {
        ControlFramer f; bool rejected=false; try { f.push(b.data(),b.size()); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    Welcome w(Bytes{2,0,1,1,2,3,4,5,6,7,8,2,10,0,6,64,0,120,7,39,14,0});
    REQUIRE(w.session==0x0102030405060708); REQUIRE(w.codec==Codec::HEVC); REQUIRE(w.settings.width==2560);
    auto ten=Bytes{2,0,1,1,2,3,4,5,6,7,8,4,10,0,6,64,0,120,7,39,14,0};
    Welcome main10(ten); REQUIRE(main10.codec==Codec::HEVC10 && main10.settings.bitDepth==10);
    auto video=packet(Bytes(12,0xab),1,true); video[5]=4;
    Reassembler main10Frames(0x0102030405060708,Codec::HEVC10);
    main10Frames.push(video.data(),video.size(),0); REQUIRE(main10Frames.next(1));
    auto in=input(4,0,14,-120,48); Reader r(in); REQUIRE(r.get<uint8_t>()==3); REQUIRE(r.get<uint8_t>()==4);
    REQUIRE(r.get<uint16_t>()==0); REQUIRE(r.get<uint16_t>()==14); REQUIRE(r.get<int32_t>()==-120); REQUIRE(r.get<int32_t>()==48);
}
void validation() {
    auto p=packet(Bytes(1161,0xab),9,true,1); REQUIRE(p.size()==41);
    auto h=VideoHeader::parse(p.data(),p.size()); REQUIRE(h); REQUIRE(h->index==1 && h->count==2 && h->length==1);
    REQUIRE(!VideoHeader::parse(p.data(),p.size()-1));
    for(size_t position:std::vector<size_t>{0,4,5,7,31,33,35,37,39}) {
        auto bad=p; bad[position]=0xff; REQUIRE(!VideoHeader::parse(bad.data(),bad.size()));
    }
    Reassembler a(0x0102030405060708,Codec::HEVC);
    push(a,packet(Bytes(10,1),1,true,0,123),0); REQUIRE(!a.next(1));
    auto wrong=packet(Bytes(10,1),1,true); wrong[5]=1; push(a,wrong,0); REQUIRE(!a.next(1));
}
void reassembly() {
    Reassembler a(0x0102030405060708,Codec::HEVC); REQUIRE(a.takeIDRRequest());
    Bytes data(2500); for(size_t i=0;i<data.size();++i) data[i]=uint8_t(i);
    push(a,packet(data,1,true,2),100); push(a,packet(data,1,true,0),200); push(a,packet(data,1,true,0),300);
    REQUIRE(!a.next(400)); push(a,packet(data,1,true,1),500);
    auto frame=a.next(600); REQUIRE(frame && frame->data==data && frame->id==1 && frame->key);
    push(a,packet(data,1,true,0),700); REQUIRE(!a.next(800));
    push(a,packet(Bytes(4,3),3,false),900); REQUIRE(!a.next(1000));
    push(a,packet(Bytes(4,2),2,false),1100); frame=a.next(1200); REQUIRE(frame && frame->id==2);
    frame=a.next(1300); REQUIRE(frame && frame->id==3);
    push(a,packet(data,4,false,0),1400); push(a,packet(Bytes(4,5),5,false),1500);
    REQUIRE(!a.next(26401)); REQUIRE(a.takeIDRRequest());
    push(a,packet(Bytes(4,6),6,false),27000); REQUIRE(!a.next(27001));
    push(a,packet(Bytes(4,7),7,true),28000); frame=a.next(28001); REQUIRE(frame && frame->id==7);
    REQUIRE(!a.takeIDRRequest());
}
void boundedAndWrap() {
    Reassembler a(0x0102030405060708,Codec::HEVC); a.takeIDRRequest();
    push(a,packet(Bytes(4,1),0xfffffffeu,true),1); REQUIRE(a.next(2));
    push(a,packet(Bytes(4,1),0xffffffffu,false),3); REQUIRE(a.next(4));
    push(a,packet(Bytes(4,1),0,false),5); REQUIRE(a.next(6));
    push(a,packet(Bytes(4,1),1,false),7); REQUIRE(a.next(8));
    push(a,packet(Bytes(4,1),0xffffffffu,true),9); REQUIRE(!a.next(10));
    Bytes large(1200);
    for(uint32_t id=2;id<=5;++id) push(a,packet(large,id,false,0),100+id);
    REQUIRE(a.takeIDRRequest()); REQUIRE(!a.next(200));
    push(a,packet(large,6,true,0),300); auto inconsistent=packet(large,6,true,1); inconsistent[27]^=1;
    push(a,inconsistent,301); REQUIRE(a.takeIDRRequest()); REQUIRE(!a.next(302));
}
void optionalPairing() {
    auto direct=hello(Settings{},50000,"");
    auto paired=hello(Settings{},50000,"0123456789abcdef0123456789abcdef");
    REQUIRE(direct.size()==paired.size()); REQUIRE(direct.size()==48);
    REQUIRE(std::equal(direct.begin(),direct.begin()+16,paired.begin()));
    REQUIRE(std::all_of(direct.begin()+16,direct.end(),[](uint8_t value){return value==0;}));
    try { hello(Settings{},50000,"invalid"); REQUIRE(false); } catch(const std::runtime_error&) {}
}
void firstFrameDiagnosticsAndProbe() {
    REQUIRE(videoProbePort("TDVIDEO1 123 50000",123)==50000);
    for(auto& bad:std::vector<std::string>{"TDVIDEO1 124 50000","TDVIDEO1 123 0","TDVIDEO1 123 65536",
        "TDVIDEO1 123 -1","TDVIDEO1 123 50000junk","TDVIDEO1 123 ","TDVIDEO1 123 99999999999999999999"})
        REQUIRE(!videoProbePort(bad,123));
    REQUIRE(videoStage(0,0,0,0,false)==VideoStage::NoPackets);
    REQUIRE(videoStage(200,0,0,0,false)==VideoStage::IncompleteFrame);
    REQUIRE(videoStage(200,1,0,0,false)==VideoStage::DecoderInput);
    REQUIRE(videoStage(200,1,1,0,false)==VideoStage::DecoderOutput);
    REQUIRE(videoStage(200,1,1,1,false)==VideoStage::Presentation);
    REQUIRE(videoStage(200,1,1,1,true)==VideoStage::Playing);
}
void highBitrateFrames() {
    auto welcome=[](uint64_t rate) {
        Writer w; w.put(uint8_t(Message::WelcomeWide)); w.put(uint16_t(2)); w.put(uint64_t(1)); w.put(uint8_t(Codec::HEVC));
        w.put(uint16_t(4096)); w.put(uint16_t(2560)); w.put(uint16_t(60)); w.put(rate); return w.data;
    };
    REQUIRE(Welcome(welcome(MaxBitrate)).settings.bitrate==MaxBitrate);
    bool rejected=false; try { Welcome w(welcome(MaxBitrate+1)); } catch(...) { rejected=true; } REQUIRE(rejected);
    Bytes large(6*1024*1024,0x5a); // 50 ms at 1 Gbps, exceeding the old 25 ms expiry
    Reassembler legacy(0x0102030405060708,Codec::HEVC);
    push(legacy,packet(large,1,true),0); REQUIRE(!legacy.next(1));
    Reassembler modern(0x0102030405060708,Codec::HEVC,MaxFrameSize,LegacyMaxBitrate);
    size_t count=(large.size()+FragmentSize-1)/FragmentSize;
    for(size_t i=0;i<count;++i) {
        auto now=uint64_t(i)*50000/count;
        push(modern,packet(large,1,true,i),now);
        if(i+1<count) REQUIRE(!modern.next(now));
    }
    auto complete=modern.next(50000); REQUIRE(complete && complete->data==large);
    push(modern,packet(large,2,true),60000);
    REQUIRE(!modern.next(60000+300000)); REQUIRE(modern.takeIDRRequest());
    push(modern,packet(Bytes(10,1),3,true),400000); REQUIRE(modern.next(400001));
}
void receiverSchedulingJitter() {
    // A short receiver pause must not destroy an otherwise complete reference
    // chain. Completion is delivered immediately, without waiting for expiry.
    Reassembler modern(0x0102030405060708,Codec::HEVC,MaxFrameSize,2000000000);
    modern.takeIDRRequest();
    Bytes frame(1200,0x42);
    push(modern,packet(frame,1,true,0),0);
    REQUIRE(!modern.next(26000)); REQUIRE(!modern.takeIDRRequest());
    push(modern,packet(frame,1,true,1),28000);
    auto complete=modern.next(28001); REQUIRE(complete && complete->id==1);
    push(modern,packet(frame,2,false,0),30000);
    REQUIRE(!modern.next(56000)); REQUIRE(!modern.takeIDRRequest());
    push(modern,packet(frame,2,false,1),58000);
    complete=modern.next(58001); REQUIRE(complete && complete->id==2);
    // Real loss still expires; dependent future frames cannot bypass it.
    push(modern,packet(frame,3,false,0),60000);
    push(modern,packet(Bytes(4,4),4,false),70000);
    REQUIRE(!modern.next(109999)); REQUIRE(!modern.takeIDRRequest());
    REQUIRE(!modern.next(110000)); REQUIRE(modern.takeIDRRequest());
    push(modern,packet(Bytes(4,5),5,false),110001); REQUIRE(!modern.next(110002));
    push(modern,packet(Bytes(4,6),6,true),110003);
    complete=modern.next(110004); REQUIRE(complete && complete->id==6);
}
void sessionHandover() {
    uint64_t id=0x0102030405060708;
    auto notice=sessionTransition(id);
    REQUIRE(notice==Bytes({17,1,2,3,4,5,6,7,8,1}));
    REQUIRE(validSessionTransition(notice,id)); REQUIRE(!validSessionTransition(notice,id+1));
    REQUIRE(!validSessionTransition(notice,0)); REQUIRE(!validSessionTransition(notice,id,true));
    REQUIRE(validSessionTransition(sessionTransition(id,true),id,true));
    notice.back()=2; REQUIRE(!validSessionTransition(notice,id));
    notice=sessionTransition(id); notice.push_back(0); REQUIRE(!validSessionTransition(notice,id));
    notice.pop_back(); notice.pop_back(); REQUIRE(!validSessionTransition(notice,id));
}
int main() { control(); optionalPairing(); validation(); reassembly(); boundedAndWrap(); firstFrameDiagnosticsAndProbe(); highBitrateFrames(); receiverSchedulingJitter(); sessionHandover(); std::cout<<"Protocol tests passed\n"; }
