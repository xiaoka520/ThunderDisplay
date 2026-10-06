#include "renderer.hpp"
#include <d3d10.h>
#include <d3dcompiler.h>
#include "scaler.hpp"
#include "desktop_color.hpp"

bool Renderer::initScaling(UINT fw,UINT fh,UINT tw,UINT ch) {
    if(!scalingQuality || !scalingFailure.empty()) return false;
    try {
        if(!scalingVertex) {
            ComPtr<ID3DBlob> vertex,pixel,error;
            check(D3DCompile(ScalingShader,std::strlen(ScalingShader),"ThunderDisplay scaler",nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vertex,&error),"Compile scaling vertex shader");
            check(D3DCompile(ScalingShader,std::strlen(ScalingShader),"ThunderDisplay scaler",nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&pixel,&error),"Compile scaling pixel shader");
            check(device_->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&scalingVertex),"Scaling vertex shader");
            check(device_->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&scalingPixel),"Scaling pixel shader");
            D3D11_BUFFER_DESC cb{}; cb.ByteWidth=48; cb.Usage=D3D11_USAGE_DEFAULT; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            check(device_->CreateBuffer(&cb,nullptr,&scalingConstants),"Scaling parameters");
        }
        if(!horizontalRGB || (!desktopSRGB && !convertedRGB) || scalingWidth!=fw || scalingHeight!=fh || horizontalWidth!=tw || horizontalHeight!=ch) {
            context->ClearState();
            convertedRGB.Reset(); horizontalRGB.Reset(); convertedView.Reset(); horizontalView.Reset(); horizontalTarget.Reset();
            D3D11_TEXTURE2D_DESC d{}; d.Width=fw; d.Height=fh; d.MipLevels=1; d.ArraySize=1; d.SampleDesc.Count=1;
            d.Format=outputFormat; d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            if(!desktopSRGB) {
                check(device_->CreateTexture2D(&d,nullptr,&convertedRGB),"Native RGB conversion texture");
                check(device_->CreateShaderResourceView(convertedRGB.Get(),nullptr,&convertedView),"Native RGB shader view");
            }
            d.Width=tw; d.Height=ch;
            check(device_->CreateTexture2D(&d,nullptr,&horizontalRGB),"Horizontal scaling texture");
            check(device_->CreateShaderResourceView(horizontalRGB.Get(),nullptr,&horizontalView),"Horizontal shader view");
            check(device_->CreateRenderTargetView(horizontalRGB.Get(),nullptr,&horizontalTarget),"Horizontal render target");
            scalingWidth=fw; scalingHeight=fh; horizontalWidth=tw; horizontalHeight=ch;
        }
        return true;
    } catch(const std::exception& e) { scalingFailure=e.what(); enumerator.Reset(); processor.Reset(); return false; }
}
void Renderer::scaleToBackBuffer(ID3D11Texture2D* back,const td::PresentationGeometry& geometry,ID3D11ShaderResourceView* nativeRGB) {
    ComPtr<ID3D11RenderTargetView> target; check(device_->CreateRenderTargetView(back,nullptr,&target),"Scaling output view");
    context->ClearState();
    const float black[4]={0,0,0,1}; context->ClearRenderTargetView(target.Get(),black);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(scalingVertex.Get(),nullptr,0); context->PSSetShader(scalingPixel.Get(),nullptr,0);
    auto cb=scalingConstants.Get(); context->PSSetConstantBuffers(0,1,&cb);
    auto draw=[&](ID3D11ShaderResourceView* input,ID3D11RenderTargetView* output,D3D11_VIEWPORT viewport,
                  float sw,float sh,float tw,float th,float ox,float oy,float cw,float ch,float ax,float ay) {
        const float parameters[12]={sw,sh,tw,th,ox,oy,cw,ch,ax,ay,0,0};
        context->UpdateSubresource(scalingConstants.Get(),0,nullptr,parameters,0,0);
        context->OMSetRenderTargets(1,&output,nullptr); context->RSSetViewports(1,&viewport);
        context->PSSetShaderResources(0,1,&input); context->Draw(3,0);
        ID3D11ShaderResourceView* none=nullptr; context->PSSetShaderResources(0,1,&none);
        context->OMSetRenderTargets(0,nullptr,nullptr);
    };
    auto& s=geometry.source; auto& d=geometry.destination;
    draw(nativeRGB?nativeRGB:convertedView.Get(),horizontalTarget.Get(),{0,0,float(horizontalWidth),float(horizontalHeight),0,1},
        float(scalingWidth),float(scalingHeight),float(horizontalWidth),float(horizontalHeight),
        float(s.left),float(s.top),float(s.right-s.left),float(s.bottom-s.top),1,0);
    draw(horizontalView.Get(),target.Get(),{float(d.left),float(d.top),float(d.right-d.left),float(d.bottom-d.top),0,1},
        float(horizontalWidth),float(horizontalHeight),float(d.right-d.left),float(d.bottom-d.top),0,0,
        float(horizontalWidth),float(horizontalHeight),0,1);
    context->ClearState();
}

