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

ROOT = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
with tempfile.TemporaryDirectory(prefix='td-udp-check-') as directory:
    folder = Path(directory)
    compiler = ['xcrun', 'swiftc', '-target', f'{platform.machine()}-apple-macosx13.0',
        '-module-cache-path', str(folder / 'module-cache')]
    subprocess.run(compiler + ['-emit-library', '-emit-module', '-module-name', 'Wire',
        '-emit-module-path', str(folder / 'Wire.swiftmodule'),
        str(ROOT / 'mac-host/Sources/Wire/Wire.swift'), str(ROOT / 'mac-host/Sources/Wire/Clipboard.swift'), str(ROOT / 'mac-host/Sources/Wire/Blob.swift'), '-o', str(folder / 'libWire.dylib')], env=env, check=True)
    (folder / 'main.swift').write_text('''
import AppKit
import Wire
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
        let data = Data([0,0,0,1]) + Data(repeating: 0x65, count: 179996)
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
    init(display: UInt32, captureSize: CGSize, contentRect: CGRect) {}
    func apply(_ input: Input) throws {
        if input.code == 66 { throw HostError("Synthetic system input failure") }
        fatalError("Session handover must never inject input")
    }
    func releaseAll() {}
}
let options = try Options()
let server = HostServer(ip: "127.0.0.1", options: options, token: "")
server.displayCapabilities = ProtocolWire.capabilities(width: 640, height: 360, hz: 144, maximumWidth: 3840, maximumHeight: 2160, maximumHz: 240, flags: 1, name: "Test Mac", codecMask: 7, streamBits: 10)
server.handoverOnSessionEnd = true
server.localCursorAvailable = true
server.allowClipboard = false
server.inputAllowed = { FileManager.default.fileExists(atPath: ProcessInfo.processInfo.environment["TD_TEST_INPUT_ALLOW"]!) }
server.onKeyboardPacket = {
    let path = ProcessInfo.processInfo.environment["TD_TEST_HANDOVER"]! + "/keyboard-received"
    FileManager.default.createFile(atPath: path, contents: Data())
}
server.onClipboardState = { session, enabled in if session != 0 && !enabled { server.updateClipboardPermission(true) } }
server.onCursorState = { session, enabled, _ in if enabled { server.sendCursor(Data(repeating: 0x7f, count: 4100), session: session) } }
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
    (folder / 'HostServer.swift').write_text((sources / 'HostServer.swift').read_text().replace('import InputSupport\n', ''))
    subprocess.run(compiler + ['-I', directory, '-L', directory, '-lWire', '-Xlinker', '-rpath', '-Xlinker', directory,
        str(folder / 'main.swift'), str(folder / 'HostServer.swift'), str(sources / 'Support.swift'),
        '-framework', 'AppKit', '-framework', 'SystemConfiguration', '-o', str(folder / 'host-check')], env=env, check=True)
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    env['TD_TEST_CURSOR_STATE'] = str(folder / 'cursor-visible.txt')
    env['TD_TEST_INPUT_ALLOW'] = str(folder / 'allow-input')
    env['TD_TEST_HANDOVER'] = str(folder)
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
            for version in (6, 7, 8):
                control(bytes([9, version, 1]))
                modern = read_exact(struct.unpack('!I', read_exact(4))[0])
                assert modern[25] == 253, f'Query{version} must preserve rich and sRGB flags'
            control(bytes([9, 5, 1]))
            rich = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert rich[25] == 125, 'Rich cursor/image/20 Gbps feature bits missing'
            hello = struct.pack('!BHHHHHQB32s', 15, 2, udp.getsockname()[1], 4096, 2560, 144, 20000000000, 4, bytes(32))
            control(hello)
            welcome = read_exact(struct.unpack('!I', read_exact(4))[0])
            assert len(welcome) == 26 and welcome[:3] == bytes([16, 0, 2])
            assert welcome[11] == 4, 'Host did not select requested Main10'
            assert struct.unpack_from('!HH', welcome, 12) == (4096, 2560), 'Native HiDPI dimensions were not preserved'
            assert struct.unpack_from('!Q', welcome, 18)[0] == 20000000000, 'Custom gigabit bitrate was overwritten'
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
            print('Host 20 Gbps wide negotiation, native cursor/image transfers and bounded Unicode clipboard round-trip, enable/disable acknowledgments and interleaved ping passed')
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
            # Even a Ready peer must be disconnected on session handover before
            # its input reaches the fatal injection stub.
            control(struct.pack('!BBHHii', 3, 3, 65, 1, 0, 0))
            assert tcp.recv(1) == b'', 'Old-session input was not rejected'
            assert process.poll() is None, 'Denied input reached the injection stub'
            assert not (folder / 'keyboard-received').exists(), 'Denied input was logged as accepted keyboard input'
        # When the system input backend fails, the real server must report the
        # failure and close the peer instead of silently ignoring keyboard input.
        (folder / 'allow-input').touch()
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
