#include "clipboard.hpp"
#include <cstdlib>
#include <iostream>
#define REQUIRE(expr) do { if(!(expr)) { std::cerr<<__LINE__<<": "<<#expr<<"\n"; std::exit(1); } } while(0)
int main() {
    using namespace td;
    for(const auto& text:std::vector<std::string>{"",u8"Windows → Mac\r\n中文🐱",std::string(65536,'a'),std::string(3071,'x')+u8"中文"}) {
        auto packets=clipboardPackets(text,7); REQUIRE(!packets.empty());
        ClipboardAssembler assembler; std::optional<std::string> result;
        for(auto& p:packets) { REQUIRE(p.size()<=ControlLimit); result=assembler.push(p); }
        REQUIRE(result && *result==text);
    }
    for(const auto& invalid:std::vector<std::string>{std::string(65537,'a'),std::string("a\0b",3),std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2)})
        REQUIRE(clipboardPackets(invalid,1).empty());
    auto golden=clipboardPackets(u8"中",0x01020304);
    REQUIRE(golden.front()==Bytes({12,1,2,3,4,0,0,0,3,0,0,0,0,0xe4,0xb8,0xad}));
    auto large=clipboardPackets(std::string(4000,'x'),2);
    for(auto invalid:std::vector<Bytes>{Bytes{12},large.back()}) {
        ClipboardAssembler a; bool rejected=false; try { a.push(invalid); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    auto invalid=large.front(); invalid[5]=1;
    ClipboardAssembler a; bool rejected=false; try { a.push(invalid); } catch(...) { rejected=true; } REQUIRE(rejected);
    ClipboardAssembler superseded; REQUIRE(!superseded.push(large.front()));
    REQUIRE(superseded.push(clipboardPackets("new copy",3).front())=="new copy");
    auto badUTF8=golden.front(); badUTF8.back()=0xff;
    ClipboardAssembler b; rejected=false; try { b.push(badUTF8); } catch(...) { rejected=true; } REQUIRE(rejected);
    std::cout<<"Bounded UTF-8 clipboard protocol tests passed\n";
}
