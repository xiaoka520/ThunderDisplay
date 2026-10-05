#include "presentation.hpp"
#include <cstdlib>
#include <iostream>
#define REQUIRE(expr) do { if(!(expr)) { std::cerr<<__LINE__<<": "<<#expr<<"\n"; std::exit(1); } } while(0)
int main() {
    td::PresentationGeometry fit(2560,1600,1280,800,false);
    REQUIRE(fit.scale==0.5 && fit.source.right==2560 && fit.destination.right==1280);
    td::PresentationGeometry native(2560,1600,1280,800,true);
    REQUIRE(native.scale==1 && native.source.left==640 && native.source.top==400);
    REQUIRE(native.destination.right==1280 && native.source.right==1920);
    int x=0,y=0; REQUIRE(native.pointer(0,0,false,2560,1600,x,y));
    REQUIRE(x>16380 && x<16400 && y>16380 && y<16410);
    REQUIRE(native.pointer(1279,799,false,2560,1600,x,y)); REQUIRE(x<49160 && x>49130);
    REQUIRE(!native.pointer(-1,1,false,2560,1600,x,y));
    REQUIRE(native.pointer(-1,1,true,2560,1600,x,y)); REQUIRE(x>16380);
    td::PresentationGeometry full(2560,1600,2560,1600,true);
    REQUIRE(full.source.left==0 && full.destination.left==0);
    REQUIRE(full.pointer(2559,1599,false,2560,1600,x,y)); REQUIRE(x==65535 && y==65535);
    td::PresentationGeometry letterbox(1920,1080,2560,1600,false);
    REQUIRE(letterbox.destination.top==80); REQUIRE(!letterbox.pointer(10,10,false,1920,1080,x,y));
    std::cout<<"Pixel-exact, fit, letterbox and cropped pointer mapping tests passed\n";
}
