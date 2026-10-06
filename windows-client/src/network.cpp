#include "network.hpp"
#include <iphlpapi.h>

namespace {
struct Socket {
    SOCKET fd=INVALID_SOCKET;
    explicit Socket(int type) { fd=socket(AF_INET,type,0); if(fd==INVALID_SOCKET) throw std::runtime_error("socket failed"); }
    ~Socket() { if(fd!=INVALID_SOCKET) closesocket(fd); }
    Socket(const Socket&)=delete; Socket& operator=(const Socket&)=delete;
    void nonblocking() { u_long mode=1; if(ioctlsocket(fd,FIONBIO,&mode)) throw std::runtime_error("Nonblocking socket failed"); }
};
sockaddr_in endpoint(const std::string& ip,uint16_t port) {
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port);
    if(InetPtonA(AF_INET,ip.c_str(),&a.sin_addr)!=1) throw std::runtime_error("--host must be a numeric IPv4 address"); return a;
}
std::string ip(const in_addr& a) { char b[INET_ADDRSTRLEN]{}; InetNtopA(AF_INET,&a,b,sizeof(b)); return b; }
bool wouldBlock() { return WSAGetLastError()==WSAEWOULDBLOCK; }
void bindSocket(SOCKET fd,sockaddr_in a) { if(bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))) throw std::runtime_error("Socket bind failed"); }
}

