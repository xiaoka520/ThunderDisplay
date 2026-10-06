#pragma once
#include "decoder.hpp"
#include "video_inbox.hpp"

// A connection-scoped MTA owns the decoder. Renderer has a separate presenter;
// neither receiving nor decoding waits for the display refresh/Present queue.
class VideoWorker {
public:
    struct Latency { uint32_t queueAverage=0,queueMaximum=0,decodeAverage=0,decodeMaximum=0; };
private:
    td::VideoInbox inbox;
    std::atomic<bool> stopped{false};
    std::atomic<uint64_t> submitted{0},decoded{0},decodedAt{0};
    std::mutex stateMutex;
    std::condition_variable initialized;
    bool ready=false;
    std::string failure;
    Latency latency;
    std::thread work;
    void run(Renderer& renderer,td::Welcome welcome,bool desktopSRGB);
public:
    VideoWorker(Renderer& renderer,const td::Welcome& welcome,bool desktopSRGB);
    ~VideoWorker() { stop(); }
    void stop();
    bool submit(td::Frame frame) { return inbox.push(std::move(frame),micros()); }
    void requestKey() { inbox.requestKey(); }
    bool takeIDRRequest() { return inbox.takeIDRRequest(); }
    void checkFailure();
    Latency decodeLatency() { std::lock_guard<std::mutex> lock(stateMutex); return latency; }
    uint64_t submittedFrames() const { return submitted.load(); }
    uint64_t decodedFrames() const { return decoded.load(); }
    uint64_t lastDecodedAt() const { return decodedAt.load(); }
};
