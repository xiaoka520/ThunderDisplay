#include "setup_layout.hpp"
#include "setup_scroll.hpp"
#include "cursor.hpp"
#include <cstdlib>
#include <iostream>
#define REQUIRE(expr) do { if(!(expr)) { std::cerr<<__LINE__<<": "<<#expr<<"\n"; std::exit(1); } } while(0)
int main() {
    for(int width:{628,686,1200,1800}) for(int height:{350,960,1600}) for(bool paired:{false,true}) for(bool manual:{false,true}) {
        td::SetupLayout layout{width,height,paired,manual};
        REQUIRE(200+layout.fieldWidth()==width-48);
        REQUIRE(layout.fieldWidth()-154>=220);
        REQUIRE(layout.contentHeight()>=height);
        REQUIRE(layout.qualityTop()==layout.networkTop()+layout.networkHeight()+20);
        REQUIRE(layout.diagnosticsTop()==layout.qualityTop()+layout.qualityHeight()+20);
        REQUIRE(layout.shortcutsTop()+60<=layout.contentHeight());
        for(auto count:{2,3}) {
            int previousRight=0;
            for(int index=0;index<count;++index) {
                auto cell=layout.column(index,count,18);
                REQUIRE(cell.width>0 && cell.x>=48 && cell.x+cell.width<=width-48);
                if(index) REQUIRE(cell.x-previousRight==18);
                previousRight=cell.x+cell.width;
                if(count==3) REQUIRE(cell.width-65>=90);
            }
        }
    }
    td::SetupLayout compact{840,960,false,false},expanded{840,960,true,true};
    REQUIRE(expanded.minimumHeight()>compact.minimumHeight());

    // Resizing the log must extend its card/footer and page range together,
    // without moving the other settings or resetting the document viewport.
    REQUIRE(compact.logHeight()==220);
    for(int preferred:{-1,0,120,220,600,960,100000}) for(int height:{350,960,2600}) {
        td::SetupLayout layout{840,height,false,false,preferred};
        REQUIRE(layout.logHeight()>=120 && layout.logHeight()<=960);
        REQUIRE(layout.logResizeTop()==layout.logTop()+layout.logHeight()+4);
        REQUIRE(layout.logResizeTop()+28+12==layout.diagnosticsTop()+layout.diagnosticsHeight());
        REQUIRE(layout.shortcutsTop()==layout.diagnosticsTop()+layout.diagnosticsHeight()+16);
        REQUIRE(layout.shortcutsTop()+60+8<=layout.contentHeight());
        REQUIRE(layout.qualityTop()==compact.qualityTop());
        REQUIRE(layout.diagnosticsTop()==compact.diagnosticsTop());
        REQUIRE(layout.contentHeight()>=height);
        td::SetupScroll document; document.resize(compact.contentHeight(),height); document.moveTo(200);
        auto before=document.position(); document.resize(layout.contentHeight(),height);
        REQUIRE(document.position()==std::min(before,std::max(0,layout.contentHeight()-height)));
    }
    REQUIRE(td::SetupLayout::boundedLogHeight(-1)==120);
    REQUIRE(td::SetupLayout::boundedLogHeight(100000)==960);

    // Reproduce a bottom field retaining focus while the user scrolls upward.
    // Ordinary wheel/key messages must never pull the page back to that field.
    td::SetupScroll scroll;
    scroll.resize(2400,700);
    scroll.reveal(2168,2200,true);
    REQUIRE(scroll.position()==1500);
    for(int step=1;step<=25;++step) {
        scroll.wheel(120,60);
        scroll.reveal(2168,2200,false);
        REQUIRE(scroll.position()==1500-step*60);
    }
    REQUIRE(scroll.position()==0);
    scroll.wheel(120,60);
    REQUIRE(scroll.position()==0);
    // Tab/Shift+Tab still reveal newly focused fields in either direction.
    scroll.reveal(2168,2200,true);
    REQUIRE(scroll.position()==1500);
    scroll.reveal(100,132,true);
    REQUIRE(scroll.position()==100);
    scroll.moveTo(100000);
    REQUIRE(scroll.position()==1700);
    scroll.reveal(100,132,false);
    REQUIRE(scroll.position()==1700);
    scroll.resize(2400,2200);
    REQUIRE(scroll.position()==200);
    scroll.resize(2400,2600);
    REQUIRE(scroll.position()==0);

    // Fine-resolution wheels preserve partial pixels at all common DPIs.
    for(int step:{60,75,90,120}) {
        scroll.resize(2400,700);
        scroll.moveTo(500);
        for(int part=0;part<120;++part) scroll.wheel(1,step);
        REQUIRE(scroll.position()==500-step);
        for(int part=0;part<120;++part) scroll.wheel(-1,step);
        REQUIRE(scroll.position()==500);
    }
    scroll.moveTo(0);
    scroll.wheel(1,60); // outward fractional input cannot delay a reversal
    scroll.wheel(-2,60);
    REQUIRE(scroll.position()==1);
    scroll.moveTo(1700);
    scroll.wheel(-1,60);
    scroll.wheel(2,60);
    REQUIRE(scroll.position()==1699);

    using Target=td::SetupWheelTarget;
    const std::vector<td::CursorDimensions> family{{23,22},{46,44},{115,110},{230,220}};
    REQUIRE(td::bestCursorRepresentation(96,23,22,family)==0);
    REQUIRE(td::bestCursorRepresentation(144,23,22,family)==1);
    REQUIRE(td::bestCursorRepresentation(192,23,22,family)==1);
    REQUIRE(td::bestCursorRepresentation(480,23,22,family)==2);
    REQUIRE(td::bestCursorRepresentation(768,23,22,family)==3);
    REQUIRE(td::bestCursorRepresentation(384,23,22,{{23,22},{46,44}})==1);
    // Selection is independent of transmission order and changes with DPI.
    REQUIRE(td::bestCursorRepresentation(192,23,22,{{230,220},{46,44},{23,22}})==1);
    REQUIRE(td::setupWheelTarget(true,true,120,10,20)==Target::Dropdown);
    REQUIRE(td::setupWheelTarget(false,false,120,10,20)==Target::Document);
    REQUIRE(td::setupWheelTarget(false,true,120,10,20)==Target::Diagnostics);
    REQUIRE(td::setupWheelTarget(false,true,-120,10,20)==Target::Diagnostics);
    REQUIRE(td::setupWheelTarget(false,true,120,0,20)==Target::Document);
    REQUIRE(td::setupWheelTarget(false,true,-120,20,20)==Target::Document);
    REQUIRE(td::setupWheelTarget(false,true,120,0,0)==Target::Document);
    for(unsigned dpi:{96u,120u,144u,192u,288u,384u,768u}) {
        auto cursor=td::macStyleCursor(dpi);
        REQUIRE(cursor.size%32==0 && cursor.pixels.size()==size_t(cursor.size)*cursor.size);
        REQUIRE(cursor.hotX>=0 && cursor.hotX<cursor.size && cursor.hotY>=0 && cursor.hotY<cursor.size);
        bool black=false,white=false,edge=false,shadow=false;
        int left=cursor.size,top=cursor.size,right=-1,bottom=-1;
        for(size_t i=0;i<cursor.pixels.size();++i) {
            auto pixel=cursor.pixels[i];
            auto alpha=pixel>>24,r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
            REQUIRE(r==g && g==b && r<=alpha);
            black|=alpha==255 && r==0; white|=alpha>=240 && r>=240; edge|=alpha>0 && alpha<255;
            shadow|=alpha>0 && alpha<64 && r==0;
            if(alpha>=64) { // Measure the arrow body; its soft native shadow is larger.
                int x=int(i%cursor.size),y=int(i/cursor.size);
                left=std::min(left,x); right=std::max(right,x);
                top=std::min(top,y); bottom=std::max(bottom,y);
            }
        }
        REQUIRE(black && white && edge && shadow);
        REQUIRE(cursor.pixels.front()==0 && cursor.pixels.back()==0);
        // Measure visible pixels, not the padded Windows cursor bitmap. The
        // former arrow was 29 logical pixels tall (58 px at 200% scaling).
        double dpiScale=dpi/96.0;
        REQUIRE(bottom-top+1>=16*dpiScale && bottom-top+1<=19*dpiScale);
        REQUIRE(right-left+1>=10*dpiScale && right-left+1<=13*dpiScale);
        REQUIRE(std::abs(cursor.hotX-left)<=2*dpiScale && std::abs(cursor.hotY-top)<=2*dpiScale);
        REQUIRE(right<cursor.size-1 && bottom<cursor.size-1);
    }
    // At the source density, visible pixels and native click location must be
    // identical to AppKit's export, including the system outline and shadow.
    auto native=td::macStyleCursor(96*td::mac_arrow::Density);
    const auto& original=td::macArrowPixels();
    REQUIRE(original.size()==size_t(td::mac_arrow::Width)*td::mac_arrow::Height);
    REQUIRE(native.hotX==td::mac_arrow::HotX && native.hotY==td::mac_arrow::HotY);
    for(int y=0;y<td::mac_arrow::Height;++y) for(int x=0;x<td::mac_arrow::Width;++x)
        REQUIRE(native.pixels[size_t(y)*native.size+x]==original[size_t(y)*td::mac_arrow::Width+x]);
    // Native 2x/5x/10x rasters must reduce to exact hard edges, without the
    // former second bilinear blur. Click location remains in logical points.
    for(int density:{2,5,10}) {
        int width=2*density,height=density;
        std::vector<uint32_t> source(size_t(width)*height);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x)
            source[size_t(y)*width+x]=x<density?0xff000000:0xffffffff;
        auto raster=td::rasterCursor(96,width,height,2,1,1,0,source);
        REQUIRE(raster.pixels[0]==0xff000000 && raster.pixels[1]==0xffffffff);
        REQUIRE(raster.pixels[2]==0 && raster.pixels[raster.size]==0);
        REQUIRE(raster.hotX==1 && raster.hotY==0);
        if(density<=8) { // Windows cursor DPI is capped at 800%.
            auto identity=td::rasterCursor(96*density,width,height,2,1,1,0,source);
            for(int y=0;y<height;++y) for(int x=0;x<width;++x)
                REQUIRE(identity.pixels[size_t(y)*identity.size+x]==source[size_t(y)*width+x]);
        }
    }
    // Fractional monitor DPI preserves coverage and premultiplied alpha;
    // transparent pixels must not introduce dark fringes.
    std::vector<uint32_t> translucent(16,0x80808080);
    auto fractional=td::rasterCursor(144,4,4,2,2,1,1,translucent);
    for(int y=0;y<3;++y) for(int x=0;x<3;++x) REQUIRE(fractional.pixels[size_t(y)*fractional.size+x]==0x80808080);
    REQUIRE(fractional.hotX==2 && fractional.hotY==2);
    std::cout<<"Responsive layout, settings scroll and cursor DPI regression tests passed\n";
}
