#pragma once
#include "common.hpp"
#include <wincodec.h>
namespace td {
struct PNGImage { UINT width=0,height=0; Bytes pixels; };
inline PNGImage decodePNG(const Bytes& bytes,bool premultiplied=true,UINT maxSide=8192,size_t maxPixels=16*1024*1024) {
    if(bytes.size()<8 || std::memcmp(bytes.data(),"\x89PNG\r\n\x1a\n",8)!=0 || bytes.size()>32*1024*1024) throw std::runtime_error("Invalid PNG image");
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
    ComPtr<IWICStream> stream;check(factory->CreateStream(&stream),"WIC stream");
    check(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),DWORD(bytes.size())),"WIC image bytes");
    ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder),"PNG decoder");
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame),"PNG frame");
    PNGImage image; check(frame->GetSize(&image.width,&image.height),"PNG dimensions");
    if(!image.width || !image.height || image.width>maxSide || image.height>maxSide || size_t(image.width)*image.height>maxPixels) throw std::runtime_error("PNG exceeds decoded pixel limit");
    ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter),"PNG converter");
    check(converter->Initialize(frame.Get(),premultiplied?GUID_WICPixelFormat32bppPBGRA:GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"PNG format");
    image.pixels.resize(size_t(image.width)*image.height*4);
    check(converter->CopyPixels(nullptr,image.width*4,UINT(image.pixels.size()),image.pixels.data()),"PNG pixels");return image;
}
inline Bytes encodePNG(const PNGImage& image) {
    if(!image.width || !image.height || size_t(image.width)*image.height>16*1024*1024 || image.pixels.size()!=size_t(image.width)*image.height*4) return {};
    ComPtr<IWICImagingFactory> factory;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
    ComPtr<IStream> stream;check(CreateStreamOnHGlobal(nullptr,TRUE,&stream),"PNG output stream");
    ComPtr<IWICBitmapEncoder> encoder;check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"PNG encoder");check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"PNG initialize");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;
    check(encoder->CreateNewFrame(&frame,&properties),"PNG output frame");check(frame->Initialize(properties.Get()),"PNG frame initialize");
    check(frame->SetSize(image.width,image.height),"PNG output size");auto format=GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format),"PNG output format");
    if(!IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)) throw std::runtime_error("Unexpected PNG encoder format");
    check(frame->WritePixels(image.height,image.width*4,UINT(image.pixels.size()),const_cast<BYTE*>(image.pixels.data())),"PNG output pixels");check(frame->Commit(),"PNG frame commit");check(encoder->Commit(),"PNG commit");
    STATSTG stat{};check(stream->Stat(&stat,STATFLAG_NONAME),"PNG size");if(stat.cbSize.QuadPart>32*1024*1024) return {};
    HGLOBAL memory=nullptr;check(GetHGlobalFromStream(stream.Get(),&memory),"PNG memory");auto data=static_cast<BYTE*>(GlobalLock(memory));
    if(!data) throw std::runtime_error("PNG output lock failed");Bytes result(data,data+stat.cbSize.QuadPart);GlobalUnlock(memory);return result;
}
}
