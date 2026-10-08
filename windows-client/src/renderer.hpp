#pragma once
#include "common.hpp"
#include <d3d11_1.h>
#include "presentation.hpp"
#include "recovery.hpp"
#include "diagnostics.hpp"
#include "latest_picture.hpp"
#include "raw_video_damage.hpp"
#include <array>

class Renderer {
public:
    struct Latency { uint32_t renderAverage=0,renderMaximum=0,ageAverage=0,ageMaximum=0; uint64_t replaced=0; };
private:
    struct RawPlanePicture {
        std::array<ComPtr<ID3D11Texture2D>,2> textures;
        std::array<ComPtr<ID3D11ShaderResourceView>,2> views;
    };
    struct Picture { ComPtr<IMFSample> sample; UINT width,height,fps; uint64_t arrivedAt; std::shared_ptr<RawPlanePicture> rawPlanes; };
    td::LatestPicture<Picture> pictures;
    HANDLE frameReady=nullptr,stopPresentation=nullptr;
    std::atomic<bool> presenterStopped{false};
    std::atomic<bool> repaintRequested{false};
    std::mutex presentationState;
    Latency latency;
    std::atomic<uint64_t> presented{0},lateIntervals{0},maximumInterval{0};
    uint64_t presentBusyCount=0,presentBusyLoggedAt=0;
    std::atomic<double> dxgiDisplayFPS{-1};
    DXGI_FRAME_STATISTICS previousDXGI{};
    uint64_t dxgiSampleAt=0;
    bool dxgiBaseline=false;
    void sampleDisplayStatistics(uint64_t now);
    std::string presentationFailure;
    std::thread presenter;
    void presentationLoop();
    bool presentPicture(const td::LatestPicture<Picture>::Item& item);
    HWND window;
    std::mutex mutex;
    std::mutex descriptionMutex;
    std::string cachedDescription="Color pipeline: waiting for presentation";
    // Pointer hit testing must never wait for GPU conversion or Present.
    std::mutex pointerMutex;
    std::optional<td::PresentationGeometry> pointerGeometry;
    UINT pointerWidth=0,pointerHeight=0;
    RECT pointerViewport{};
    void updatePointerGeometryLocked();
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context;
    std::shared_ptr<RawPlanePicture> rawBaseline;
    std::array<std::shared_ptr<RawPlanePicture>,3> rawPlanesPool;
    UINT rawWidth=0,rawHeight=0;
    bool rawBaselineReady=false;
    bool rawPlanesChecked=false;
    std::shared_ptr<RawPlanePicture> makeRawPlanes(UINT width,UINT height);
    void uploadRawPlanes(const std::shared_ptr<RawPlanePicture>& target,const td::Bytes& pixels,UINT width,UINT height,
        const std::vector<td::RawVideoRegion>& regions);
    void verifyRawPlaneUploads();
    ComPtr<ID3D11VideoDevice> video;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoContext1> videoContext1;
    ComPtr<IDXGISwapChain2> swap;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    UINT width=0,height=0,inputWidth=0,inputHeight=0,inputFPS=0;
    UINT liveFPS=60;
    bool tearing=false, vsync=false;
    bool explicitVideoColor=false,pixelExact=false,scalingActive=false;
    UINT sourceWidth=0,sourceHeight=0;
    uint8_t bitDepth=8;
    DXGI_FORMAT outputFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
    RECT sourceRect{};
    RECT destination{};
    ComPtr<ID3D11VertexShader> scalingVertex;
    ComPtr<ID3D11PixelShader> scalingPixel;
    ComPtr<ID3D11Buffer> scalingConstants;
    ComPtr<ID3D11Texture2D> convertedRGB, horizontalRGB;
    ComPtr<ID3D11ShaderResourceView> convertedView,horizontalView;
    ComPtr<ID3D11RenderTargetView> horizontalTarget;
    UINT scalingWidth=0,scalingHeight=0,horizontalWidth=0,horizontalHeight=0;
    uint8_t scalingQuality=1;
    std::string scalingFailure;
    bool desktopSRGB=false;
    ComPtr<ID3D11VertexShader> colorVertex;
    ComPtr<ID3D11PixelShader> colorPixel,colorBlitPixel;
    ComPtr<ID3D11Buffer> colorConstants;
    ComPtr<ID3D11SamplerState> colorSampler;
    ComPtr<ID3D11Texture2D> colorYUV,colorRGB;
    ComPtr<ID3D11ShaderResourceView> colorLuma,colorChroma,colorRGBView;
    ComPtr<ID3D11RenderTargetView> colorTarget;
    bool presentDesktopColor(ID3D11Texture2D* texture,UINT slice,UINT frameWidth,UINT frameHeight,const td::PresentationGeometry& geometry,
        ID3D11ShaderResourceView* nativeLuma=nullptr,ID3D11ShaderResourceView* nativeChroma=nullptr);
    bool presentBackBuffer();
    bool initScaling(UINT frameWidth,UINT frameHeight,UINT targetWidth,UINT cropHeight);
    void scaleToBackBuffer(ID3D11Texture2D* back,const td::PresentationGeometry& geometry,ID3D11ShaderResourceView* nativeRGB=nullptr);
    td::DisplayedFrame frameState;
    bool occluded=false;
    ComPtr<ID3D11Texture2D> retainedRGB;
    ComPtr<ID3D11ShaderResourceView> retainedView;
    ComPtr<ID3D11VertexShader> frozenVertex;
    ComPtr<ID3D11PixelShader> frozenPixel;
    ComPtr<ID3D11SamplerState> frozenSampler;
    bool repaintImageLocked();
    bool resizeLocked();
    void checkPresent(HRESULT result,const char* operation);
public:
    Renderer(HWND hwnd, bool vsync);
    ~Renderer();
    ID3D11Device* device() const { return device_.Get(); }
    void present(IMFSample* sample, UINT frameWidth, UINT frameHeight, UINT fps,uint64_t arrivedAt);
    bool presentRaw(const td::Bytes& pixels,UINT frameWidth,UINT frameHeight,UINT fps,uint64_t arrivedAt,
        const std::vector<td::RawVideoRegion>* changed=nullptr);
    void checkFailure();
    Latency presentationLatency();
    uint64_t presentedFrames() const { return presented.load(); }
    uint64_t lateFrameIntervals() const { return lateIntervals.load(); }
    uint64_t replacedFrames() { return pictures.discardedPictures(); }
    uint64_t takeMaximumFrameInterval() { return maximumInterval.exchange(0); }
    double displayFPS() const { return micros()-lastPresentation()<2000000?dxgiDisplayFPS.load():-1; }
    RECT viewport();
    void resize();
    void configureBitDepth(uint8_t depth);
    void setDesktopSRGB(bool value) { std::lock_guard<std::mutex> lock(mutex); desktopSRGB=value; }
    bool pointerPosition(int px,int py,bool dragging,int32_t& x,int32_t& y);
    bool remotePointerPosition(uint16_t x,uint16_t y,int& px,int& py);
    void setPixelExact(bool value) { std::lock_guard<std::mutex> lock(mutex); pixelExact=value; updatePointerGeometryLocked(); }
    void setScalingQuality(uint8_t value) { std::lock_guard<std::mutex> lock(mutex); scalingQuality=std::min<uint8_t>(value,1); }
    bool hasFrame() const { return frameState.fresh(micros()); }
    bool presentationExpected() const { return IsWindowVisible(window) && !IsIconic(window); }
    bool hasImage() const { return frameState.hasImage(); }
    uint64_t lastPresentation() const { return frameState.lastPresentation(); }
    std::string colorDescription();
    // The window message thread must stay available to DXGI. Present can wait
    // for that thread, so painting and resizing only request presenter work.
    bool repaintImage() { repaintRequested=true; return frameState.hasImage(); }
    bool resetFrame(bool preserve=false,bool keepSnapshot=false,const char* reason="session reset");
};
