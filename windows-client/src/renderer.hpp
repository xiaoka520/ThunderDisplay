#pragma once
#include "common.hpp"
#include <d3d11_1.h>
#include "presentation.hpp"
#include "recovery.hpp"
#include "diagnostics.hpp"
#include "latest_picture.hpp"

class Renderer {
public:
    struct Latency { uint32_t renderAverage=0,renderMaximum=0,ageAverage=0,ageMaximum=0; uint64_t replaced=0; };
private:
    struct Picture { ComPtr<IMFSample> sample; UINT width,height,fps; uint64_t arrivedAt; };
    td::LatestPicture<Picture> pictures;
    HANDLE frameReady=nullptr,stopPresentation=nullptr;
    std::atomic<bool> presenterStopped{false};
    std::mutex presentationState;
    Latency latency;
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
    ComPtr<ID3D11VideoDevice> video;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoContext1> videoContext1;
    ComPtr<IDXGISwapChain2> swap;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    UINT width=0,height=0,inputWidth=0,inputHeight=0,inputFPS=0;
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
    void presentDesktopColor(ID3D11Texture2D* texture,UINT slice,UINT frameWidth,UINT frameHeight,const td::PresentationGeometry& geometry);
    void presentBackBuffer();
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
    void checkFailure();
    Latency presentationLatency();
    RECT viewport();
    void resize();
    void configureBitDepth(uint8_t depth);
    void setDesktopSRGB(bool value) { std::lock_guard<std::mutex> lock(mutex); desktopSRGB=value; }
    bool pointerPosition(int px,int py,bool dragging,int32_t& x,int32_t& y);
    void setPixelExact(bool value) { std::lock_guard<std::mutex> lock(mutex); pixelExact=value; updatePointerGeometryLocked(); }
    void setScalingQuality(uint8_t value) { std::lock_guard<std::mutex> lock(mutex); scalingQuality=std::min<uint8_t>(value,1); }
    bool hasFrame() const { return frameState.fresh(micros()); }
    bool presentationExpected() const { return IsWindowVisible(window) && !IsIconic(window); }
    bool hasImage() const { return frameState.hasImage(); }
    uint64_t lastPresentation() const { return frameState.lastPresentation(); }
    std::string colorDescription();
    bool repaintImage() { std::lock_guard<std::mutex> lock(mutex); return repaintImageLocked(); }
    bool resetFrame(bool preserve=false,bool keepSnapshot=false,const char* reason="session reset");
};
