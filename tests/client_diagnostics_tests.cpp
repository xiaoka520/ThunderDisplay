#include "client_diagnostics.hpp"
#include "control_outbox.hpp"
#include <cassert>
#include <iostream>
#include <thread>
int main() {
    td::RemoteDiagnostics logs;
    logs.push("video.performance","disabled"); assert(logs.take().empty());
    logs.setEnabled(true);
    logs.push("input.mouse","private input"); logs.push("clipboard.text","private clipboard"); logs.push("connection.attempt","private code");
    assert(logs.take().empty());
    logs.push("video.performance","fps=60\n\x01secret");
    assert(logs.take()=="fps=60  secret\n");
    for(int i=0;i<10000;++i) logs.push("display.latency",std::string(2000,'x'));
    auto batch=logs.take(); assert(batch.size()<=4000 && batch.find("dropped=9936")!=std::string::npos);
    logs.setEnabled(false); assert(logs.take().empty());
    logs.setEnabled(true); assert(logs.take().empty()); // No disk/history export after enabling.
    std::thread producer([&]{for(int i=0;i<10000;++i) logs.push("video.raw","fps=60");});
    for(int i=0;i<200;++i) { logs.setEnabled(i%2); assert(logs.take().size()<=4000); }
    producer.join(); logs.setEnabled(false); logs.setEnabled(true); assert(logs.take().empty());
    auto start=td::clientDiagnostics(7,1);
    assert(start==td::Bytes({23,1,0,0,0,0,0,0,0,7,1}));
    auto payload=td::clientDiagnostics(7,2,"video.performance fps=60\n");
    assert(payload.size()==36 && payload[10]==2);
    bool rejected=false; try { td::clientDiagnostics(7,0,"text"); } catch(...) { rejected=true; } assert(rejected);
    rejected=false; try { td::clientDiagnostics(7,2,std::string(4001,'x')); } catch(...) { rejected=true; } assert(rejected);
    td::ControlOutbox outbox; outbox.allowInput(true);
    outbox.push(payload); outbox.push(td::clientDiagnostics(7,2,"newest\n")); outbox.push(td::input(3,65,1));
    assert(outbox.take()->message==td::input(3,65,1));
    assert(outbox.take()->message==td::clientDiagnostics(7,2,"newest\n"));
    outbox.push(payload); outbox.push(td::clientDiagnostics(7,0));
    assert(outbox.take()->message==td::clientDiagnostics(7,0)); assert(!outbox.take());
    outbox.push(payload); outbox.transition(td::sessionTransition(7,true));
    assert(outbox.take()->message==td::sessionTransition(7,true)); assert(!outbox.take());
    td::PerformanceWindow window; window.reset(100,{});
    auto values=window.sample(2000100,{120,1000000000,100,90,5,7,2},50000,60,true,-1);
    assert(values.find("receive_fps=60.00")!=std::string::npos);
    assert(values.find("upload_fps=50.00")!=std::string::npos && values.find("present_fps=45.00")!=std::string::npos);
    assert(values.find("receive_Gbps=4.00")!=std::string::npos && values.find("dxgi_display_fps=-1.00")!=std::string::npos);
    assert(values.find("busy_dropped=5")!=std::string::npos && values.find("late_intervals=2")!=std::string::npos);
    values=window.sample(3000100,{},0,60,false,-1); // Reset counters never wrap into huge FPS.
    assert(values.find("present_fps=0.00")!=std::string::npos);
    std::cout<<"Opt-in remote diagnostics, input priority and measured frame windows passed\n";
}
