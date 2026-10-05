#pragma once
#include "png.hpp"
namespace td {
inline UINT pngClipboardFormat() { static const auto format=RegisterClipboardFormatW(L"PNG");return format; }
// Caller holds the native clipboard lock. Prefer lossless PNG, then native DIB.
inline Bytes readClipboardPNG() {
    auto format=pngClipboardFormat();
    if(IsClipboardFormatAvailable(format)) {
        auto memory=GetClipboardData(format);auto size=GlobalSize(memory);
        if(size>=8 && size<=32*1024*1024) {
            auto data=static_cast<BYTE*>(GlobalLock(memory));
            if(data) { Bytes png(data,data+size);GlobalUnlock(memory);decodePNG(png,false);return png; }
        }
    }
    for(auto kind:{CF_DIBV5,CF_DIB}) {
        if(!IsClipboardFormatAvailable(kind)) continue;
        auto memory=GetClipboardData(kind);auto size=GlobalSize(memory);
        if(size<sizeof(BITMAPINFOHEADER) || size>80*1024*1024) continue;
        auto data=static_cast<BYTE*>(GlobalLock(memory));if(!data) continue;
        BITMAPINFOHEADER header{};std::memcpy(&header,data,sizeof(header));
        auto height=std::abs(int64_t(header.biHeight));
        size_t offset=header.biSize;
        if(header.biSize==40 && header.biCompression==BI_BITFIELDS) offset+=12;
        if(header.biClrUsed) offset+=size_t(header.biClrUsed)*sizeof(RGBQUAD);
        bool valid=header.biSize>=40 && header.biWidth>0 && header.biWidth<=8192 && height>0 && height<=8192 &&
            uint64_t(header.biWidth)*height<=16*1024*1024 && header.biPlanes==1 &&
            (header.biBitCount==24 || header.biBitCount==32) && (header.biCompression==BI_RGB || header.biCompression==BI_BITFIELDS);
        size_t stride=valid?((size_t(header.biWidth)*header.biBitCount+31)/32)*4:0;
        valid=valid && offset<=size && stride*height<=size-offset;
        if(!valid) { GlobalUnlock(memory);continue; }
        // Only accept standard RGB bitfields; don't silently reinterpret color.
        bool hasAlpha=false;
        if(header.biCompression==BI_BITFIELDS) {
            DWORD masks[4]{};size_t maskOffset=40;
            if(size<maskOffset+12) { GlobalUnlock(memory);continue; }
            std::memcpy(masks,data+maskOffset,12);
            if(masks[0]!=0x00ff0000 || masks[1]!=0x0000ff00 || masks[2]!=0x000000ff) { GlobalUnlock(memory);continue; }
            if(header.biSize>=108) { std::memcpy(&masks[3],data+52,4);hasAlpha=header.biBitCount==32 && masks[3]==0xff000000; }
        }
        PNGImage image{UINT(header.biWidth),UINT(height),Bytes(size_t(header.biWidth)*height*4)};
        for(UINT y=0;y<image.height;++y) for(UINT x=0;x<image.width;++x) {
            auto src=data+offset+(header.biHeight<0?y:image.height-y-1)*stride+x*(header.biBitCount/8);
            auto dest=image.pixels.data()+(size_t(y)*image.width+x)*4;
            dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=hasAlpha?src[3]:255;
        }
        GlobalUnlock(memory);return encodePNG(image);
    }
    if(IsClipboardFormatAvailable(CF_BITMAP)) {
        auto bitmap=static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));BITMAP description{};
        if(!GetObjectW(bitmap,sizeof(description),&description) || description.bmWidth<=0 || description.bmHeight<=0 || description.bmWidth>8192 || description.bmHeight>8192 ||
            uint64_t(description.bmWidth)*description.bmHeight>16*1024*1024) return {};
        PNGImage image{UINT(description.bmWidth),UINT(description.bmHeight),Bytes(size_t(description.bmWidth)*description.bmHeight*4)};
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LONG(image.width);info.bmiHeader.biHeight=-LONG(image.height);
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        auto dc=GetDC(nullptr);int rows=GetDIBits(dc,bitmap,0,image.height,image.pixels.data(),&info,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);
        if(rows!=int(image.height)) return {};
        for(size_t i=3;i<image.pixels.size();i+=4) image.pixels[i]=255;
        return encodePNG(image);
    }
    return {};
}
inline HGLOBAL clipboardMemory(const void* bytes,size_t size) {
    auto memory=GlobalAlloc(GMEM_MOVEABLE,size);if(!memory) return nullptr;
    auto data=GlobalLock(memory);if(!data) { GlobalFree(memory);return nullptr; }
    std::memcpy(data,bytes,size);GlobalUnlock(memory);return memory;
}
inline bool writeClipboardPNG(HWND window,const Bytes& png,const PNGImage& image) {
    BITMAPV5HEADER header{};header.bV5Size=sizeof(header);header.bV5Width=LONG(image.width);header.bV5Height=-LONG(image.height);
    header.bV5Planes=1;header.bV5BitCount=32;header.bV5Compression=BI_BITFIELDS;header.bV5SizeImage=DWORD(image.pixels.size());
    header.bV5RedMask=0x00ff0000;header.bV5GreenMask=0x0000ff00;header.bV5BlueMask=0x000000ff;header.bV5AlphaMask=0xff000000;header.bV5CSType=LCS_sRGB;
    Bytes dib(sizeof(header)+image.pixels.size());std::memcpy(dib.data(),&header,sizeof(header));std::memcpy(dib.data()+sizeof(header),image.pixels.data(),image.pixels.size());
    auto pngMemory=clipboardMemory(png.data(),png.size()),dibMemory=clipboardMemory(dib.data(),dib.size());
    if(!pngMemory || !dibMemory || !OpenClipboard(window)) { if(pngMemory) GlobalFree(pngMemory);if(dibMemory) GlobalFree(dibMemory);return false; }
    bool success=false;
    if(EmptyClipboard()) {
        if(SetClipboardData(pngClipboardFormat(),pngMemory)) { pngMemory=nullptr;success=true; }
        if(SetClipboardData(CF_DIBV5,dibMemory)) { dibMemory=nullptr;success=true; }
    }
    CloseClipboard();if(pngMemory) GlobalFree(pngMemory);if(dibMemory) GlobalFree(dibMemory);return success;
}
}
