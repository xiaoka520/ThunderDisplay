#include "raw_video_updates.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <iostream>

int main(int argc,char** argv) {
    if(argc!=3) return 2;
    const auto target=std::stoi(argv[2]); if(target<1 || target>240) return 2;
    const int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return 2;
    sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(uint16_t(std::stoi(argv[1])));
    address.sin_addr.s_addr=inet_addr("127.0.0.1");
    timeval timeout{2,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    if(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))) return 2;
    const auto hello=td::rawHandshake(0x0102030405060708ull);
    if(send(fd,hello.data(),hello.size(),0)!=ssize_t(hello.size())) return 2;
    using Clock=std::chrono::steady_clock;
    const auto began=Clock::now(); uint64_t received=0;
    auto exact=[&](td::Bytes& bytes) {
        size_t offset=0;
        while(offset<bytes.size()) {
            auto n=recv(fd,bytes.data()+offset,std::min<size_t>(65536,bytes.size()-offset),0);
            if(n<=0) { if(offset) throw std::runtime_error("Incomplete update"); return false; }
            offset+=size_t(n); received+=uint64_t(n);
            // Four Gbps synthetic receive pacing; not a physical-link benchmark.
            std::this_thread::sleep_until(began+std::chrono::nanoseconds(received*2));
        }
        return true;
    };
    try {
        const auto size=td::rawPacked10Bytes(4096,2560); td::RawVideoUpdates state(4096,2560);
        td::Bytes header(20),payload,expected(size); unsigned frames=0,stable=0,full=0; uint32_t previous=0;
        Clock::time_point first,last;
        while(exact(header)) {
            const td::RawFrameHeader frame(header,size,true); payload.resize(frame.size);
            if(!exact(payload)) throw std::runtime_error("Missing update payload");
            const auto& pixels=state.apply(payload,frame.id); const auto value=uint8_t(frame.id);
            std::fill(expected.begin(),expected.end(),frame.pts?value:0); expected.front()=expected.back()=value;
            if(pixels!=expected) throw std::runtime_error("Dropped capture broke exact pixel reconstruction");
            ++frames; if(payload.size()==size+16) ++full;
            if(!frame.pts && Clock::now()-began>std::chrono::seconds(1)) {
                if(!stable) first=Clock::now(); last=Clock::now(); ++stable;
            }
            if(previous && !td::newer(frame.id,previous)) throw std::runtime_error("Non-monotonic update");
            previous=frame.id;
        }
        close(fd);
        if(stable<2) return 1;
        const auto fps=double(stable-1)/std::chrono::duration<double>(last-first).count();
        std::cout<<"synthetic_4Gbps stable_fps="<<fps<<" frames="<<frames<<" full="<<full<<" received_bytes="<<received
            <<" every_complete_image_exact=true no_screen_or_input=true\n";
        return fps>=target*0.8?0:1;
    } catch(const std::exception& e) { close(fd); std::cerr<<e.what()<<'\n'; return 1; }
}
