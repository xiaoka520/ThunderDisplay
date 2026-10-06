#pragma once
#include "renderer.hpp"

class Decoder {
    Renderer& renderer;
    td::Welcome welcome;
    ComPtr<IMFTransform> transform;
    ComPtr<IMFDXGIDeviceManager> manager;
    ComPtr<IMFMediaEventGenerator> events;
    DWORD inputID=0,outputID=0;
    UINT outputWidth=0,outputHeight=0;
    bool async=false, started=false, discontinuity=true;
    bool desktopSRGB=false;
    unsigned credits=0;
    uint64_t inputs=0, outputs=0;
    uint64_t presentationTime=0;
    ComPtr<IMFSample> latestOutput;
    UINT latestWidth=0,latestHeight=0;
    uint64_t latestArrivedAt=0;
    std::deque<std::pair<uint64_t,uint64_t>> arrivals;
    struct Pending { td::Frame frame; uint64_t arrivedAt; };
    std::deque<Pending> pending;
    void configure(IMFTransform* candidate);
    void outputType();
    bool output();
    void presentLatest();
    void input(const td::Frame& frame,uint64_t arrivedAt);
public:
    Decoder(Renderer& renderer, const td::Welcome& welcome, bool desktopSRGB=false);
    ~Decoder();
    // False means a dependency was discarded: flush and resume from a new IDR.
    bool submit(td::Frame frame,uint64_t arrivedAt);
    bool pump();
    void flush();
    uint64_t submittedFrames() const { return inputs; }
    uint64_t decodedFrames() const { return outputs; }
    uint64_t presentationMicros() const { return presentationTime; }
};
