#include "video_worker.hpp"
#include "raw_video_updates.hpp"
#include "media_thread.hpp"

VideoWorker::VideoWorker(Renderer& renderer,const td::Welcome& welcome,bool srgb):
    rawInbox(td::isRaw(welcome.codec)?std::make_unique<td::RawVideoInbox>(td::rawVideoBytes(welcome.settings.width,welcome.settings.height,welcome.codec)):nullptr),
    work([this,&renderer,welcome,srgb]{run(renderer,welcome,srgb);}) {
    std::unique_lock<std::mutex> lock(stateMutex);
    initialized.wait(lock,[&]{return ready;});
    auto error=failure; lock.unlock();
    if(!error.empty()) { stop(); throw std::runtime_error(error); }
}
void VideoWorker::stop() {
    stopped=true; inbox.stop();
    if(rawInbox) rawInbox->stop();
    { std::lock_guard<std::mutex> lock(stateMutex); if(rawSocket!=INVALID_SOCKET) shutdown(rawSocket,SD_BOTH); }
    initialized.notify_all(); if(work.joinable()) work.join();
}
void VideoWorker::checkFailure() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(!failure.empty()) throw std::runtime_error(failure);
}
void VideoWorker::run(Renderer& renderer,td::Welcome welcome,bool srgb) {
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try {
        check(com,"Initialize decoder apartment");
        MediaThreadPriority priority("pixel worker");
        if(td::isRaw(welcome.codec)) {
            if(!srgb) throw std::runtime_error("Raw P010 requires desktop sRGB color");
            { std::lock_guard<std::mutex> lock(stateMutex); ready=true; } initialized.notify_one();
            runRaw(renderer,welcome);
            CoUninitialize(); return;
        }
        Decoder decoder(renderer,welcome,srgb);
        { std::lock_guard<std::mutex> lock(stateMutex); ready=true; } initialized.notify_one();
        const auto maximumAge=std::max<uint64_t>(50000,2000000/std::max<uint16_t>(1,welcome.settings.fps));
        uint64_t statsAt=micros(),queueTotal=0,queueMax=0,frames=0,operations=0,decodeTotal=0,decodeMax=0,renderTotal=0,renderMax=0;
        while(!stopped) {
            auto item=inbox.take();
            if(stopped) break;
            auto began=micros();
            if(item && (!inbox.current(*item) || began-item->queuedAt>maximumAge)) {
                ++busyDropped;
                if(inbox.current(*item)) inbox.requestKey();
                continue;
            }
            const auto previousOutputs=decoder.decodedFrames(), previousRender=decoder.presentationMicros();
            bool intact=true;
            if(item) {
                auto wait=began-item->queuedAt; queueTotal+=wait; queueMax=std::max(queueMax,wait); ++frames;
                if(item->flush && decoder.submittedFrames()) decoder.flush();
                intact=decoder.submit(std::move(item->frame),item->queuedAt);
            } else intact=decoder.pump();
            if(!intact) inbox.requestKey();
            const auto now=micros(),render=decoder.presentationMicros()-previousRender,elapsed=now-began;
            if(item || decoder.decodedFrames()!=previousOutputs) {
                ++operations; renderTotal+=render; renderMax=std::max(renderMax,render);
                auto decode=elapsed>=render?elapsed-render:0; decodeTotal+=decode; decodeMax=std::max(decodeMax,decode);
            }
            submitted=decoder.submittedFrames();
            if(decoder.decodedFrames()!=previousOutputs) { decodedAt=now; decoded=decoder.decodedFrames(); }
            if(now-statsAt>=5000000) {
                { std::lock_guard<std::mutex> lock(stateMutex); latency={uint32_t(queueTotal/std::max<uint64_t>(1,frames)),uint32_t(queueMax),uint32_t(decodeTotal/std::max<uint64_t>(1,operations)),uint32_t(decodeMax)}; }
                diagnosticLog("video.latency","queue_us_avg/max="+std::to_string(queueTotal/std::max<uint64_t>(1,frames))+"/"+std::to_string(queueMax)+
                    " decode_us_avg/max="+std::to_string(decodeTotal/std::max<uint64_t>(1,operations))+"/"+std::to_string(decodeMax)+
                    " display_enqueue_us_avg/max="+std::to_string(renderTotal/std::max<uint64_t>(1,operations))+"/"+std::to_string(renderMax)+
                    " operations="+std::to_string(operations)+" discarded="+std::to_string(inbox.discardedFrames()));
                statsAt=now; queueTotal=queueMax=frames=operations=decodeTotal=decodeMax=renderTotal=renderMax=0;
            }
        }
    } catch(const std::exception& e) {
        { std::lock_guard<std::mutex> lock(stateMutex); failure=e.what(); ready=true; }
        initialized.notify_one(); inbox.stop();
        diagnosticLog("video.worker.error",e.what());
    }
    if(SUCCEEDED(com)) CoUninitialize();
}

