#include "video_worker.hpp"

VideoWorker::VideoWorker(Renderer& renderer,const td::Welcome& welcome,bool srgb):
    work([this,&renderer,welcome,srgb]{run(renderer,welcome,srgb);}) {
    std::unique_lock<std::mutex> lock(stateMutex);
    initialized.wait(lock,[&]{return ready;});
    auto error=failure; lock.unlock();
    if(!error.empty()) { stop(); throw std::runtime_error(error); }
}
void VideoWorker::stop() {
    stopped=true; inbox.stop(); if(work.joinable()) work.join();
}
void VideoWorker::checkFailure() {
    std::lock_guard<std::mutex> lock(stateMutex);
    if(!failure.empty()) throw std::runtime_error(failure);
}
void VideoWorker::run(Renderer& renderer,td::Welcome welcome,bool srgb) {
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try {
        check(com,"Initialize decoder apartment");
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
        Decoder decoder(renderer,welcome,srgb);
        { std::lock_guard<std::mutex> lock(stateMutex); ready=true; } initialized.notify_one();
        const auto maximumAge=std::max<uint64_t>(50000,2000000/std::max<uint16_t>(1,welcome.settings.fps));
        uint64_t statsAt=micros(),queueTotal=0,queueMax=0,frames=0,operations=0,decodeTotal=0,decodeMax=0,renderTotal=0,renderMax=0;
        while(!stopped) {
            auto item=inbox.take();
            if(stopped) break;
            auto began=micros();
            if(item && (!inbox.current(*item) || began-item->queuedAt>maximumAge)) {
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
