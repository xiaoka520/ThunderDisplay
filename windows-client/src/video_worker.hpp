#pragma once
#include "decoder.hpp"
#include "video_inbox.hpp"
#include "raw_video.hpp"
#include "raw_video_inbox.hpp"

// A connection-scoped MTA owns the decoder. Renderer has a separate presenter;
// neither receiving nor decoding waits for the display refresh/Present queue.
class VideoWorker {
public:
    struct Latency { uint32_t queueAverage=0,queueMaximum=0,decodeAverage=0,decodeMaximum=0; };
private:
    td::VideoInbox inbox;
    std::unique_ptr<td::RawVideoInbox> rawInbox;
    std::atomic<bool> stopped{false};
    std::atomic<uint64_t> submitted{0},decoded{0},decodedAt{0};
    std::atomic<uint64_t> wireBytes{0},busyDropped{0};
    std::atomic<uint64_t> rawReceivedAt{0};
    std::mutex stateMutex;
    std::condition_variable initialized;
    bool ready=false;
    std::string failure;
    Latency latency;
    SOCKET rawSocket=INVALID_SOCKET;
    std::thread work;
    void run(Renderer& renderer,td::Welcome welcome,bool desktopSRGB);
    void runRaw(Renderer& renderer,const td::Welcome& welcome);
public:
    VideoWorker(Renderer& renderer,const td::Welcome& welcome,bool desktopSRGB);
    ~VideoWorker() { stop(); }
    void stop();
    void startRaw(sockaddr_in address,uint64_t session);
    bool submit(td::Frame frame) { return inbox.push(std::move(frame),micros()); }
    void requestKey() { inbox.requestKey(); }
    bool takeIDRRequest() { return inbox.takeIDRRequest(); }
    void checkFailure();
    Latency decodeLatency() { std::lock_guard<std::mutex> lock(stateMutex); return latency; }
    uint64_t submittedFrames() const { return submitted.load(); }
    uint64_t decodedFrames() const { return decoded.load(); }
    uint64_t lastDecodedAt() const { return decodedAt.load(); }
    uint64_t receivedVideoBytes() const { return wireBytes.load(); }
    uint64_t lastRawReceivedAt() const { return rawReceivedAt.load(); }
    uint64_t discardedFrames() const { return busyDropped.load(); }
    uint64_t replacedFrames() { return rawInbox?rawInbox->replacedFrames():inbox.discardedFrames(); }
};