void VideoWorker::startRaw(sockaddr_in address,uint64_t session) {
    auto fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(fd==INVALID_SOCKET) throw std::runtime_error("Raw video socket unavailable");
    struct SocketCleanup { SOCKET fd; ~SocketCleanup() { if(fd!=INVALID_SOCKET) closesocket(fd); } } cleanup{fd};
    // Reserve capacity before connect/SYN. Setting it after the handshake
    // reports the larger buffer but left the live advertised window at 65,280
    // bytes. The synthetic throughput test reserves its buffer before SYN.
    int receiveCapacity=0,capacityLength=sizeof(receiveCapacity);
    const int requestedCapacity=16*1024*1024;
    int bufferError=0;
    if(!getsockopt(fd,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<char*>(&receiveCapacity),&capacityLength) && receiveCapacity<requestedCapacity)
        if(setsockopt(fd,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<const char*>(&requestedCapacity),sizeof(requestedCapacity))) bufferError=WSAGetLastError();
    capacityLength=sizeof(receiveCapacity);
    if(!getsockopt(fd,SOL_SOCKET,SO_RCVBUF,reinterpret_cast<char*>(&receiveCapacity),&capacityLength))
        diagnosticLog("video.raw.socket","receive_buffer_bytes="+std::to_string(receiveCapacity)+" requested_bytes="+std::to_string(requestedCapacity)+" option_error="+std::to_string(bufferError)+" phase=before_connect");
    u_long nonblocking=1;
    if(ioctlsocket(fd,FIONBIO,&nonblocking)) throw std::runtime_error("Raw socket mode failed");
    if(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==SOCKET_ERROR && WSAGetLastError()!=WSAEWOULDBLOCK)
        throw std::runtime_error("Raw video connection failed");
    fd_set write; FD_ZERO(&write); FD_SET(fd,&write); timeval timeout{3,0};
    if(select(0,nullptr,&write,nullptr,&timeout)!=1) throw std::runtime_error("Raw video connection timed out");
    int error=0,length=sizeof(error); if(getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length) || error)
        throw std::runtime_error("Raw video connection rejected");
    nonblocking=0; ioctlsocket(fd,FIONBIO,&nonblocking);
    DWORD sendTimeout=2000; int yes=1;
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&sendTimeout),sizeof(sendTimeout));
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char*>(&yes),sizeof(yes));
    const auto hello=td::rawHandshake(session); size_t offset=0;
    while(offset<hello.size()) {
        auto n=send(fd,reinterpret_cast<const char*>(hello.data()+offset),int(hello.size()-offset),0);
        if(n<=0) throw std::runtime_error("Raw video authentication failed"); offset+=size_t(n);
    }
    // Winsock documents a socket as indeterminate after SO_RCVTIMEO expires.
    // Poll a nonblocking socket instead, so a capture's startup delay never
    // invalidates the video connection before its first frame arrives.
    nonblocking=1;
    if(ioctlsocket(fd,FIONBIO,&nonblocking)) throw std::runtime_error("Raw receive socket mode failed");
    { std::lock_guard<std::mutex> lock(stateMutex);
      if(stopped || rawSocket!=INVALID_SOCKET) throw std::runtime_error("Unexpected raw video endpoint");
      rawSocket=fd; cleanup.fd=INVALID_SOCKET; }
    initialized.notify_all();
}
void VideoWorker::runRaw(Renderer& renderer,const td::Welcome& welcome) {
    SOCKET fd;
    { std::unique_lock<std::mutex> lock(stateMutex); initialized.wait(lock,[&]{return stopped || rawSocket!=INVALID_SOCKET;});
      if(stopped) return; fd=rawSocket; }
    struct RetireSocket {
        std::function<void()> cleanup; ~RetireSocket() { cleanup(); }
    } retire{[&]{ std::lock_guard<std::mutex> lock(stateMutex); closesocket(rawSocket); rawSocket=INVALID_SOCKET; }};
    bool receivedFirstFrame=false;
    struct ReceiveTiming { uint64_t readinessWait=0,callMaximum=0,loopGapMaximum=0; } timing;
    auto exact=[&](td::Bytes& bytes) {
        size_t offset=0; auto progress=micros(),reportedAt=progress;
        auto lastOperation=progress;
        while(offset<bytes.size() && !stopped) {
            const auto began=micros(); timing.loopGapMaximum=std::max(timing.loopGapMaximum,began-lastOperation);
            auto n=recv(fd,reinterpret_cast<char*>(bytes.data()+offset),int(std::min<size_t>(1048576,bytes.size()-offset)),0);
            const auto receiveError=n<0?WSAGetLastError():0;
            lastOperation=micros(); timing.callMaximum=std::max(timing.callMaximum,lastOperation-began);
            if(n>0) { offset+=size_t(n); progress=micros(); rawReceivedAt=progress; wireBytes+=uint64_t(n); }
            else if(n<0 && receiveError==WSAEWOULDBLOCK) {
                if(micros()-progress>5000000) throw std::runtime_error("Raw video receive stalled");
                fd_set readable; FD_ZERO(&readable); FD_SET(fd,&readable);
                timeval timeout{0,250000};
                const auto waiting=micros();
                if(select(0,&readable,nullptr,nullptr,&timeout)==SOCKET_ERROR && !stopped)
                    throw std::runtime_error("Raw video receive readiness failed");
                lastOperation=micros(); timing.readinessWait+=lastOperation-waiting;
            } else if(n<0 && receiveError==WSAEINTR) {
                continue;
            } else if(!stopped) throw std::runtime_error("Raw video connection closed");
            if(!receivedFirstFrame && micros()-reportedAt>=1000000) {
                diagnosticLog("video.raw.startup","received_bytes="+std::to_string(offset)+" expected_bytes="+std::to_string(bytes.size()));
                reportedAt=micros();
            }
        }
        return !stopped && offset==bytes.size();
    };
    const bool updates=welcome.codec==td::Codec::RawDelta10,packed=welcome.codec!=td::Codec::RawP010;
    const auto expected=td::rawVideoBytes(welcome.settings.width,welcome.settings.height,welcome.codec);
    auto& raw=*rawInbox;
    std::thread receiver([&] {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
        diagnosticLog("video.scheduling","raw receiver above-normal; nonblocking socket polling");
        try {
            td::Bytes header(20),payload; uint32_t previous=0;
            uint64_t stats=micros(),receiveTotal=0,receiveMax=0,copyTotal=0,copyMax=0,count=0,copiedBytes=0;
            uint64_t readinessTotal=0,readinessMax=0,callMax=0,loopGapMax=0,lastSlowReport=0;
            uint64_t previousPTS=0,previousArrival=0,sourceGapMax=0,arrivalGapMax=0;
            std::unique_ptr<td::RawVideoUpdates> reconstruction;
            if(updates) { reconstruction=std::make_unique<td::RawVideoUpdates>(welcome.settings.width,welcome.settings.height); payload.reserve(expected+16); }
            while(!stopped) {
                auto slot=raw.acquire(); if(!slot) break;
                if(!exact(header)) { raw.release(*slot); break; }
                const td::RawFrameHeader frame(header,expected,updates);
                if(previous && !td::newer(frame.id,previous)) throw std::runtime_error("Out-of-order raw video frame");
                previous=frame.id; const auto arrivedAt=micros();
                const auto sourceGap=previousArrival && frame.pts>previousPTS ? frame.pts-previousPTS : 0;
                const auto arrivalGap=previousArrival ? arrivedAt-previousArrival : 0;
                sourceGapMax=std::max(sourceGapMax,sourceGap); arrivalGapMax=std::max(arrivalGapMax,arrivalGap);
                previousPTS=frame.pts; previousArrival=arrivedAt; timing={};
                uint64_t copy=0,receivedAt=0;
                if(updates) {
                    payload.resize(frame.size);
                    if(!exact(payload)) { raw.release(*slot); break; }
                    receivedAt=micros();
                    reconstruction->apply(payload,frame.id);
                    copiedBytes+=reconstruction->snapshot(raw.pixels(*slot),raw.revisions(*slot));
                    copy=micros()-receivedAt;
                } else { if(!exact(raw.pixels(*slot))) { raw.release(*slot); break; } receivedAt=micros(); }
                const auto receive=receivedAt-arrivedAt; receiveTotal+=receive; receiveMax=std::max(receiveMax,receive);
                readinessTotal+=timing.readinessWait; readinessMax=std::max(readinessMax,timing.readinessWait);
                callMax=std::max(callMax,timing.callMaximum); loopGapMax=std::max(loopGapMax,timing.loopGapMaximum);
                copyTotal+=copy; copyMax=std::max(copyMax,copy); ++count;
                if(receivedFirstFrame && receive>25000 && receivedAt-lastSlowReport>=5000000) {
                    diagnosticLog("video.raw.stall","frame_id="+std::to_string(frame.id)+" payload_bytes="+std::to_string(frame.size)+
                        " receive_us="+std::to_string(receive)+" readiness_wait_us="+std::to_string(timing.readinessWait)+
                        " recv_call_us_max="+std::to_string(timing.callMaximum)+" receiver_gap_us_max="+std::to_string(timing.loopGapMaximum)+
                        " source_interval_us="+std::to_string(sourceGap)+" arrival_interval_us="+std::to_string(arrivalGap));
                    lastSlowReport=receivedAt;
                }
                if(!receivedFirstFrame) {
                    diagnosticLog("video.raw.startup","first_complete_frame_bytes="+std::to_string(header.size()+frame.size)+" payload_receive_us="+std::to_string(receive));
                    receivedFirstFrame=true;
                }
                ++submitted;
                if(!raw.publish({*slot,arrivedAt,micros()})) break;
                if(micros()-stats>=5000000) {
                    diagnosticLog("video.raw.receive","frames="+std::to_string(count)+" payload_receive_us_avg/max="+
                        std::to_string(receiveTotal/std::max<uint64_t>(1,count))+"/"+std::to_string(receiveMax)+
                        " reconstruct_copy_us_avg/max="+std::to_string(copyTotal/std::max<uint64_t>(1,count))+"/"+std::to_string(copyMax)+
                        " snapshot_copy_bytes_avg="+std::to_string(copiedBytes/std::max<uint64_t>(1,count))+
                        " readiness_wait_us_avg/max="+std::to_string(readinessTotal/std::max<uint64_t>(1,count))+"/"+std::to_string(readinessMax)+
                        " recv_call_us_max="+std::to_string(callMax)+" receiver_gap_us_max="+std::to_string(loopGapMax)+
                        " source_interval_us_max="+std::to_string(sourceGapMax)+" arrival_interval_us_max="+std::to_string(arrivalGapMax));
                    stats=micros(); receiveTotal=receiveMax=copyTotal=copyMax=count=copiedBytes=0;
                    readinessTotal=readinessMax=callMax=loopGapMax=sourceGapMax=arrivalGapMax=0;
                }
            }
        } catch(const std::exception& e) {
            { std::lock_guard<std::mutex> lock(stateMutex); failure=e.what(); }
            diagnosticLog("video.raw.receive.error",e.what());
        }
        raw.stop();
    });
    struct JoinReceiver {
        td::RawVideoInbox& raw; SOCKET fd; std::thread& thread;
        ~JoinReceiver() { raw.stop(); shutdown(fd,SD_BOTH); if(thread.joinable()) thread.join(); }
    } join{raw,fd,receiver};
    td::Bytes p010(packed?td::rawP010Bytes(welcome.settings.width,welcome.settings.height):0);
    std::vector<uint64_t> convertedRevisions;
    uint64_t stats=micros(),uploadTotal=0,uploadMax=0,queueTotal=0,queueMax=0,count=0,dropped=0,replaced=0;
    uint64_t unpackTotal=0,unpackMax=0,gpuTotal=0,gpuMax=0,processedBytes=0,regionCount=0;
    while(!stopped) {
        auto frame=raw.take(); if(!frame) break;
        struct ReleaseFrame { td::RawVideoInbox& raw; size_t slot; ~ReleaseFrame() { raw.release(slot); } } release{raw,frame->slot};
        if(stopped) break;
        const auto began=micros(),queued=began-frame->receivedAt;
        if(queued>50000) { ++dropped; ++busyDropped; continue; }
        queueTotal+=queued; queueMax=std::max(queueMax,queued);
        const auto& pixels=raw.pixels(frame->slot);
        const auto regions=updates?td::rawChangedRegions(welcome.settings.width,welcome.settings.height,raw.revisions(frame->slot),convertedRevisions):
            std::vector<td::RawVideoRegion>{{0,0,welcome.settings.width,welcome.settings.height}};
        if(packed) {
            if(updates) for(const auto& region:regions) td::unpackRaw10Region(pixels,welcome.settings.width,welcome.settings.height,p010,region);
            else td::unpackRaw10(pixels,welcome.settings.width,welcome.settings.height,p010);
        }
        const auto unpackedAt=micros(),unpack=unpackedAt-began;
        unpackTotal+=unpack; unpackMax=std::max(unpackMax,unpack);
        if(!stopped && renderer.presentRaw(packed?p010:pixels,welcome.settings.width,welcome.settings.height,welcome.settings.fps,frame->arrivedAt,&regions)) {
            if(updates) convertedRevisions=raw.revisions(frame->slot);
            decodedAt=micros(); ++decoded;
        } else { ++dropped; ++busyDropped; }
        const auto gpu=micros()-unpackedAt; gpuTotal+=gpu; gpuMax=std::max(gpuMax,gpu);
        processedBytes+=td::rawRegionP010Bytes(regions); regionCount+=regions.size();
        const auto upload=micros()-began; uploadTotal+=upload; uploadMax=std::max(uploadMax,upload); ++count;
        if(micros()-stats>=5000000) {
            { std::lock_guard<std::mutex> lock(stateMutex); latency={uint32_t(queueTotal/std::max<uint64_t>(1,count)),uint32_t(queueMax),uint32_t(uploadTotal/std::max<uint64_t>(1,count)),uint32_t(uploadMax)}; }
            const auto pendingReplaced=raw.replacedFrames();
            diagnosticLog("video.raw","frames="+std::to_string(count)+" unpack_upload_us_avg/max="+
                std::to_string(uploadTotal/std::max<uint64_t>(1,count))+"/"+std::to_string(uploadMax)+" busy_discarded="+std::to_string(dropped)+
                " queue_us_avg/max="+std::to_string(queueTotal/std::max<uint64_t>(1,count))+"/"+std::to_string(queueMax)+
                " pending_replaced="+std::to_string(pendingReplaced-replaced)+" receiver=independent"+
                " wire="+(updates?"exact-updates":packed?"packed10":"P010")+" decoder=bypassed"+
                " unpack_us_avg/max="+std::to_string(unpackTotal/std::max<uint64_t>(1,count))+"/"+std::to_string(unpackMax)+
                " gpu_upload_us_avg/max="+std::to_string(gpuTotal/std::max<uint64_t>(1,count))+"/"+std::to_string(gpuMax)+
                " processed_P010_bytes_avg="+std::to_string(processedBytes/std::max<uint64_t>(1,count))+
                " upload_regions_avg="+std::to_string(regionCount/std::max<uint64_t>(1,count))+" upload=changed-planes");
            replaced=pendingReplaced; stats=micros(); uploadTotal=uploadMax=queueTotal=queueMax=count=dropped=0;
            unpackTotal=unpackMax=gpuTotal=gpuMax=processedBytes=regionCount=0;
        }
    }
}
