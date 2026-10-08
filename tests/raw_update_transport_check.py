#!/usr/bin/env python3
"""Production Swift sender -> production C++ reconstruction at paced 4 Gbps.

Only synthetic pixels and loopback are used. A dense startup intentionally
replaces captured frames before sparse 60/120/240 Hz updates verify every pixel.
This does not measure the Thunderbolt adapter or Windows GPU.
"""
from pathlib import Path
import os
import subprocess
from swift_wire_build import compile_wire

ROOT = Path(__file__).resolve().parents[1]
folder = ROOT / 'build/tests/raw-update-transport'
folder.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
if Path('/Applications/Xcode.app/Contents/Developer').is_dir():
    env.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
compiler = ['/usr/bin/xcrun', 'swiftc', '-O', '-module-cache-path', str(folder / 'module-cache')]
compiler = compile_wire(compiler, folder, env)
(folder / 'main.swift').write_text(r'''
import Darwin
import Foundation
import Wire
struct HostError: Error { let message: String; init(_ message: String) { self.message=message } }
func log(_ message: String) { print(message); fflush(stdout) }
let receiver=CommandLine.arguments[1],fps=Int(CommandLine.arguments[2])!
let count=try RawVideoWire.byteCount(width:4096,height:2560,packed:true)
let sender=try RawVideoSender(ip:"127.0.0.1",peerIP:inet_addr("127.0.0.1"),session:0x0102030405060708,width:4096,height:2560,packed:true,updates:true) { log("failure=\($0)") }
let child=Process(); child.executableURL=URL(fileURLWithPath:receiver); child.arguments=[String(sender.port),String(fps)]; try child.run()
let queue=DispatchQueue(label:"test.synthetic-pixels",qos:.userInteractive),timer=DispatchSource.makeTimerSource(queue:queue)
let began=DispatchTime.now().uptimeNanoseconds
var id:UInt32=0
timer.schedule(deadline:.now(),repeating:.nanoseconds(1_000_000_000/fps),leeway:.microseconds(100))
timer.setEventHandler {
    id += 1
    let dense=DispatchTime.now().uptimeNanoseconds-began<400_000_000
    var image=Data(repeating:dense ? UInt8(truncatingIfNeeded:id) : 0,count:count)
    image[0]=UInt8(truncatingIfNeeded:id); image[count-1]=UInt8(truncatingIfNeeded:id)
    _=sender.enqueue(OutgoingVideoFrame(data:image,pts:dense ? 1 : 0,id:id,key:true))
}
timer.resume(); RunLoop.current.run(until:Date().addingTimeInterval(4)); timer.cancel(); queue.sync {}
RunLoop.current.run(until:Date().addingTimeInterval(0.2)); sender.stop(); child.waitUntilExit()
exit(child.terminationStatus)
''')
subprocess.run(compiler + ['-I', str(folder), '-L', str(folder), '-lWire', '-Xlinker', '-rpath', '-Xlinker', str(folder),
    str(folder / 'main.swift'), str(ROOT / 'mac-host/Sources/ThunderDisplayHost/RawVideoSender.swift'),
    '-o', str(folder / 'sender')], env=env, check=True)
subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'shared'),
    str(ROOT / 'tests/raw_update_transport_receiver.cpp'), '-o', str(folder / 'receiver')], check=True)
for fps in (60, 120, 240):
    print(f'Target {fps} synthetic fps, paced loopback only', flush=True)
    subprocess.run([str(folder / 'sender'), str(folder / 'receiver'), str(fps)], env=env, check=True, timeout=12)
