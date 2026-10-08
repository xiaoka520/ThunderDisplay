#!/usr/bin/env python3
"""Exercise the real HostServer sockets with synthetic capture and no CGEvent injection."""
from pathlib import Path
import os
import platform
import socket
import struct
import subprocess
import tempfile
import time
from swift_wire_build import compile_wire

ROOT = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
with tempfile.TemporaryDirectory(prefix='td-udp-check-') as directory:
    folder = Path(directory)
    compiler = ['xcrun', 'swiftc', '-target', f'{platform.machine()}-apple-macosx13.0',
        '-module-cache-path', str(folder / 'module-cache')]
    compiler = compile_wire(compiler, folder, env)
    (folder / 'main.swift').write_text('''
import AppKit
import Wire
// Exercise the production sender with a stalled transport: control stays free
// and cancellation releases the duplicated UDP port before the send completes.
func checkSenderIsolation() throws {
    let fd=socket(AF_INET,SOCK_DGRAM,0); precondition(fd>=0)
    try nonblocking(fd)
    var endpoint=try address("127.0.0.1",port:0)
    precondition(withAddress(&endpoint) { bind(fd,$0,$1) } == 0)
    var length=socklen_t(MemoryLayout<sockaddr_in>.size)
    precondition(withUnsafeMutablePointer(to:&endpoint) { p in p.withMemoryRebound(to:sockaddr.self,capacity:1) { getsockname(fd,$0,&length) } } == 0)
    let entered=DispatchSemaphore(value:0), release=DispatchSemaphore(value:0), finished=DispatchSemaphore(value:0)
    let sender=try VideoSender(socket:fd,session:1,codec:.hevc,target:endpoint,fps:60,emit:{ _,cancelled in
        entered.signal(); release.wait(); precondition(cancelled()); finished.signal(); return false
    },onFailure:{ fatalError("Cancelled sends must not request old-session IDRs") })
    precondition(sender.enqueue(OutgoingVideoFrame(data:Data([1]),pts:0,id:1,key:true)))
    precondition(entered.wait(timeout:.now()+1) == .success)
    let control=DispatchQueue(label:"sender-test-control"), responsive=DispatchSemaphore(value:0)
    control.async {
        precondition(sender.enqueue(OutgoingVideoFrame(data:Data([2]),pts:1,id:2,key:false)))
        sender.stop(); responsive.signal()
    }
    let result=responsive.wait(timeout:.now()+1)
    if result != .success { release.signal(); fatalError("Video work blocked control cancellation") }
    close(fd)
    let replacement=socket(AF_INET,SOCK_DGRAM,0); precondition(replacement>=0)
    precondition(withAddress(&endpoint) { bind(replacement,$0,$1) } == 0, "Old sender retained the handover port")
    close(replacement); release.signal()
    precondition(finished.wait(timeout:.now()+1) == .success)
    print("Production video sender isolation and immediate port release check passed")
}
try checkSenderIsolation()
final class CaptureEngine: @unchecked Sendable {
    let codec: Codec
    let effectiveBitrate: UInt64
    var active = false, contentRect = CGRect(x: 0, y: 0, width: 640, height: 360)
    var onFrame: ((Data, UInt64, Bool) -> Bool)?, onFailure: ((String) -> Void)?
    private var pts: UInt64 = 0
    init(hello: Hello, codec: Codec, queue: DispatchQueue, cursorVisible: Bool = true, desktopSRGB: Bool = false) throws {
        self.codec = codec; effectiveBitrate = hello.bitrate
        try String(cursorVisible).write(toFile: ProcessInfo.processInfo.environment["TD_TEST_CURSOR_STATE"]!, atomically: true, encoding: .utf8)
    }
    func start(displayID: UInt32?) async throws -> UInt32 { 0 }
    func requestIDR() {
        guard active else { return }
        var data = codec.isRaw ? Data(repeating: 0xC0, count: 640 * 360 * 3) : Data([0,0,0,1]) + Data(repeating: 0x65, count: 179996)
        if codec == .rawPacked10 || codec == .rawDelta10 {
            data = try! data.withUnsafeBytes { bytes in
                try RawVideoWire.pack(width: 640, height: 360,
                    luma: bytes, lumaStride: 1280,
                    chroma: UnsafeRawBufferPointer(start: bytes.baseAddress!.advanced(by: 640 * 360 * 2), count: 640 * 360), chromaStride: 1280, packed: true)
            }
        }
        _ = onFrame?(data, pts, true); pts += 10000
    }
    func stop() {
        active = false
        let root = ProcessInfo.processInfo.environment["TD_TEST_HANDOVER"]!
        if FileManager.default.fileExists(atPath: root + "/block-stop") {
            FileManager.default.createFile(atPath: root + "/stop-entered", contents: Data())
            let deadline = Date().addingTimeInterval(6)
            while !FileManager.default.fileExists(atPath: root + "/release-stop") {
                precondition(Date() < deadline, "Test teardown was not released")
                Thread.sleep(forTimeInterval: 0.01)
            }
        }
    }
}
protocol InputControlling: AnyObject {
    func apply(_ input: Input) throws
    func releaseAll()
}
final class InputInjector: InputControlling {
    init(display: UInt32, captureSize: CGSize, contentRect: CGRect, desktopBounds: [CGRect] = []) {}
    func apply(_ input: Input) throws {
        if [1, 2, 5, 6, 7].contains(input.kind) {
            let path = ProcessInfo.processInfo.environment["TD_TEST_HANDOVER"]! + "/pointer-input"
            var data = (try? Data(contentsOf: URL(fileURLWithPath: path))) ?? Data()
            data.append(input.kind); try data.write(to: URL(fileURLWithPath: path)); return
        }
        if input.code == 66 { throw HostError("Synthetic system input failure") }
        fatalError("Session handover must never inject input")
    }
    func releaseAll() {}
}
let options = try Options()
let testWindowsDiagnosticStore = WindowsDiagnosticStore(directory: URL(fileURLWithPath: ProcessInfo.processInfo.environment["TD_TEST_HANDOVER"]!))
let server = HostServer(ip: "127.0.0.1", options: options, token: "")
server.displayCapabilities = ProtocolWire.capabilities(width: 640, height: 360, hz: 144, maximumWidth: 3840, maximumHeight: 2160, maximumHz: 240, flags: 1, name: "Test Mac", codecMask: 7, streamBits: 10)
server.handoverOnSessionEnd = true
server.localCursorAvailable = true
server.desktopBounds = [CGRect(x: 0, y: 0, width: 1920, height: 1080), CGRect(x: -1280, y: 0, width: 1280, height: 720)]
server.allowClipboard = false
server.inputAllowed = { FileManager.default.fileExists(atPath: ProcessInfo.processInfo.environment["TD_TEST_INPUT_ALLOW"]!) }
server.onKeyboardPacket = {
    let path = ProcessInfo.processInfo.environment["TD_TEST_HANDOVER"]! + "/keyboard-received"
    FileManager.default.createFile(atPath: path, contents: Data())
}
server.onClipboardState = { session, enabled in if session != 0 && !enabled { server.updateClipboardPermission(true) } }
server.onCursorState = { session, enabled, _ in if enabled {
    server.sendCursor(Data(repeating: 0x7f, count: 4100), session: session)
    server.sendCursorPosition(CGPoint(x: 0, y: 0), visible: false, session: session)
} }
server.onClipboardImage = { session, image in server.sendImage(image, session: session) }
server.onClipboardText = { session, text in server.sendClipboard(text, session: session) }
try server.start()
print("CHECK_READY"); fflush(stdout)
var handingOver = false
let checkTimer = Timer.scheduledTimer(withTimeInterval: 0.01, repeats: true) { _ in
    let root = ProcessInfo.processInfo.environment["TD_TEST_HANDOVER"]!
    if !handingOver && FileManager.default.fileExists(atPath: root + "/handover") {
        handingOver = true
        DispatchQueue.global().async {
            server.stop(handover: true)
            FileManager.default.createFile(atPath: root + "/stop-finished", contents: Data())
        }
    }
}
RunLoop.current.run(until: Date().addingTimeInterval(15))
checkTimer.invalidate()
server.stop()
''')
    sources = ROOT / 'mac-host/Sources/ThunderDisplayHost'
    # The transport harness deliberately substitutes its non-injecting stub for
    # InputSupport; the production input module has separate event-level tests.
    (folder / 'HostServer.swift').write_text((sources / 'HostServer.swift').read_text().replace('import InputSupport\n', '').replace('WindowsDiagnosticStore.shared', 'testWindowsDiagnosticStore'))
    subprocess.run(compiler + ['-I', directory, '-L', directory, '-lWire', '-Xlinker', '-rpath', '-Xlinker', directory,
        str(folder / 'main.swift'), str(folder / 'HostServer.swift'), str(sources / 'Support.swift'), str(sources / 'VideoSender.swift'), str(sources / 'RawVideoSender.swift'), str(ROOT / 'mac-host/Sources/InputSupport/CapturedPointer.swift'),
        '-framework', 'AppKit', '-framework', 'SystemConfiguration', '-o', str(folder / 'host-check')], env=env, check=True)
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    env['TD_TEST_CURSOR_STATE'] = str(folder / 'cursor-visible.txt')
    env['TD_TEST_INPUT_ALLOW'] = str(folder / 'allow-input')
    env['TD_TEST_HANDOVER'] = str(folder)
    (folder / 'allow-input').touch()
    process = subprocess.Popen([str(folder / 'host-check'), '--port', str(port), '--no-pairing'],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    try:
        for line in process.stdout:
            if line.strip() == 'CHECK_READY':
                break
        else:
            raise AssertionError('Host failed to start')
        # A legacy query must not silently opt into gigabit bitrate / large frames.
        with socket.create_connection(('127.0.0.1', port), timeout=3) as legacy:
            def legacy_read(count):
                data = b''
                while len(data) < count:
                    chunk = legacy.recv(count-len(data))
                    assert chunk, 'Legacy check disconnected early'
                    data += chunk
                return data
            query = bytes([9, 3]); legacy.sendall(struct.pack('!I', len(query)) + query)
            caps = legacy_read(struct.unpack('!I', legacy_read(4))[0])
            assert caps[25] == 13
            request = struct.pack('!BHHHHHIB32s', 1, 1, 50000, 640, 360, 60, 1000000000, 2, bytes(32))
            legacy.sendall(struct.pack('!I', len(request)) + request)
            failure = legacy_read(struct.unpack('!I', legacy_read(4))[0])
            assert failure[0] == 7 and b'High bitrate requires' in failure
            assert legacy.recv(1) == b'', 'Failed legacy connection was not closed'
        # Keep v5-v9 peers within their original 20 Gbps range.
        with socket.create_connection(('127.0.0.1', port), timeout=3) as old, old.makefile('rb') as stream:
            query = bytes([9, 5, 0]); old.sendall(struct.pack('!I', len(query)) + query)
            caps = stream.read(struct.unpack('!I', stream.read(4))[0])
            assert caps[0] == 10
            request = struct.pack('!BHHHHHQB32s', 15, 2, 50000, 640, 360, 60, 40000000000, 2, bytes(32))
            old.sendall(struct.pack('!I', len(request)) + request)
            failure = stream.read(struct.unpack('!I', stream.read(4))[0])
            assert failure[0] == 7 and b'High link bitrate requires' in failure
        with socket.create_connection(('127.0.0.1', port), timeout=3) as tcp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4*1024*1024)
            udp.bind(('127.0.0.1', 0)); udp.settimeout(.2)
            def control(payload):
                tcp.sendall(struct.pack('!I', len(payload)) + payload)
            def read_exact(count):
                data = b''
                while len(data) < count:
                    chunk = tcp.recv(count-len(data))
                    assert chunk, 'Host disconnected'
                    data += chunk
                return data
            control(bytes([9]))
            capabilities = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert capabilities[:3] == bytes([10, 0, 1])
            assert struct.unpack_from('!IIHIIHBBBH', capabilities, 3) == (640, 360, 144, 3840, 2160, 240, 3, 8, 1, 8)
            assert capabilities[28:] == b'Test Mac'
            control(bytes([9, 2]))
            extended = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert extended[23:25] == bytes([7, 10]) and extended[28:] == b'Test Mac'
            control(bytes([9, 3]))
            features = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert features[25] == 13, 'Native-pixel/clipboard feature bits missing'
            control(bytes([9, 4, 1]))
            latest = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert latest[25] == 29, 'Legacy client must keep the video cursor'
            for version in (6, 7, 8, 9, 10):
                control(bytes([9, version, 1]))
                modern = read_exact(struct.unpack('!I', read_exact(4))[0])
                assert modern[25] == 253, f'Query{version} must preserve rich and sRGB flags'
            control(bytes([9, 11, 1]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0])[25] == 221, 'Multi-display query must use the real video cursor'
            control(bytes([9, 5, 1]))
            rich = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert rich[25] == 125, 'Rich cursor/image/20 Gbps feature bits missing'
            control(bytes([9, 10, 1]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0])[25] == 253
            hello = struct.pack('!BHHHHHQB32s', 15, 2, udp.getsockname()[1], 4096, 2560, 144, 40000000000, 4, bytes(32))
            control(hello)
            welcome = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert len(welcome) == 26 and welcome[:3] == bytes([16, 0, 2])
            assert welcome[11] == 4, 'Host did not select requested Main10'
            assert struct.unpack_from('!HH', welcome, 12) == (4096, 2560), 'Native HiDPI dimensions were not preserved'
            assert struct.unpack_from('!Q', welcome, 18)[0] == 40000000000, 'Detected link bitrate was truncated or overwritten'
            assert (folder / 'cursor-visible.txt').read_text() == 'false', 'Host did not hide the video cursor before capture starts'
            session = struct.unpack_from('!Q', welcome, 3)[0]
            probe = f'TDVIDEO1 {session}'.encode()
            # Before Ready, even a correct probe cannot expose/activate the stream.
            udp.sendto(probe, ('127.0.0.1', port))
            try:
                udp.recvfrom(1500)
                raise AssertionError('Probe accepted before Ready')
            except socket.timeout:
                pass
            control(bytes([8]))
            cursor = b''
            while len(cursor) < 4100:
                packet = read_exact(struct.unpack('!I', read_exact(4))[0])
                typ, ident, total, offset = struct.unpack_from('!BIII', packet)
                assert typ == 13 and total == 4100 and offset == len(cursor)
                cursor += packet[13:]
            assert cursor == bytes([0x7f])*4100
            control(struct.pack('!BBQ9I', 19, 1, session, 100, 200, 300, 400, 500, 600, 700, 800, 2))
            control(bytes([5]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6]), 'v9 latency report broke the control channel'
            print('Session-bound v9 Windows latency report accepted; control channel remains responsive')
            control(bytes([11, 1]))
            ack = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert ack == bytes([11, 0]), 'Mac clipboard-off preference was not respected'
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([11, 1]), 'Live Mac permission change was not acknowledged'
            clip = ('Windows → Mac\n中文🐱' * 1000).encode()
            assert len(clip) <= 65536
            for offset in range(0, len(clip), 3072):
                control(struct.pack('!BIII', 12, 9, len(clip), offset) + clip[offset:offset+3072])
            control(bytes([5]))
            chunks = {}; pong = False; clip_id = None
            while sum(map(len, chunks.values())) < len(clip) or not pong:
                packet = read_exact(struct.unpack('!I', read_exact(4))[0])
                if packet == bytes([6]):
                    pong = True
                else:
                    typ, ident, total, offset = struct.unpack_from('!BIII', packet)
                    assert typ == 12 and total == len(clip) and (clip_id is None or clip_id == ident)
                    clip_id = ident; chunks[offset] = packet[13:]
            assert b''.join(chunks[offset] for offset in sorted(chunks)) == clip
            image = bytes(range(256))*800
            for offset in range(0, len(image), 3072):
                control(struct.pack('!BIII', 14, 10, len(image), offset) + image[offset:offset+3072])
            control(bytes([5]))
            copied = b''; pong = False
            while len(copied) < len(image) or not pong:
                packet = read_exact(struct.unpack('!I', read_exact(4))[0])
                if packet == bytes([6]): pong = True; continue
                typ, ident, total, offset = struct.unpack_from('!BIII', packet)
                assert typ == 14 and total == len(image) and offset == len(copied)
                copied += packet[13:]
            assert copied == image
            control(bytes([11, 0]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([11, 0])
            print('Host v10 / 40 Gbps wide negotiation, native cursor/image transfers and bounded Unicode clipboard round-trip, enable/disable acknowledgments and interleaved ping passed')
            # Ignore initial video, emulating a return path that has not opened yet.
            end = time.monotonic()+.3
            while time.monotonic() < end:
                try: udp.recvfrom(1500)
                except socket.timeout: pass
            for invalid in (f'TDVIDEO1 {session+1}'.encode(), probe+b' junk'):
                udp.sendto(invalid, ('127.0.0.1', port))
                try:
                    udp.recvfrom(1500)
                    raise AssertionError('Invalid session/probe accepted')
                except socket.timeout:
                    pass
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as stranger:
                stranger.bind(('127.0.0.1', 0)); stranger.settimeout(.2)
                stranger.sendto(probe, ('127.0.0.1', port))
                try:
                    stranger.recvfrom(1500)
                    raise AssertionError('Probe from undeclared video port accepted')
                except socket.timeout:
                    pass
            udp.sendto(probe, ('127.0.0.1', port))
            reply, sender = udp.recvfrom(1500)
            parts = reply.decode().split()
            assert sender == ('127.0.0.1', port) and parts[:2] == ['TDVIDEO1', str(session)]
            video_port = int(parts[2]); assert 0 < video_port <= 65535
            udp.sendto(probe, ('127.0.0.1', video_port))
            pieces = {}; frame_id = None; deadline = time.monotonic()+3
            while time.monotonic() < deadline:
                data, sender = udp.recvfrom(1500)
                h = struct.unpack('!IBBHQIQIHHHH', data[:40])
                magic, version, codec, flags, sid, fid, pts, size, index, count, length, reserved = h
                assert sender == ('127.0.0.1', video_port)
                assert (magic, version, codec, flags, sid, size, reserved) == (0x54444231, 1, 4, 1, session, 180000, 0)
                assert len(data) == 40+length and count == (size+1159)//1160
                assert fid > 1, 'Video probe did not resend an IDR after initial frame'
                if frame_id is None: frame_id = fid
                assert frame_id == fid
                pieces[index] = data[40:]
                if len(pieces) == count:
                    payload = b''.join(pieces[i] for i in range(count))
                    assert payload == bytes([0,0,0,1])+bytes([0x65])*179996
                    print(f'Host legacy/Main10 capability query + Main10 UDP probe check passed: session/port validation, matching reply socket, IDR resend, {len(pieces)} fragments / {len(payload)} synthetic bytes')
                    break
            else:
                raise AssertionError('No complete keyframe after UDP probe')
            # Routine path keepalives must not trigger expensive keyframes.
            # The same live session must still honor explicit recovery requests.
            for _ in range(3):
                udp.sendto(probe, ('127.0.0.1', video_port))
                try:
                    udp.recvfrom(1500)
                    raise AssertionError('Repeated keepalive forced an unnecessary keyframe')
                except socket.timeout:
                    pass
            recovery_request = bytes([4])  # Message.requestIDR
            tcp.sendall(struct.pack('!I', len(recovery_request)) + recovery_request)
            recovered = {}; recovery_id = None; deadline = time.monotonic()+3
            while time.monotonic() < deadline:
                data, sender = udp.recvfrom(1500)
                h = struct.unpack('!IBBHQIQIHHHH', data[:40])
                _, _, _, flags, sid, fid, _, size, index, count, length, _ = h
                assert sender == ('127.0.0.1', video_port) and sid == session and flags == 1
                assert fid > frame_id and size == 180000 and len(data) == 40+length
                if recovery_id is None: recovery_id = fid
                assert fid == recovery_id
                recovered[index] = data[40:]
                if len(recovered) == count:
                    assert b''.join(recovered[i] for i in range(count)) == payload
                    print('Repeated video keepalives do not re-encode; explicit IDR recovery remains functional')
                    break
            else:
                raise AssertionError('Explicit recovery request did not produce a complete keyframe')
            # A v9 Ready peer is notified of handover before its input can reach
            # the fatal injection stub; diagnostic reports must not block it.
            (folder / 'allow-input').unlink()
            control(struct.pack('!BBHHii', 3, 3, 65, 1, 0, 0))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([17]) + struct.pack('!Q', session) + bytes([1]), 'Old-session input did not trigger handover'
            control(struct.pack('!BBQ9I', 19, 1, session, 100, 200, 300, 400, 500, 600, 700, 800, 2))
            control(bytes([18]) + struct.pack('!Q', session) + bytes([1]))
            control(bytes([5]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6]), 'Late statistics blocked handover control'
            assert process.poll() is None, 'Denied input reached the injection stub'
            assert not (folder / 'keyboard-received').exists(), 'Denied input was logged as accepted keyboard input'
        # When the system input backend fails, the real server must report the
        # failure and close the peer instead of silently ignoring keyboard input.
        (folder / 'allow-input').touch()
        with socket.create_connection(('127.0.0.1', port), timeout=3) as tcp:
            control(bytes([9, 11, 1]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0])[25] == 221
            control(hello)
            welcome = read_exact(struct.unpack('!I', read_exact(4))[0])
            mouse_session = struct.unpack_from('!Q', welcome, 3)[0]
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([20]) + struct.pack('!Q', mouse_session) + bytes([1])
            assert (folder / 'cursor-visible.txt').read_text() == 'true'
            control(bytes([8]))
            for kind, code, flags, x, y in [(6, 0, 0, -20, 10), (7, 0, 1, 0, 0), (7, 0, 0, 0, 0), (5, 0, 0, 0, 0), (1, 0, 0, 30000, 40000)]:
                control(struct.pack('!BBHHii', 3, kind, code, flags, x, y))
            control(bytes([5]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6])
            assert (folder / 'pointer-input').read_bytes() == bytes([6, 7, 7, 5, 1]), 'Fullscreen/window movement or click ordering changed'
            print('Query11 multi-display input, video cursor, session-bound mouse mode and window absolute fallback passed (synthetic input only)')
        for query, raw_codec in [(13, 8), (14, 16), (15, 32), (16, 32)]:
            sample = 0xC0C0 >> 6
            packed_group = (sample | (sample << 10) | (sample << 20) | (sample << 30)).to_bytes(5, 'little')
            expected_payload = bytes([0xC0]) * (640 * 360 * 3) if query == 13 else packed_group * (640 * 360 * 3 // 8)
            expected_size = len(expected_payload)
            with socket.create_connection(('127.0.0.1', port), timeout=3) as tcp:
                control(bytes([9, query, 0]))
                caps = read_exact(struct.unpack('!I', read_exact(4))[0])
                assert caps[23] == (63 if query >= 15 else 31 if query == 14 else 15) and caps[24] == 10 and caps[25] == 253
                raw_hello = struct.pack('!BHHHHHQB32s', 15, 2, 50000, 640, 360, 60, expected_size * 8 * 60, raw_codec, bytes(32))
                control(raw_hello)
                welcome = read_exact(struct.unpack('!I', read_exact(4))[0])
                raw_session = struct.unpack_from('!Q', welcome, 3)[0]
                assert welcome[11] == raw_codec
                endpoint = read_exact(struct.unpack('!I', read_exact(4))[0])
                assert endpoint[:9] == bytes([22]) + struct.pack('!Q', raw_session)
                raw_port = struct.unpack_from('!H', endpoint, 9)[0]
                assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([20]) + struct.pack('!Q', raw_session) + bytes([1])
                assert (folder / 'cursor-visible.txt').read_text() == 'false', 'New multi-display mode must hide the video cursor automatically'
                # A stale session must not gain access to raw screen pixels.
                with socket.create_connection(('127.0.0.1', raw_port), timeout=3) as stale:
                    stale.sendall(struct.pack('!IQ', 0x54445241, raw_session + 1))
                    assert stale.recv(1) == b''
                with socket.create_connection(('127.0.0.1', raw_port), timeout=3) as raw:
                    raw.sendall(struct.pack('!IQ', 0x54445241, raw_session))
                    control(bytes([8]))
                    cursor, position = b'', None
                    while len(cursor) < 4100 or position is None:
                        packet = read_exact(struct.unpack('!I', read_exact(4))[0])
                        if packet[0] == 21:
                            position = packet
                        else:
                            typ, transfer, total, offset = struct.unpack_from('!BIII', packet)
                            assert typ == 13 and total == 4100 and offset == len(cursor)
                            cursor += packet[13:]
                    assert position == bytes([21]) + struct.pack('!Q', raw_session) + bytes(5)
                    def raw_exact(count):
                        data = b''
                        while len(data) < count:
                            piece = raw.recv(count-len(data))
                            assert piece
                            data += piece
                        return data
                    header = raw_exact(20)
                    magic, frame_id, size, pts = struct.unpack('!IIIQ', header)
                    assert magic == 0x54445246 and frame_id and size == expected_size + (16 if query >= 15 else 0)
                    expected_update = struct.pack('!IIHHI', 0x54444431, 0, 0, 0, 0) if query >= 15 else b''
                    assert raw_exact(size) == expected_update + expected_payload
                    if query >= 15:
                        control(bytes([4]))
                        magic, next_id, size, pts = struct.unpack('!IIIQ', raw_exact(20))
                        assert next_id > frame_id and size == 16
                        assert raw_exact(size) == struct.pack('!IIHHI', 0x54444431, frame_id, 1, 0, 0)
                    if query == 16:
                        start = struct.pack('!BBQB', 23, 1, raw_session, 1)
                        stop = struct.pack('!BBQB', 23, 1, raw_session, 0)
                        report = struct.pack('!BBQB', 23, 1, raw_session, 2)
                        control(start)
                        assert read_exact(struct.unpack('!I', read_exact(4))[0]) == start
                        control(report + b'video.performance receive_fps=60 present_fps=59\n')
                        control(bytes([5]))
                        assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6])
                        log_file = folder / 'windows-client.log'
                        deadline = time.monotonic() + 2
                        while not log_file.exists() or 'present_fps=59' not in log_file.read_text():
                            assert time.monotonic() < deadline, 'Mac did not store the Windows log'
                            time.sleep(0.01)
                        # Bound the persistent logs, not just the network queue.
                        with log_file.open('ab') as output:
                            output.write(b'x' * (2 * 1024 * 1024))
                        control(stop)
                        assert read_exact(struct.unpack('!I', read_exact(4))[0]) == stop
                        control(report + b'video.performance after-stop\n')
                        control(bytes([5]))
                        assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6])
                        time.sleep(0.02)
                        assert 'after-stop' not in log_file.read_text()
                        assert log_file.stat().st_mode & 0o777 == 0o600
                        previous_log = folder / 'windows-client.previous.log'
                        deadline = time.monotonic() + 2
                        while not previous_log.exists() or 'sharing=disabled' not in log_file.read_text():
                            assert time.monotonic() < deadline, 'Mac log rotation did not finish'
                            time.sleep(0.01)
                        assert previous_log.stat().st_mode & 0o777 == 0o600
                        print('Query16 Windows debug logs stored on Mac; enable/disable acknowledgments and independent control passed')
                    # Stop draining a raw transfer, then prove input/control still works.
                    for _ in range(20): control(bytes([4]))
                    control(bytes([5]))
                    assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6]), 'Raw transfer blocked the control channel'
                print(f'Query{query} raw TCP, stale-session rejection, exact ten-bit frame bytes, independent cursor and responsive control passed')
        with socket.create_connection(('127.0.0.1', port), timeout=3) as tcp:
            control(bytes([9, 10, 0]))
            read_exact(struct.unpack('!I', read_exact(4))[0])
            control(hello)
            read_exact(struct.unpack('!I', read_exact(4))[0])
            control(bytes([8]))
            control(struct.pack('!BBHHii', 3, 6, 0, 0, 20, 0))
            assert tcp.recv(1) == b'', 'Unnegotiated relative input was accepted'
            assert (folder / 'pointer-input').read_bytes() == bytes([6, 7, 7, 5, 1])
        # The remaining handover scenarios use protocol v8, whose original
        # 20 Gbps limit remains in effect even on a v10-capable host.
        hello = hello[:11] + struct.pack('!Q', 20000000000) + hello[19:]
        with socket.create_connection(('127.0.0.1', port), timeout=3) as tcp:
            control(bytes([9, 8, 0]))
            read_exact(struct.unpack('!I', read_exact(4))[0])
            control(hello)
            welcome = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert welcome[0] == 16
            control(bytes([8]))
            control(struct.pack('!BBHHii', 3, 3, 66, 1, 0, 0))
            failure = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert failure == bytes([7])+b'InputUnavailable: Synthetic system input failure'
            assert tcp.recv(1) == b''
            assert process.poll() is None
            assert (folder / 'keyboard-received').exists(), 'Accepted keyboard packet did not trigger its diagnostic'
            (folder / 'keyboard-received').unlink()
            print('Ready-session handover and system input failure feedback checks passed (no OS input injection)')
        # Announce a login transition even with no subsequent keyboard packet,
        # before the AppKit-side shutdown timer notices the session change.
        with socket.create_connection(('127.0.0.1', port), timeout=3) as tcp:
            control(bytes([9, 8, 0]))
            read_exact(struct.unpack('!I', read_exact(4))[0])
            control(hello)
            welcome = read_exact(struct.unpack('!I', read_exact(4))[0])
            transition_session = struct.unpack('!Q', welcome[3:11])[0]
            control(bytes([8])); control(bytes([5]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6])
            (folder / 'allow-input').unlink()
            notice = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert notice == bytes([17]) + struct.pack('!Q', transition_session) + bytes([1])
            control(bytes([18]) + struct.pack('!Q', transition_session) + bytes([1]))
            control(bytes([5]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6])
            assert not (folder / 'keyboard-received').exists()
            print('Idle login transition announced before capture/input delivery; control channel remains usable')
        (folder / 'allow-input').touch()
        # A slow encoder shutdown must not prevent the desktop host claiming
        # both the TCP listener and wildcard UDP discovery port immediately.
        with socket.create_connection(('127.0.0.1', port), timeout=3) as tcp:
            control(bytes([9, 8, 0]))
            read_exact(struct.unpack('!I', read_exact(4))[0])
            control(hello)
            welcome = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert welcome[0] == 16
            transition_session = struct.unpack('!Q', welcome[3:11])[0]
            control(bytes([8])); control(bytes([5]))
            assert read_exact(struct.unpack('!I', read_exact(4))[0]) == bytes([6])
            (folder / 'block-stop').touch(); (folder / 'handover').touch()
            notice = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert notice == bytes([17]) + struct.pack('!Q', transition_session) + bytes([1]), 'Missing session-scoped handover notice'
            # Even an in-flight input packet before the receipt must not reach
            # the old LoginWindow injector after the handover notice.
            control(struct.pack('!BBHHii', 3, 3, 65, 1, 0, 0))
            control(bytes([18]) + struct.pack('!Q', transition_session) + bytes([1]))
            deadline = time.monotonic() + 3
            while not (folder / 'stop-entered').exists():
                assert time.monotonic() < deadline, 'Shutdown did not reach capture cleanup'
                time.sleep(.01)
            assert not (folder / 'stop-finished').exists(), 'Capture cleanup did not block'
            assert not (folder / 'keyboard-received').exists(), 'Handover input was logged as accepted keyboard input'
            for _ in range(5):
                with socket.socket() as replacement, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery:
                    replacement.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    discovery.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    replacement.bind(('127.0.0.1', port)); replacement.listen(1)
                    discovery.bind(('0.0.0.0', port))
            try:
                assert tcp.recv(1) == b'', 'Old control channel remained open during teardown'
            except ConnectionResetError:
                pass  # Handover deliberately avoids a root-owned TIME_WAIT socket.
            (folder / 'release-stop').touch()
            deadline = time.monotonic() + 3
            while not (folder / 'stop-finished').exists():
                assert time.monotonic() < deadline, 'Shutdown did not finish after release'
                time.sleep(.01)
            print('TCP / UDP ports can be claimed during blocked capture teardown; old connection closes and repeated cleanup succeeds')
    finally:
        process.terminate()
        process.communicate(timeout=5)