Renderer::Renderer(HWND hwnd, bool sync): window(hwnd), vsync(sync) {
    D3D_FEATURE_LEVEL level;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&device_,&level,&context),"Create hardware D3D11 device");
    ComPtr<ID3D10Multithread> multithread;
    check(context.As(&multithread),"D3D multithread interface"); multithread->SetMultithreadProtected(TRUE);
    check(device_.As(&video),"D3D11 video device"); check(context.As(&videoContext),"D3D11 video context");
    videoContext.As(&videoContext1);
    ComPtr<IDXGIDevice> dxgi; check(device_.As(&dxgi),"DXGI device");
    ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter),"DXGI adapter");
    ComPtr<IDXGIFactory2> factory; check(adapter->GetParent(IID_PPV_ARGS(&factory)),"DXGI factory");
    ComPtr<IDXGIFactory5> factory5;
    if(SUCCEEDED(factory.As(&factory5))) { BOOL supported=FALSE; if(SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&supported,sizeof(supported)))) tearing=supported!=FALSE; }
    RECT r; GetClientRect(hwnd,&r); width=std::max<LONG>(1,r.right); height=std::max<LONG>(1,r.bottom);
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=width; desc.Height=height; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2;
    desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.Scaling=DXGI_SCALING_STRETCH;
    desc.Flags=DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT|(tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0);
    ComPtr<IDXGISwapChain1> chain;
    check(factory->CreateSwapChainForHwnd(device_.Get(),hwnd,&desc,nullptr,nullptr,&chain),"Create swap chain");
    check(chain.As(&swap),"Swap chain 2"); check(swap->SetMaximumFrameLatency(1),"Limit presentation queue");
    ComPtr<IDXGISwapChain3> colorSwap;
    check(chain.As(&colorSwap),"Swap chain color interface");
    UINT colorSupport=0;
    check(colorSwap->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,&colorSupport),"Check SDR output color space");
    if(!(colorSupport&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) throw std::runtime_error("Display does not support the SDR RGB presentation color space");
    check(colorSwap->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709),"Set SDR full-range RGB presentation");
    factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
    destination={0,0,LONG(width),LONG(height)};
    updatePointerGeometryLocked();
    frameReady=swap->GetFrameLatencyWaitableObject();
    if(!frameReady) throw std::runtime_error("Presentation latency waitable object unavailable");
    stopPresentation=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stopPresentation) { CloseHandle(frameReady); throw std::runtime_error("Create presentation stop event failed"); }
    try { presenter=std::thread([this]{presentationLoop();}); }
    catch(...) { CloseHandle(stopPresentation); CloseHandle(frameReady); throw; }
}
Renderer::~Renderer() {
    presenterStopped=true; pictures.retire(true); SetEvent(stopPresentation);
    if(presenter.joinable()) presenter.join();
    if(stopPresentation) CloseHandle(stopPresentation);
    if(frameReady) CloseHandle(frameReady);
}
void Renderer::checkFailure() {
    std::lock_guard<std::mutex> lock(presentationState);
    if(!presentationFailure.empty()) throw std::runtime_error(presentationFailure);
}
Renderer::Latency Renderer::presentationLatency() {
    std::lock_guard<std::mutex> lock(presentationState); return latency;
}
void Renderer::presentationLoop() {
    const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    try {
        check(com,"Initialize presentation apartment"); SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
        uint64_t statsAt=micros(),count=0,renderTotal=0,renderMax=0,ageTotal=0,ageMax=0,ageCount=0,statsGeneration=UINT64_MAX;
        bool havePermit=false;
        while(!presenterStopped) {
            if(!pictures.wait()) continue;
            if(!havePermit) {
                const HANDLE waits[]={stopPresentation,frameReady};
                const auto result=WaitForMultipleObjects(2,waits,FALSE,100);
                if(result==WAIT_OBJECT_0) break;
                if(result==WAIT_TIMEOUT) continue;
                if(result!=WAIT_OBJECT_0+1) throw std::runtime_error("Wait for presentation frame failed");
                havePermit=true;
            }
            // Fetch AFTER display readiness, so newly decoded pictures replace
            // pending samples while waiting instead of becoming a stale FIFO.
            auto item=pictures.take(); if(!item || !pictures.current(*item)) continue;
            if(statsGeneration!=item->generation) {
                statsGeneration=item->generation; statsAt=micros(); count=renderTotal=renderMax=ageTotal=ageMax=ageCount=0;
            }
            const auto began=micros();
            if(!presentPicture(*item)) continue; // Keep readiness if no Present occurred.
            havePermit=false; const auto now=micros();
            const auto render=now-began,age=now>=item->picture.arrivedAt?now-item->picture.arrivedAt:0;
            ++count; renderTotal+=render; renderMax=std::max(renderMax,render);
            if(item->picture.arrivedAt) { ++ageCount; ageTotal+=age; ageMax=std::max(ageMax,age); }
            if(now-statsAt>=5000000) {
                Latency snapshot{uint32_t(renderTotal/std::max<uint64_t>(1,count)),uint32_t(renderMax),
                    uint32_t(ageTotal/std::max<uint64_t>(1,ageCount)),uint32_t(ageMax),pictures.discardedPictures()};
                { std::lock_guard<std::mutex> lock(presentationState); latency=snapshot; }
                diagnosticLog("display.latency","render_us_avg/max="+std::to_string(snapshot.renderAverage)+"/"+std::to_string(snapshot.renderMaximum)+
                    " arrival_to_present_us_avg/max="+std::to_string(snapshot.ageAverage)+"/"+std::to_string(snapshot.ageMaximum)+
                    " measured="+std::to_string(ageCount)+" replaced="+std::to_string(snapshot.replaced));
                statsAt=now; count=renderTotal=renderMax=ageTotal=ageMax=ageCount=0;
            }
        }
    } catch(const std::exception& e) {
        { std::lock_guard<std::mutex> lock(presentationState); presentationFailure=e.what(); }
        diagnosticLog("display.worker.error",e.what()); pictures.retire(true);
    }
    if(SUCCEEDED(com)) CoUninitialize();
}
void Renderer::present(IMFSample* sample,UINT frameWidth,UINT frameHeight,UINT fps,uint64_t arrivedAt) {
    checkFailure(); Picture picture{sample,frameWidth,frameHeight,fps,arrivedAt}; pictures.push(std::move(picture));
}
void Renderer::updatePointerGeometryLocked() {
    std::lock_guard<std::mutex> lock(pointerMutex);
    pointerWidth=sourceWidth; pointerHeight=sourceHeight;
    if(sourceWidth>=2 && sourceHeight>=2) {
        pointerGeometry.emplace(sourceWidth,sourceHeight,width,height,pixelExact);
        auto d=pointerGeometry->destination; pointerViewport={d.left,d.top,d.right,d.bottom};
    } else { pointerGeometry.reset(); pointerViewport={0,0,LONG(width),LONG(height)}; }
}
bool Renderer::resizeLocked() {
    RECT r; GetClientRect(window,&r); UINT w=std::max<LONG>(1,r.right),h=std::max<LONG>(1,r.bottom);
    if(w==width && h==height) return false;
    diagnosticLog("display.resize",std::to_string(width)+"x"+std::to_string(height)+" -> "+std::to_string(w)+"x"+std::to_string(h));
    context->ClearState();
    check(swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT|
        (tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0)),"Resize swap chain");
    width=w; height=h; enumerator.Reset(); processor.Reset();
    updatePointerGeometryLocked();
    return true;
}
void Renderer::configureBitDepth(uint8_t depth) {
    std::lock_guard<std::mutex> lock(mutex);
    if(depth!=8 && depth!=10) throw std::runtime_error("Unsupported stream precision");
    if(bitDepth==depth) return;
    diagnosticLog("display.precision",std::to_string(bitDepth)+" -> "+std::to_string(depth));
    auto format=depth==10?DXGI_FORMAT_R10G10B10A2_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
    UINT support=0; check(device_->CheckFormatSupport(format,&support),"Check RGB output precision");
    if(!(support&D3D11_FORMAT_SUPPORT_RENDER_TARGET)) throw std::runtime_error("GPU does not support 10-bit RGB output");
    context->ClearState();
    check(swap->ResizeBuffers(0,width,height,format,DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT|
        (tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0)),"Configure swap chain precision");
    ComPtr<IDXGISwapChain3> colorSwap; check(swap.As(&colorSwap),"Swap chain precision color interface");
    check(colorSwap->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709),"Set SDR output color space");
    bitDepth=depth; outputFormat=format; enumerator.Reset(); processor.Reset(); explicitVideoColor=false;
    convertedRGB.Reset(); horizontalRGB.Reset(); scalingFailure.clear();
    colorRGB.Reset(); colorTarget.Reset(); colorRGBView.Reset();
    repaintImageLocked();
}
bool Renderer::pointerPosition(int px,int py,bool dragging,int32_t& x,int32_t& y) {
    std::lock_guard<std::mutex> lock(pointerMutex);
    return pointerGeometry && pointerGeometry->pointer(px,py,dragging,pointerWidth,pointerHeight,x,y);
}
void Renderer::resize() { std::lock_guard<std::mutex> lock(mutex); if(resizeLocked()) repaintImageLocked(); }
RECT Renderer::viewport() { std::lock_guard<std::mutex> lock(pointerMutex); return pointerViewport; }
std::string Renderer::colorDescription() {
    // Periodic status on the socket thread must not wait for a busy Present.
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()) { std::lock_guard<std::mutex> cached(descriptionMutex); return cachedDescription; }
    auto description=std::string(desktopSRGB?"Color pipeline: sRGB SDR / BT.709 matrix | ":"Color pipeline: BT.709 SDR | ")+(bitDepth==10?"P010 64-940 -> RGB10 0-1023 | ":"NV12 16-235 -> RGB8 0-255 | ")+
        (desktopSRGB?(colorRGB?"Explicit sRGB shader conversion":"Waiting for conversion"):!processor?"Waiting for conversion":explicitVideoColor?"Explicit DXGI conversion":"Legacy conversion")+
        "\nScaling: "+(scalingActive?std::string("Lanczos-2 (no sharpening)"):std::string(desktopSRGB?"Bilinear compatibility":"Video processor compatibility"))+
        (scalingFailure.empty()?"":" (fallback: "+scalingFailure+")")+
        (sourceWidth?"\nStream pixels: "+std::to_string(sourceWidth)+"x"+std::to_string(sourceHeight)+
        " | Viewport: "+std::to_string(destination.right-destination.left)+"x"+std::to_string(destination.bottom-destination.top)+
        " | Display scale: "+std::to_string(pixelExact?100:((destination.right-destination.left)*100/sourceWidth))+"%"+(pixelExact?" (1:1)":""):"");
    { std::lock_guard<std::mutex> cached(descriptionMutex); cachedDescription=description; }
    return description;
}
bool Renderer::presentPicture(const td::LatestPicture<Picture>::Item& item) {
    std::lock_guard<std::mutex> lock(mutex);
    if(!pictures.current(item)) return false;
    auto* sample=item.picture.sample.Get(); const auto frameWidth=item.picture.width,frameHeight=item.picture.height,fps=item.picture.fps;
    if(IsIconic(window)) return false;
    resizeLocked();
    ComPtr<IMFMediaBuffer> buffer; check(sample->GetBufferByIndex(0,&buffer),"Decoded media buffer");
    ComPtr<IMFDXGIBuffer> gpu;
    if(FAILED(buffer.As(&gpu))) throw std::runtime_error("Decoder returned CPU memory. Hardware D3D11 decoding is required; update GPU drivers or use --codec h264.");
    ComPtr<ID3D11Texture2D> texture; check(gpu->GetResource(IID_PPV_ARGS(&texture)),"Decoded GPU texture");
    UINT slice; check(gpu->GetSubresourceIndex(&slice),"Decoded texture index");
    D3D11_TEXTURE2D_DESC textureDesc; texture->GetDesc(&textureDesc);
    if(textureDesc.Format!=(bitDepth==10?DXGI_FORMAT_P010:DXGI_FORMAT_NV12)) throw std::runtime_error("Decoder output precision does not match the negotiated stream");
    td::PresentationGeometry geometry(frameWidth,frameHeight,width,height,pixelExact);
    auto& dest=geometry.destination; auto& crop=geometry.source;
    if(desktopSRGB) {
        try { presentDesktopColor(texture.Get(),slice,frameWidth,frameHeight,geometry); }
        catch(const std::exception& e) { throw std::runtime_error(std::string("DesktopColorUnavailable: ")+e.what()); }
        return true;
    }
    // The shader supports anti-aliasing footprints up to a 4:1 shrink per axis.
    bool scaleSupported=double(crop.right-crop.left)/(dest.right-dest.left)<=4 && double(crop.bottom-crop.top)/(dest.bottom-dest.top)<=4;
    bool highQuality=!pixelExact && scaleSupported && initScaling(frameWidth,frameHeight,UINT(dest.right-dest.left),UINT(crop.bottom-crop.top));
    scalingActive=highQuality;
    if(!enumerator || inputWidth!=textureDesc.Width || inputHeight!=textureDesc.Height || inputFPS!=fps) {
        inputWidth=textureDesc.Width; inputHeight=textureDesc.Height; inputFPS=fps;
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC d{}; d.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        d.InputWidth=inputWidth; d.InputHeight=inputHeight; d.OutputWidth=highQuality?frameWidth:width; d.OutputHeight=highQuality?frameHeight:height;
        d.InputFrameRate={fps,1}; d.OutputFrameRate={fps,1}; d.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        enumerator.Reset(); processor.Reset();
        check(video->CreateVideoProcessorEnumerator(&d,&enumerator),"Video processor enumerator");
        check(video->CreateVideoProcessor(enumerator.Get(),0,&processor),"Video processor");
        explicitVideoColor=false;
        ComPtr<ID3D11VideoProcessorEnumerator1> colorEnumerator;
        if(videoContext1 && SUCCEEDED(enumerator.As(&colorEnumerator))) {
            BOOL supported=FALSE;
            if(SUCCEEDED(colorEnumerator->CheckVideoProcessorFormatConversion(textureDesc.Format,
                DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709,outputFormat,
                DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,&supported))) explicitVideoColor=supported!=FALSE;
        }
    }
    sourceWidth=frameWidth; sourceHeight=frameHeight;
    updatePointerGeometryLocked();
    if(bitDepth==10 && !explicitVideoColor) throw std::runtime_error("GPU does not support P010 to 10-bit SDR RGB conversion");
    destination={dest.left,dest.top,dest.right,dest.bottom}; sourceRect={crop.left,crop.top,crop.right,crop.bottom};
    RECT output={0,0,LONG(width),LONG(height)};
    RECT native={0,0,LONG(frameWidth),LONG(frameHeight)};
    videoContext->VideoProcessorSetStreamFrameFormat(processor.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    videoContext->VideoProcessorSetStreamSourceRect(processor.Get(),0,TRUE,highQuality?&native:&sourceRect);
    videoContext->VideoProcessorSetStreamDestRect(processor.Get(),0,TRUE,highQuality?&native:&destination);
    videoContext->VideoProcessorSetOutputTargetRect(processor.Get(),TRUE,highQuality?&native:&output);
    videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(),0,FALSE);
    D3D11_VIDEO_COLOR black{}; black.RGBA.A=1; videoContext->VideoProcessorSetOutputBackgroundColor(processor.Get(),FALSE,&black);
    if(explicitVideoColor) {
        videoContext1->VideoProcessorSetStreamColorSpace1(processor.Get(),0,DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
        videoContext1->VideoProcessorSetOutputColorSpace1(processor.Get(),DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    } else {
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE in{}; in.YCbCr_Matrix=1; in.Nominal_Range=D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE out{}; out.RGB_Range=0; out.Nominal_Range=D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        videoContext->VideoProcessorSetStreamColorSpace(processor.Get(),0,&in); videoContext->VideoProcessorSetOutputColorSpace(processor.Get(),&out);
    }
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{}; iv.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
    iv.Texture2D.MipSlice=slice%textureDesc.MipLevels; iv.Texture2D.ArraySlice=slice/textureDesc.MipLevels;
    ComPtr<ID3D11VideoProcessorInputView> input;
    check(video->CreateVideoProcessorInputView(texture.Get(),enumerator.Get(),&iv,&input),"Video input view");
    ComPtr<ID3D11Texture2D> back; check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Swap chain back buffer");
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{}; ov.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> target;
    check(video->CreateVideoProcessorOutputView(highQuality?convertedRGB.Get():back.Get(),enumerator.Get(),&ov,&target),"Video output view");
    D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable=TRUE; stream.pInputSurface=input.Get();
    check(videoContext->VideoProcessorBlt(processor.Get(),target.Get(),0,1,&stream),"GPU YUV to RGB conversion");
    if(highQuality) scaleToBackBuffer(back.Get(),geometry);
    presentBackBuffer();
    return true;
}
void Renderer::presentBackBuffer() {
    {
        ComPtr<ID3D11Texture2D> back; check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Handover frame buffer");
        D3D11_TEXTURE2D_DESC d{},old{}; back->GetDesc(&d); if(retainedRGB) retainedRGB->GetDesc(&old);
        if(!retainedRGB || d.Width!=old.Width || d.Height!=old.Height || d.Format!=old.Format) {
            retainedView.Reset(); retainedRGB.Reset();
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.Usage=D3D11_USAGE_DEFAULT; d.CPUAccessFlags=0; d.MiscFlags=0;
            check(device_->CreateTexture2D(&d,nullptr,&retainedRGB),"Retain handover frame");
            check(device_->CreateShaderResourceView(retainedRGB.Get(),nullptr,&retainedView),"Handover frame view");
        }
        context->ClearState(); context->CopyResource(retainedRGB.Get(),back.Get());
    }
    auto started=micros();
    auto result=swap->Present(vsync?1:0,(!vsync && tearing)?DXGI_PRESENT_ALLOW_TEARING:0);
    checkPresent(result,"Present");
    if(result!=DXGI_STATUS_OCCLUDED) {
        auto now=micros(),previous=frameState.lastPresentation();
        if(now-started>100000) diagnosticLog("display.present.slow","duration_us="+std::to_string(now-started));
        if(previous && now>previous && now-previous>500000)
            diagnosticLog("display.frame.gap","duration_us="+std::to_string(now-previous));
        if(frameState.present(now)) {
            diagnosticLog("display.live","first or resumed frame");
            PostMessageW(window,FramePresentedMessage,0,0);
        }
    }
}
void Renderer::checkPresent(HRESULT result,const char* operation) {
    if(FAILED(result)) {
        std::ostringstream error;
        error<<operation<<" HRESULT=0x"<<std::hex<<uint32_t(result)
             <<" device_removed=0x"<<uint32_t(device_->GetDeviceRemovedReason());
        diagnosticLog("display.error",error.str());
        check(result,operation);
    }
    const bool hidden=result==DXGI_STATUS_OCCLUDED;
    if(hidden!=occluded) diagnosticLog("display.occlusion",hidden?"occluded":"visible");
    occluded=hidden;
}
bool Renderer::repaintImageLocked() {
    if(!frameState.hasImage() || !retainedRGB || IsIconic(window)) return false;
    resizeLocked();
    ComPtr<ID3D11Texture2D> back; check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Retained frame output");
    D3D11_TEXTURE2D_DESC saved{},output{}; retainedRGB->GetDesc(&saved); back->GetDesc(&output);
    // Preserve pixels exactly whenever the buffer geometry/precision matches.
    if(saved.Width==output.Width && saved.Height==output.Height && saved.Format==output.Format) {
        context->ClearState(); context->CopyResource(back.Get(),retainedRGB.Get());
        checkPresent(swap->Present(0,(!vsync && tearing)?DXGI_PRESENT_ALLOW_TEARING:0),"Present retained frame");
        return true;
    }
    if(!frozenVertex) {
        const char* shader=R"(
Texture2D<float4> image : register(t0); SamplerState sampleImage : register(s0);
struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }
float4 ps(V v) : SV_Target { return image.Sample(sampleImage,v.uv); }
)";
        ComPtr<ID3DBlob> vs,ps,error;
        check(D3DCompile(shader,std::strlen(shader),"ThunderDisplay handover",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error),"Handover vertex shader");
        check(D3DCompile(shader,std::strlen(shader),"ThunderDisplay handover",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error),"Handover pixel shader");
        check(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&frozenVertex),"Handover vertex shader");
        check(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&frozenPixel),"Handover pixel shader");
        D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD=D3D11_FLOAT32_MAX;
        check(device_->CreateSamplerState(&s,&frozenSampler),"Handover sampler");
    }
    ComPtr<ID3D11RenderTargetView> target; check(device_->CreateRenderTargetView(back.Get(),nullptr,&target),"Handover output view");
    td::PresentationGeometry geometry(saved.Width,saved.Height,width,height,false); auto& r=geometry.destination;
    context->ClearState(); const float black[4]={0,0,0,1}; context->ClearRenderTargetView(target.Get(),black);
    auto render=target.Get(); auto view=retainedView.Get(); auto sampler=frozenSampler.Get();
    context->OMSetRenderTargets(1,&render,nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(frozenVertex.Get(),nullptr,0); context->PSSetShader(frozenPixel.Get(),nullptr,0);
    context->PSSetShaderResources(0,1,&view); context->PSSetSamplers(0,1,&sampler);
    D3D11_VIEWPORT viewport{float(r.left),float(r.top),float(r.right-r.left),float(r.bottom-r.top),0,1};
    context->RSSetViewports(1,&viewport); context->Draw(3,0); context->ClearState();
    // A repaint of the saved pixels never advances live-frame freshness.
    auto result=swap->Present(0,(!vsync && tearing)?DXGI_PRESENT_ALLOW_TEARING:0);
    checkPresent(result,"Present retained frame");
    return true;
}
bool Renderer::resetFrame(bool preserve,bool keepSnapshot,const char* reason) {
    // Drop old-session samples before the graphics lock. In-flight Present
    // completes before the retained snapshot is made under that same lock.
    pictures.retire();
    { std::lock_guard<std::mutex> lock(presentationState); latency={}; }
    std::lock_guard<std::mutex> lock(mutex);
    const bool hold=preserve && retainedRGB;
    diagnosticLog(hold?"display.hold":"display.clear",reason);
    frameState.reset(hold);
    if(hold) {
        try { repaintImageLocked(); InvalidateRect(window,nullptr,FALSE); return true; }
        catch(const std::exception& e) {
            // A removed device cannot keep a valid snapshot. Recovery/teardown
            // must proceed instead of letting a worker exception terminate us.
            diagnosticLog("display.hold.error",e.what());
            frameState.reset(false); keepSnapshot=false;
            diagnosticLog("display.clear","retained image repaint failed");
        }
    }
    if(!keepSnapshot) { retainedView.Reset(); retainedRGB.Reset(); }
    // The swap chain owns the visible pixels. A GDI repaint does not retire them.
    if(swap) {
        ComPtr<ID3D11Texture2D> back;
        ComPtr<ID3D11RenderTargetView> target;
        if(SUCCEEDED(swap->GetBuffer(0,IID_PPV_ARGS(&back))) && SUCCEEDED(device_->CreateRenderTargetView(back.Get(),nullptr,&target))) {
            const float black[4]={0,0,0,1}; context->ClearRenderTargetView(target.Get(),black);
            auto result=swap->Present(0,(!vsync && tearing)?DXGI_PRESENT_ALLOW_TEARING:0);
            // Teardown must still complete even if the GPU was removed.
            if(FAILED(result)) diagnosticLog("display.clear.error","HRESULT="+std::to_string(uint32_t(result)));
        }
    }
    InvalidateRect(window,nullptr,FALSE);
    return false;
}
void Renderer::presentDesktopColor(ID3D11Texture2D* texture,UINT slice,UINT fw,UINT fh,const td::PresentationGeometry& geometry) {
    if(!colorVertex || !colorPixel || !colorBlitPixel || !colorConstants || !colorSampler) {
        ComPtr<ID3DBlob> vs,ps,blit,error;
        auto compile=[&](const char* entry,const char* profile,ID3DBlob** output) {
            check(D3DCompile(DesktopColorShader,std::strlen(DesktopColorShader),"ThunderDisplay desktop color",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,output,&error),"Compile desktop color shader");
        };
        compile("vs","vs_5_0",&vs); compile("convert","ps_5_0",&ps); compile("blit","ps_5_0",&blit);
        check(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&colorVertex),"Desktop color vertex shader");
        check(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&colorPixel),"Desktop color pixel shader");
        check(device_->CreatePixelShader(blit->GetBufferPointer(),blit->GetBufferSize(),nullptr,&colorBlitPixel),"Desktop color blit shader");
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=32; cb.Usage=D3D11_USAGE_DEFAULT; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device_->CreateBuffer(&cb,nullptr,&colorConstants),"Desktop color parameters");
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxAnisotropy=1; sd.ComparisonFunc=D3D11_COMPARISON_NEVER; sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(device_->CreateSamplerState(&sd,&colorSampler),"Desktop chroma sampler");
    }
    D3D11_TEXTURE2D_DESC source{}; texture->GetDesc(&source);
    D3D11_TEXTURE2D_DESC previous{}; if(colorYUV) colorYUV->GetDesc(&previous);
    if(!colorYUV || !colorLuma || !colorChroma || previous.Width!=source.Width || previous.Height!=source.Height || previous.Format!=source.Format) {
        colorYUV.Reset(); colorLuma.Reset(); colorChroma.Reset();
        auto d=source; d.MipLevels=1; d.ArraySize=1; d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags=0; d.MiscFlags=0;
        check(device_->CreateTexture2D(&d,nullptr,&colorYUV),"Desktop GPU YUV copy texture");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; view.Texture2D.MipLevels=1;
        view.Format=bitDepth==10?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM;
        check(device_->CreateShaderResourceView(colorYUV.Get(),&view,&colorLuma),"Desktop luma view");
        view.Format=bitDepth==10?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM;
        check(device_->CreateShaderResourceView(colorYUV.Get(),&view,&colorChroma),"Desktop chroma view");
    }
    previous={}; if(colorRGB) colorRGB->GetDesc(&previous);
    if(!colorRGB || !colorTarget || !colorRGBView || previous.Width!=fw || previous.Height!=fh || previous.Format!=outputFormat) {
        colorRGB.Reset(); colorTarget.Reset(); colorRGBView.Reset();
        D3D11_TEXTURE2D_DESC d{}; d.Width=fw; d.Height=fh; d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=outputFormat; d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        check(device_->CreateTexture2D(&d,nullptr,&colorRGB),"Desktop sRGB texture");
        check(device_->CreateRenderTargetView(colorRGB.Get(),nullptr,&colorTarget),"Desktop sRGB target");
        check(device_->CreateShaderResourceView(colorRGB.Get(),nullptr,&colorRGBView),"Desktop sRGB view");
    }
    context->ClearState(); context->CopySubresourceRegion(colorYUV.Get(),0,0,0,0,texture,slice,nullptr);
    float parameters[8]={float(source.Width),float(source.Height),float(bitDepth==10),0,0,0,float(fw),float(fh)};
    context->UpdateSubresource(colorConstants.Get(),0,nullptr,parameters,0,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(colorVertex.Get(),nullptr,0); context->PSSetShader(colorPixel.Get(),nullptr,0);
    auto cb=colorConstants.Get(); context->PSSetConstantBuffers(0,1,&cb);
    auto sampler=colorSampler.Get(); context->PSSetSamplers(0,1,&sampler);
    ID3D11ShaderResourceView* planes[2]={colorLuma.Get(),colorChroma.Get()}; context->PSSetShaderResources(0,2,planes);
    auto target=colorTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
    D3D11_VIEWPORT viewport{0,0,float(fw),float(fh),0,1}; context->RSSetViewports(1,&viewport); context->Draw(3,0); context->ClearState();
    auto& s=geometry.source; auto& d=geometry.destination;
    sourceWidth=fw; sourceHeight=fh; sourceRect={s.left,s.top,s.right,s.bottom}; destination={d.left,d.top,d.right,d.bottom};
    updatePointerGeometryLocked();
    bool footprint=double(s.right-s.left)/(d.right-d.left)<=4 && double(s.bottom-s.top)/(d.bottom-d.top)<=4;
    scalingActive=!pixelExact && footprint && initScaling(fw,fh,UINT(d.right-d.left),UINT(s.bottom-s.top));
    ComPtr<ID3D11Texture2D> back; check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"Desktop back buffer");
    if(scalingActive) scaleToBackBuffer(back.Get(),geometry,colorRGBView.Get());
    else {
        ComPtr<ID3D11RenderTargetView> output; check(device_->CreateRenderTargetView(back.Get(),nullptr,&output),"Desktop output target");
        const float black[4]={0,0,0,1}; context->ClearRenderTargetView(output.Get(),black);
        float crop[8]={float(fw),float(fh),0,0,float(s.left),float(s.top),float(s.right-s.left),float(s.bottom-s.top)};
        context->UpdateSubresource(colorConstants.Get(),0,nullptr,crop,0,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); context->VSSetShader(colorVertex.Get(),nullptr,0); context->PSSetShader(colorBlitPixel.Get(),nullptr,0);
        context->PSSetConstantBuffers(0,1,&cb); context->PSSetSamplers(0,1,&sampler);
        auto view=colorRGBView.Get(); context->PSSetShaderResources(2,1,&view);
        auto render=output.Get(); context->OMSetRenderTargets(1,&render,nullptr);
        viewport={float(d.left),float(d.top),float(d.right-d.left),float(d.bottom-d.top),0,1}; context->RSSetViewports(1,&viewport); context->Draw(3,0); context->ClearState();
    }
    presentBackBuffer();
}
