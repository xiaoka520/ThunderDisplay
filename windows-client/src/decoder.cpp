#include "decoder.hpp"
#include <codecapi.h>
#include <icodecapi.h>

Decoder::Decoder(Renderer& r,const td::Welcome& w,bool srgb):renderer(r),welcome(w),desktopSRGB(srgb) {
    UINT reset; check(MFCreateDXGIDeviceManager(&reset,&manager),"DXGI device manager");
    check(manager->ResetDevice(renderer.device(),reset),"Set decode GPU");
    MFT_REGISTER_TYPE_INFO type={MFMediaType_Video,w.codec==td::Codec::H264?MFVideoFormat_H264:MFVideoFormat_HEVC};
    IMFActivate** candidates=nullptr; UINT count=0;
    check(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_ASYNCMFT|MFT_ENUM_FLAG_HARDWARE|
        MFT_ENUM_FLAG_LOCALMFT|MFT_ENUM_FLAG_SORTANDFILTER,&type,nullptr,&candidates,&count),"Enumerate video decoders");
    std::string failure="No compatible D3D11 decoder installed";
    for(UINT i=0;i<count && !transform;++i) {
        ComPtr<IMFTransform> candidate;
        try {
            check(candidates[i]->ActivateObject(IID_PPV_ARGS(&candidate)),"Activate decoder");
            configure(candidate.Get()); transform=candidate;
            outputType();
            check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0),"Begin decoder streaming");
            check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0),"Start decoder stream"); started=true;
        } catch(const std::exception& e) {
            failure=e.what(); candidates[i]->ShutdownObject(); events.Reset(); transform.Reset(); started=false;
        }
    }
    for(UINT i=0;i<count;++i) candidates[i]->Release(); CoTaskMemFree(candidates);
    if(!transform) throw std::runtime_error(failure);
}
void Decoder::configure(IMFTransform* mft) {
    inputID=outputID=0; credits=0; async=false;
    ComPtr<IMFAttributes> a; check(mft->GetAttributes(&a),"Decoder attributes");
    async=MFGetAttributeUINT32(a.Get(),MF_TRANSFORM_ASYNC,FALSE)!=0;
    if(async) { check(a->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK,TRUE),"Unlock async decoder"); check(mft->QueryInterface(IID_PPV_ARGS(&events)),"Async decoder events"); }
    if(!MFGetAttributeUINT32(a.Get(),MF_SA_D3D11_AWARE,FALSE)) throw std::runtime_error("Decoder lacks D3D11 hardware support");
    a->SetUINT32(MF_LOW_LATENCY,TRUE);
    ComPtr<ICodecAPI> api;
    if(SUCCEEDED(mft->QueryInterface(IID_PPV_ARGS(&api)))) {
        VARIANT v; VariantInit(&v); v.vt=VT_BOOL; v.boolVal=VARIANT_TRUE; api->SetValue(&CODECAPI_AVLowLatencyMode,&v);
    }
    check(mft->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,reinterpret_cast<ULONG_PTR>(manager.Get())),"Bind decoder to D3D11 GPU");
    DWORD inputs,outputs; check(mft->GetStreamCount(&inputs,&outputs),"Decoder stream count");
    if(inputs!=1 || outputs!=1) throw std::runtime_error("Unexpected decoder streams");
    auto ids=mft->GetStreamIDs(1,&inputID,1,&outputID); if(ids!=E_NOTIMPL) check(ids,"Decoder stream IDs");
    ComPtr<IMFMediaType> input; check(MFCreateMediaType(&input),"Input media type");
    check(input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Video major type");
    check(input->SetGUID(MF_MT_SUBTYPE,welcome.codec==td::Codec::H264?MFVideoFormat_H264:MFVideoFormat_HEVC),"Codec subtype");
    check(MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,welcome.settings.width,welcome.settings.height),"Input size");
    check(MFSetAttributeRatio(input.Get(),MF_MT_FRAME_RATE,welcome.settings.fps,1),"Input rate");
    check(MFSetAttributeRatio(input.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1),"Pixel aspect");
    if(welcome.codec!=td::Codec::H264) check(input->SetUINT32(MF_MT_MPEG2_PROFILE,welcome.codec==td::Codec::HEVC10?eAVEncH265VProfile_Main_420_10:eAVEncH265VProfile_Main_420_8),"Set HEVC input profile");
    input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
    input->SetUINT32(MF_MT_VIDEO_PRIMARIES,MFVideoPrimaries_BT709);
    input->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT709);
    input->SetUINT32(MF_MT_TRANSFER_FUNCTION,desktopSRGB?MFVideoTransFunc_sRGB:MFVideoTransFunc_709);
    input->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235);
    check(mft->SetInputType(inputID,input.Get(),0),"Set compressed input type");
}
void Decoder::outputType() {
    for(DWORD i=0;;++i) {
        ComPtr<IMFMediaType> type;
        auto hr=transform->GetOutputAvailableType(outputID,i,&type);
        if(hr==MF_E_TRANSFORM_TYPE_NOT_SET) return; // SPS in the first IDR can provide the type.
        if(hr==MF_E_NO_MORE_TYPES) break;
        check(hr,"Enumerate decoded output types");
        GUID subtype; check(type->GetGUID(MF_MT_SUBTYPE,&subtype),"Output subtype");
        if(subtype!=(welcome.codec==td::Codec::HEVC10?MFVideoFormat_P010:MFVideoFormat_NV12)) continue;
        check(transform->SetOutputType(outputID,type.Get(),0),"Set negotiated GPU output precision");
        outputWidth=welcome.settings.width; outputHeight=welcome.settings.height;
        return;
    }
    throw std::runtime_error(welcome.codec==td::Codec::HEVC10?"Decoder exposes no 10-bit P010 output":"Decoder exposes no NV12 output");
}
void Decoder::input(const td::Frame& frame) {
    ComPtr<IMFSample> sample; check(MFCreateSample(&sample),"Create compressed sample");
    ComPtr<IMFMediaBuffer> buffer; check(MFCreateMemoryBuffer(DWORD(frame.data.size()),&buffer),"Create compressed buffer");
    BYTE* data; check(buffer->Lock(&data,nullptr,nullptr),"Lock compressed buffer");
    std::memcpy(data,frame.data.data(),frame.data.size()); check(buffer->Unlock(),"Unlock compressed buffer");
    check(buffer->SetCurrentLength(DWORD(frame.data.size())),"Compressed length"); check(sample->AddBuffer(buffer.Get()),"Attach compressed buffer");
    check(sample->SetSampleTime(LONGLONG(frame.pts)*10),"Compressed timestamp");
    check(sample->SetSampleDuration(10000000/welcome.settings.fps),"Compressed duration");
    if(frame.key) sample->SetUINT32(MFSampleExtension_CleanPoint,TRUE);
    if(discontinuity) sample->SetUINT32(MFSampleExtension_Discontinuity,TRUE);
    auto hr=transform->ProcessInput(inputID,sample.Get(),0);
    if(hr==MF_E_NOTACCEPTING && !async) { while(output()) {} hr=transform->ProcessInput(inputID,sample.Get(),0); }
    check(hr,"Submit compressed frame");
    ++inputs;
    discontinuity=false;
}
bool Decoder::output() {
    for(unsigned changes=0;changes<4;++changes) {
    MFT_OUTPUT_STREAM_INFO info{};
    check(transform->GetOutputStreamInfo(outputID,&info),"Output allocation info");
    if(!(info.dwFlags&(MFT_OUTPUT_STREAM_PROVIDES_SAMPLES|MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES))) {
        // The native D3D11 decoder should allocate its own GPU samples.
        throw std::runtime_error("Decoder requires CPU output allocation; D3D11 output is unavailable");
    }
    MFT_OUTPUT_DATA_BUFFER data{}; data.dwStreamID=outputID;
    DWORD status=0; auto hr=transform->ProcessOutput(0,1,&data,&status);
    ComPtr<IMFSample> sample; sample.Attach(data.pSample);
    if(data.pEvents) data.pEvents->Release();
    if(hr==MF_E_TRANSFORM_STREAM_CHANGE) { outputType(); continue; }
    if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT) return false;
    check(hr,"Decode output");
    if(sample) {
        ++outputs; latestOutput=std::move(sample); latestWidth=outputWidth; latestHeight=outputHeight;
    }
    return true;
    }
    throw std::runtime_error("Repeated decoder output type changes");
}
void Decoder::presentLatest() {
    if(!latestOutput) return;
    // Consume every codec output to preserve reference dependencies, but do not
    // wait for Present once per obsolete output during a decoder event burst.
    auto sample=std::move(latestOutput);
    renderer.present(sample.Get(),latestWidth,latestHeight,welcome.settings.fps);
}
bool Decoder::submit(td::Frame frame) {
    if(!async) { input(frame); while(output()) {} presentLatest(); return true; }
    // Consume newly available input credits before judging the queue as stalled.
    pump();
    if(pending.size()>=2) { flush(); return false; }
    pending.push_back(std::move(frame)); return pump();
}
bool Decoder::pump() {
    if(!async) return true;
    for(unsigned i=0;i<64;++i) {
        ComPtr<IMFMediaEvent> event; auto hr=events->GetEvent(MF_EVENT_FLAG_NO_WAIT,&event);
        if(hr==MF_E_NO_EVENTS_AVAILABLE) break;
        check(hr,"Poll decoder event");
        HRESULT result; check(event->GetStatus(&result),"Decoder event status"); check(result,"Async decoder");
        MediaEventType type; check(event->GetType(&type),"Decoder event type");
        if(type==METransformNeedInput) ++credits;
        if(type==METransformHaveOutput) output();
    }
    while(credits && !pending.empty()) { input(pending.front()); pending.pop_front(); --credits; }
    presentLatest();
    return true;
}
void Decoder::flush() {
    pending.clear(); latestOutput.Reset(); credits=0; discontinuity=true;
    check(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH,0),"Flush decoder");
    if(async) {
        ComPtr<IMFMediaEvent> event;
        while(SUCCEEDED(events->GetEvent(MF_EVENT_FLAG_NO_WAIT,&event))) event.Reset();
    }
    check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0),"Restart decoder stream");
}
Decoder::~Decoder() {
    if(transform && started) { transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,inputID); transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING,0); }
    if(async && transform) { ComPtr<IMFShutdown> shutdown; if(SUCCEEDED(transform.As(&shutdown))) shutdown->Shutdown(); }
}