void ClientSession::setStatus(std::string value) {
    std::cout<<"[ThunderDisplay] "<<value<<std::endl;
    { std::lock_guard<std::mutex> lock(mutex); status=std::move(value); }
    PostMessageW(window,StatusMessage,0,0);
}
void ClientSession::checkRecoveryDeadline() {
    if(retryBudget.expired(micros())) throw std::runtime_error("Recovery limit reached: 10 minutes");
}
void ClientSession::expireHandover() {
    if(holdingFrame && !handoverHold.active(micros())) {
        holdingFrame=false; handoverHold.clear(); renderer.resetFrame();
        PostMessageW(window,DisconnectedMessage,0,0);
    }
}
void ClientSession::send(td::Bytes data) {
    if(!online || holdingFrame) return;
    std::lock_guard<std::mutex> lock(mutex);
    if(!online || holdingFrame) return;
    // Mouse moves can replace only the immediately preceding move, preserving button/key ordering.
    if(data.size()==14 && data[0]==uint8_t(td::Message::Input) && data[1]==1 && !outgoing.empty() &&
       outgoing.back().size()==14 && outgoing.back()[0]==uint8_t(td::Message::Input) && outgoing.back()[1]==1) {
        outgoing.back()=std::move(data); return;
    }
    if(outgoing.size()>=256) { overflow=true; return; }
    outgoing.push_back(std::move(data));
}
void ClientSession::sendClipboard(const std::string& text) {
    if(!clipboardOnline) return;
    std::lock_guard<std::mutex> lock(mutex);
    if(!clipboardOnline) return;
    if(++clipboardID==0) ++clipboardID;
    clipboardOutgoing=td::clipboardPackets(text,clipboardID);
}
void ClientSession::sendClipboardImage(const td::Bytes& png) {
    if(!imageClipboardEnabled()) return;
    std::lock_guard<std::mutex> lock(mutex);
    if(!imageClipboardEnabled()) return;
    if(++clipboardID==0) ++clipboardID;
    clipboardOutgoing=td::blobPackets(td::Message::ClipboardImage,png,clipboardID,td::ClipboardImageLimit);
}
std::string ClientSession::discover() {
    Socket socket(SOCK_DGRAM); socket.nonblocking(); BOOL yes=TRUE;
    setsockopt(socket.fd,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&yes),sizeof(yes));
    bindSocket(socket.fd,endpoint("0.0.0.0",0));
    ULONG size=16384; std::vector<uint8_t> storage(size);
    auto* adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    auto hr=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,adapters,&size);
    if(hr==ERROR_BUFFER_OVERFLOW) { storage.resize(size); adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); hr=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,adapters,&size); }
    if(hr!=NO_ERROR) throw std::runtime_error("Cannot enumerate local network adapters");
    std::vector<sockaddr_in> destinations;
    for(auto* adapter=adapters;adapter;adapter=adapter->Next) {
        if(adapter->OperStatus!=IfOperStatusUp || adapter->IfType!=IF_TYPE_ETHERNET_CSMACD) continue;
        for(auto* u=adapter->FirstUnicastAddress;u;u=u->Next) {
            if(u->Address.lpSockaddr->sa_family!=AF_INET || u->OnLinkPrefixLength>30 || !u->OnLinkPrefixLength) continue;
            auto a=*reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            uint32_t mask=0xffffffffu<<(32-u->OnLinkPrefixLength);
            a.sin_addr.s_addr=htonl(ntohl(a.sin_addr.s_addr)|~mask); a.sin_port=htons(options.port);
            destinations.push_back(a);
        }
    }
    if(destinations.empty()) throw std::runtime_error("No active Ethernet / Thunderbolt IPv4 interface; configure static IPs and use --host");
    const char query[]="TDDISC1?"; auto start=micros(),last=uint64_t(0);
    while(!stopFlag && micros()-start<2000000) {
        checkRecoveryDeadline();
        if(micros()-last>500000) {
            for(auto& a:destinations) sendto(socket.fd,query,8,0,reinterpret_cast<sockaddr*>(&a),sizeof(a)); last=micros();
        }
        fd_set reads; FD_ZERO(&reads); FD_SET(socket.fd,&reads); timeval timeout{0,50000};
        if(select(0,&reads,nullptr,nullptr,&timeout)>0) {
            char reply[64]{}; sockaddr_in sender{}; int len=sizeof(sender);
            auto n=recvfrom(socket.fd,reply,sizeof(reply)-1,0,reinterpret_cast<sockaddr*>(&sender),&len);
            if(n>0 && std::string(reply,n)=="TDHOST1 "+std::to_string(options.port)) return ip(sender.sin_addr);
        }
    }
    throw std::runtime_error("No host discovered. Check the Thunderbolt Bridge IPv4 addresses or specify --host.");
}
void ClientSession::run() {
    auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(com)) { setStatus("COM initialization failed"); return; }
    while(!stopFlag) {
        expireHandover();
        retryBudget.begin(micros());
        if(retryBudget.exhausted(micros())) {
            holdingFrame=false; handoverHold.clear(); renderer.resetFrame();
            recoveryStopped=true; setStatus("Recovery limit reached: stopped after 10 minutes or 150 attempts"); break;
        }
        if(!retryBudget.startAttempt(micros())) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue;
        }
        attemptNumber=retryBudget.attempts;
        try {
            setStatus(options.host.empty()?"Discovering Thunderbolt host…":"Connecting to "+options.host);
            auto host=options.host.empty()?discover():options.host;
            checkRecoveryDeadline(); connectAndStream(host);
        } catch(const std::exception& e) { if(!stopFlag) setStatus(e.what()); }
        online=false; clipboardOnline=false; richClipboard=false; localCursorActive=false; videoInterrupted=false;
        expireHandover(); renderer.resetFrame(holdingFrame);
        { std::lock_guard<std::mutex> lock(mutex); outgoing.clear(); clipboardOutgoing.clear(); clipboardIncoming.reset();imageIncoming.reset();cursorIncoming.reset(); }
        PostMessageW(window,DisconnectedMessage,0,0);
    }
    CoUninitialize();
}
void ClientSession::connectAndStream(const std::string& host) {
    online=false; clipboardOnline=false; richClipboard=false; localCursorActive=false; renderer.resetFrame(holdingFrame); overflow=false; wantIDR=false;
    Socket tcp(SOCK_STREAM); tcp.nonblocking();
    BOOL yes=TRUE; setsockopt(tcp.fd,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes));
    auto remote=endpoint(host,options.port);
    auto result=connect(tcp.fd,reinterpret_cast<sockaddr*>(&remote),sizeof(remote));
    if(result && !wouldBlock()) throw std::runtime_error("TCP connect failed");
    if(result) {
        auto deadline=micros()+3000000; bool connected=false;
        while(!stopFlag && micros()<deadline) {
            checkRecoveryDeadline();
            fd_set write,error; FD_ZERO(&write); FD_ZERO(&error); FD_SET(tcp.fd,&write); FD_SET(tcp.fd,&error); timeval t{0,50000};
            if(select(0,nullptr,&write,&error,&t)>0) {
                int code=0,len=sizeof(code); if(getsockopt(tcp.fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&code),&len) || code) throw std::runtime_error("Host is unreachable");
                connected=true; break;
            }
        }
        if(!connected) throw std::runtime_error("TCP connection timed out");
    }
    sockaddr_in local{}; int localLength=sizeof(local);
    if(getsockname(tcp.fd,reinterpret_cast<sockaddr*>(&local),&localLength)) throw std::runtime_error("Cannot obtain route address");
    local.sin_port=0;
    Socket udp(SOCK_DGRAM); udp.nonblocking();
    int receiveBuffer=8*1024*1024; setsockopt(udp.fd,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&receiveBuffer),sizeof(receiveBuffer));
    bindSocket(udp.fd,local); localLength=sizeof(local);
    if(getsockname(udp.fd,reinterpret_cast<sockaddr*>(&local),&localLength)) throw std::runtime_error("Cannot obtain video port");
    bool querying=options.capabilityVersion>=6 || options.localCursor || options.autoQuality || options.autoFrameRate || options.settings.bitrate>300000000 || options.clipboard || options.settings.height>2304 || options.colorDepth==10 || (options.colorDepth==0 && options.displayBits>=10 && (requestedCodecMask&2));
    auto capabilityQuery=options.capabilityVersion==1?td::Bytes{uint8_t(td::Message::CapabilityQuery)}:td::Bytes{uint8_t(td::Message::CapabilityQuery),options.capabilityVersion};
    if(options.capabilityVersion>=4) capabilityQuery.push_back(options.localCursor && options.capabilityVersion>=5?1:0);
    options.settings.codecMask=requestedCodecMask;
    td::Bytes output=td::framed(querying?capabilityQuery:td::hello(options.settings,ntohs(local.sin_port),options.token)); size_t outputOffset=0;
    td::ControlFramer framer; std::unique_ptr<Decoder> decoder; std::unique_ptr<td::Reassembler> assembler;
    uint64_t accepted=micros(),lastControl=accepted,lastPing=accepted,lastIDR=0,lastFrame=accepted;
    uint64_t sessionID=0,videoPackets=0,completeFrames=0,readyAt=0,lastStatus=0,lastProbe=0;
    td::Codec codec=td::Codec::H264;
    sockaddr_in videoEndpoint=remote; bool haveVideoPort=false;
    std::string streamDescription;
    std::string sourceDescription;
    bool awaitingKey=true;
    bool clipboardSupported=false,nativeNegotiated=false,largeFrames=false,cursorSupported=false,richFeatures=false,desktopSRGB=false;
    bool confirmedFrame=false;
    bool transitionReceived=false;
    uint64_t decodedCount=0,lastDecoded=accepted;
    td::ClipboardAssembler clipboardAssembler;
    td::BlobAssembler imageAssembler,cursorAssembler;
    auto failureWithFallback=[&](td::Codec used,const std::string& reason) {
        if(desktopSRGB && reason.find("DesktopColorUnavailable:")!=std::string::npos) {
            options.capabilityVersion=5;
            fallbackDescription="Desktop color unavailable; reconnecting with legacy color: "+reason;
            return fallbackDescription;
        }
        if(nativeNegotiated && options.autoQuality && (options.settings.width>options.display.width || options.settings.height>options.display.height || options.settings.height>2304)) {
            options.nativePixels=false;
            fallbackDescription=std::string("Native pixels unavailable; reconnecting at display resolution: ")+reason;
            return fallbackDescription;
        }
        auto mask=td::fallbackCodecMask(used,options.settings.codecMask,options.colorDepth);
        if(mask) {
            requestedCodecMask=mask; options.settings.codecMask=mask;
            if(used==td::Codec::HEVC10) { options.colorDepth=8; fallbackDescription=std::string("10-bit unavailable; reconnecting with 8-bit SDR: ")+reason; }
            else fallbackDescription=std::string("HEVC unavailable; reconnecting with H.264: ")+reason;
            return fallbackDescription;
        }
        return reason;
    };
    auto decodeOperation=[&](auto operation) {
        try { return operation(); }
        catch(const std::exception& e) {
            throw std::runtime_error(failureWithFallback(codec,e.what()));
        }
    };
    auto drainFrames=[&] {
        while(auto frame=assembler->next(micros())) {
            ++completeFrames;
            if(awaitingKey && !frame->key) continue;
            if(frame->key && awaitingKey) {
                // A fresh decoder already waits for its first IDR. Flushing here
                // used to discard the async decoder's initial input-ready events.
                if(decoder->submittedFrames()) decodeOperation([&] { decoder->flush(); });
                awaitingKey=false;
            }
            if(!decodeOperation([&] { return decoder->submit(std::move(*frame)); })) { awaitingKey=true; wantIDR=true; }
            else lastFrame=micros();
        }
    };
    auto stageText=[&] {
        if(videoInterrupted) return "Video interrupted; requesting a fresh keyframe";
        switch(td::videoStage(videoPackets,completeFrames,decoder->submittedFrames(),decoder->decodedFrames(),renderer.hasFrame())) {
        case td::VideoStage::NoPackets: return "No video packets; check the Mac capture and UDP firewall";
        case td::VideoStage::IncompleteFrame: return "Video packets received; waiting for a complete keyframe";
        case td::VideoStage::DecoderInput: return "Keyframe received; decoder has not accepted input";
        case td::VideoStage::DecoderOutput: return "Compressed frames submitted; decoder has not produced output";
        case td::VideoStage::Presentation: return "Decoded frames ready; restore the remote display window";
        case td::VideoStage::Playing: return "Video playing";
        }
        return "";
    };
    while(!stopFlag) {
        expireHandover();
        checkRecoveryDeadline();
        if(overflow) throw std::runtime_error("Input queue overflow; reconnecting to release held keys");
        auto now=micros();
        if(decoder && decoder->decodedFrames()!=decodedCount) {
            decodedCount=decoder->decodedFrames(); lastDecoded=now; videoInterrupted=false;
        }
        if(!confirmedFrame && renderer.hasFrame()) {
            holdingFrame=false; handoverHold.clear();
            confirmedFrame=true; retryBudget.succeeded(); retryBudget.streamDisplayed(); attemptNumber=0;
            PostMessageW(window,FramePresentedMessage,0,0);
        }
        if(!transitionReceived && confirmedFrame && !td::VideoHealth::fresh(now,lastDecoded) && !videoInterrupted.exchange(true)) {
            renderer.resetFrame(); PostMessageW(window,DisconnectedMessage,0,0);
        }
        if(!transitionReceived && confirmedFrame && td::VideoHealth::stalled(now,lastDecoded)) throw std::runtime_error("Video stalled: reconnecting to restore live frames");
        if(now-lastControl>10000000 || (!decoder && now-accepted>15000000)) throw std::runtime_error("Host timed out");
        if(decoder && now-lastProbe>(haveVideoPort?2000000u:500000u)) {
            auto probe=td::videoProbe(sessionID); auto& target=haveVideoPort?videoEndpoint:remote;
            sendto(udp.fd,probe.data(),int(probe.size()),0,reinterpret_cast<sockaddr*>(&target),sizeof(target)); lastProbe=now;
        }
        if(decoder && now-lastStatus>1000000) {
            setStatus(streamDescription+"\r\n"+renderer.colorDescription()+"\r\n"+stageText()+"\r\nClipboard active: "+(clipboardOnline?"yes":"no")+"\r\nVideo packets: "+std::to_string(videoPackets)+
                " | Complete frames: "+std::to_string(completeFrames)+" | Submitted: "+std::to_string(decoder->submittedFrames())+
                " | Decoded: "+std::to_string(decoder->decodedFrames())); lastStatus=now;
        }
        if(decoder && !renderer.hasFrame() && decoder->decodedFrames()==0 && now-readyAt>12000000) {
            if(completeFrames && (nativeNegotiated || td::fallbackCodecMask(codec,options.settings.codecMask,options.colorDepth)))
                throw std::runtime_error(failureWithFallback(codec,"first frame decode timed out"));
            throw std::runtime_error(std::string("First frame timed out: ")+stageText()+" | Video packets: "+std::to_string(videoPackets)+
                " | Complete frames: "+std::to_string(completeFrames)+" | Submitted: "+std::to_string(decoder->submittedFrames()));
        }
        if(online && now-lastPing>2000000) { send({uint8_t(td::Message::Ping)}); lastPing=now; }
        if(online && (wantIDR || now-lastFrame>500000) && now-lastIDR>250000) {
            send({uint8_t(td::Message::RequestIDR)}); wantIDR=false; lastIDR=now;
        }
        if(outputOffset==output.size()) {
            output.clear(); outputOffset=0;
            std::lock_guard<std::mutex> lock(mutex);
            while(!outgoing.empty() && output.size()<16384) {
                auto b=td::framed(outgoing.front()); outgoing.pop_front(); output.insert(output.end(),b.begin(),b.end());
            }
            if(output.empty() && clipboardOnline && !clipboardOutgoing.empty()) {
                output=td::framed(clipboardOutgoing.front()); clipboardOutgoing.pop_front();
            }
        }
        fd_set read,write; FD_ZERO(&read); FD_ZERO(&write); FD_SET(tcp.fd,&read); FD_SET(udp.fd,&read);
        if(outputOffset<output.size()) FD_SET(tcp.fd,&write);
        timeval timeout{0,2000}; auto selected=select(0,&read,&write,nullptr,&timeout);
        if(selected==SOCKET_ERROR) throw std::runtime_error("Socket select failed");
        if(FD_ISSET(tcp.fd,&write)) {
            auto n=::send(tcp.fd,reinterpret_cast<const char*>(output.data()+outputOffset),int(output.size()-outputOffset),0);
            if(n>0) outputOffset+=size_t(n); else if(n==0 || !wouldBlock()) throw std::runtime_error("TCP send failed");
        }
        if(FD_ISSET(tcp.fd,&read)) {
            uint8_t b[8192]; auto n=recv(tcp.fd,reinterpret_cast<char*>(b),sizeof(b),0);
            if(n==0) {
                if(querying && options.capabilityVersion>1) { --options.capabilityVersion; throw std::runtime_error("Retrying display detection with older host capabilities"); }
                throw std::runtime_error("Host disconnected");
            }
            if(n<0 && !wouldBlock()) throw std::runtime_error("TCP receive failed");
            if(n>0) for(auto& message:framer.push(b,size_t(n))) {
                lastControl=micros(); auto type=td::Message(message[0]);
                if(type==td::Message::SessionTransition) {
                    if(options.capabilityVersion<8 || !decoder || !td::validSessionTransition(message,sessionID))
                        throw std::runtime_error("Invalid session transition notice");
                    if(!transitionReceived && confirmedFrame && renderer.hasFrame()) {
                        handoverHold.begin(micros()); holdingFrame=true; renderer.resetFrame(true);
                    }
                    transitionReceived=true; online=false; clipboardOnline=false; localCursorActive=false;
                    { std::lock_guard<std::mutex> lock(mutex); outgoing.clear(); clipboardOutgoing.clear(); }
                    // Retire queued input before acknowledging the session notice.
                    // A partially sent framed message must finish to keep framing intact.
                    if(outputOffset==0) output.clear();
                    auto ack=td::framed(td::sessionTransition(sessionID,true)); output.insert(output.end(),ack.begin(),ack.end());
                    PostMessageW(window,DisconnectedMessage,0,0); continue;
                }
                if(type==td::Message::Failure) {
                    auto earlyReason=std::string(message.begin()+1,message.end());
                    if(earlyReason.rfind("HostWaitingForLogin:",0)==0 || earlyReason.rfind("PreLoginCaptureUnavailable:",0)==0 || earlyReason.rfind("PreLoginStarting:",0)==0 || earlyReason.rfind("InputUnavailable:",0)==0)
                        throw std::runtime_error(earlyReason);
                    if(querying && options.capabilityVersion>1) { --options.capabilityVersion; throw std::runtime_error("Retrying display detection with older host capabilities"); }
                    auto reason=std::string(message.begin()+1,message.end());
                    throw std::runtime_error(failureWithFallback(reason.find("Main10Unavailable:")==0?td::Codec::HEVC10:codec,reason));
                }
                if(type==td::Message::Capabilities && querying) {
                    td::HostCapabilities capabilities(message);
                    auto mask=td::negotiatedCodecMask(requestedCodecMask,options.colorDepth,options.displayBits,capabilities.codecMask);
                    nativeNegotiated=options.nativePixels && (capabilities.flags&8);
                    clipboardSupported=(capabilities.flags&4)!=0;
                    largeFrames=(capabilities.flags&16)!=0;
                    richFeatures=(capabilities.flags&64)!=0; richClipboard=richFeatures;
                    cursorSupported=richFeatures && (capabilities.flags&32)!=0;
                    desktopSRGB=options.capabilityVersion>=6 && (capabilities.flags&128)!=0;
                    renderer.setDesktopSRGB(desktopSRGB);
                    if(options.settings.bitrate>td::LegacyMaxBitrate && (!options.autoQuality || options.customBitrate) && !richFeatures) throw std::runtime_error("Bitrate above 1000 Mbps requires both platforms 0.7.0 or later");
                    if(options.settings.bitrate>300000000 && (!options.autoQuality || options.customBitrate) && !largeFrames)
                        throw std::runtime_error("Bitrate above 300 Mbps requires Mac Host 0.6.2 or later. Update the Mac or lower the custom bitrate.");
                    if(options.autoQuality) options.settings=td::bestQuality(capabilities.current,options.display,mask,nativeNegotiated,options.customBitrate?options.settings.bitrate:0,largeFrames?td::LegacyMaxBitrate:300000000);
                    else { options.settings.codecMask=mask; if(options.autoFrameRate) options.settings.fps=td::negotiatedFrameRate(capabilities.current,options.display); }
                    if(options.settings.height>2304 && !(capabilities.flags&8)) throw std::runtime_error("Update the Mac host to use native HiDPI pixels above 2304 lines");
                    sourceDescription="Mac display: "+capabilities.name+" | "+std::to_string(capabilities.current.width)+"×"+
                        std::to_string(capabilities.current.height)+" | "+std::to_string(capabilities.current.hz)+" Hz\r\n";
                    output=td::framed(td::hello(options.settings,ntohs(local.sin_port),options.token,richFeatures)); outputOffset=0; querying=false;
                    setStatus("Auto quality negotiated: "+std::to_string(options.settings.width)+"×"+std::to_string(options.settings.height)+
                        " | "+std::to_string(options.settings.fps)+" Hz | "+std::to_string(options.settings.bitrate/1000000)+" Mbps");
                    continue;
                }
                if((type==td::Message::Welcome || type==td::Message::WelcomeWide) && !decoder && !querying) {
                    td::Welcome welcome(message);
                    renderer.retainForHandover(options.capabilityVersion>=8);
                    sessionID=welcome.session; codec=welcome.codec;
                    if(!(options.settings.codecMask&uint8_t(welcome.codec))) throw std::runtime_error("Host selected an unrequested codec");
                    try { renderer.configureBitDepth(welcome.settings.bitDepth); decoder=std::make_unique<Decoder>(renderer,welcome,desktopSRGB); }
                    catch(const std::exception& e) {
                        throw std::runtime_error(failureWithFallback(welcome.codec,e.what()));
                    }
                    assembler=std::make_unique<td::Reassembler>(welcome.session,welcome.codec,richFeatures?td::MaxFrameSize:largeFrames?td::GigabitMaxFrameSize:td::LegacyMaxFrameSize,welcome.settings.bitrate);
                    localCursorActive=options.localCursor && cursorSupported;
                    online=true; send({uint8_t(td::Message::Ready)}); wantIDR=true; readyAt=lastFrame=lastDecoded=micros();
                    if(options.clipboard && clipboardSupported) {
                        send({uint8_t(td::Message::ClipboardControl),1});
                    }
                    streamDescription=sourceDescription+host+" | "+std::to_string(welcome.settings.width)+"×"+std::to_string(welcome.settings.height)+
                        " | target "+std::to_string(welcome.settings.fps)+" Hz | "+std::to_string(welcome.settings.bitrate/1000000)+" Mbps | "+
                        (welcome.codec==td::Codec::HEVC10?"HEVC Main10 / SDR 10-bit":welcome.codec==td::Codec::HEVC?"HEVC / SDR 8-bit":"H.264 / SDR 8-bit")+std::string(" / 4:2:0 / D3D11")+
                        (options.clipboard?(clipboardSupported?(richFeatures?"\r\nClipboard: text and images requested (64 KiB / 32 MiB)":"\r\nText clipboard: requested (64 KiB)"):"\r\nText clipboard: unavailable on this Mac host"):"\r\nText clipboard: disabled")+
                        (localCursorActive?"\r\nCursor: live macOS system cursor (video cursor hidden)":options.localCursor?(richFeatures?"\r\nCursor: video (native cursor access unavailable on this Mac)":"\r\nCursor: video (update Mac to 0.7.0 for live system cursors)"):"\r\nCursor: video")+
                        (welcome.settings.bitrate!=options.settings.bitrate?"\r\nHardware encoder bitrate limit: requested "+std::to_string(options.settings.bitrate/1000000)+" Mbps, accepted "+std::to_string(welcome.settings.bitrate/1000000)+" Mbps":"")+
                        (fallbackDescription.empty()?"":"\r\nLast fallback: "+fallbackDescription);
                    setStatus(streamDescription);
                } else if(type==td::Message::CursorImage && cursorSupported && decoder) {
                    if(auto image=cursorAssembler.push(message,td::Message::CursorImage,td::CursorImageLimit)) {
                        { std::lock_guard<std::mutex> lock(mutex);cursorIncoming=std::move(*image); }
                        PostMessageW(window,CursorMessage,0,0);
                    }
                } else if(type==td::Message::ClipboardImage && richFeatures && clipboardOnline) {
                    if(auto image=imageAssembler.push(message,td::Message::ClipboardImage,td::ClipboardImageLimit)) {
                        { std::lock_guard<std::mutex> lock(mutex);imageIncoming=std::move(*image);clipboardIncoming.reset(); }
                        PostMessageW(window,ClipboardMessage,0,0);
                    }
                } else if(type==td::Message::ClipboardControl && options.clipboard && clipboardSupported && message.size()==2 && message[1]<=1) {
                    clipboardOnline=message[1]!=0;
                    { std::lock_guard<std::mutex> lock(mutex); clipboardOutgoing.clear(); clipboardIncoming.reset();imageIncoming.reset(); }
                    clipboardAssembler.reset();imageAssembler.reset(); PostMessageW(window,ClipboardMessage,1,0);
                } else if(type==td::Message::ClipboardText && clipboardOnline) {
                    if(auto text=clipboardAssembler.push(message)) {
                        { std::lock_guard<std::mutex> lock(mutex); clipboardIncoming=std::move(*text);imageIncoming.reset(); }
                        PostMessageW(window,ClipboardMessage,0,0);
                    }
                } else if(type!=td::Message::Pong || message.size()!=1) throw std::runtime_error("Unexpected control message");
            }
        }
        if(transitionReceived) continue; // No old-session decode/present after the notice.
        if(FD_ISSET(udp.fd,&read)) {
            for(unsigned i=0;i<512;++i) {
                uint8_t b[1500]; sockaddr_in sender{}; int length=sizeof(sender);
                auto n=recvfrom(udp.fd,reinterpret_cast<char*>(b),sizeof(b),0,reinterpret_cast<sockaddr*>(&sender),&length);
                if(n<0) { if(wouldBlock()) break; throw std::runtime_error("UDP receive failed"); }
                if(assembler && sender.sin_addr.s_addr==remote.sin_addr.s_addr) {
                    if(sender.sin_port==remote.sin_port) {
                        if(auto port=td::videoProbePort(std::string(reinterpret_cast<char*>(b),size_t(n)),sessionID)) {
                            videoEndpoint=remote; videoEndpoint.sin_port=htons(*port); haveVideoPort=true; lastProbe=0;
                            continue;
                        }
                    }
                    auto header=td::VideoHeader::parse(b,size_t(n));
                    if(header && header->session==sessionID && header->codec==codec) {
                        ++videoPackets; assembler->push(b,size_t(n),micros());
                    }
                }
                // Drain complete access units frequently: a burst must not evict usable frames.
                if(assembler) drainFrames();
            }
        }
        if(assembler) {
            drainFrames();
            if(assembler->takeIDRRequest()) { wantIDR=true; awaitingKey=true; }
        }
        if(decoder) decodeOperation([&] { decoder->pump(); });
    }
    // Closing TCP releases all pressed keys/buttons on the host, including on exit.
}
