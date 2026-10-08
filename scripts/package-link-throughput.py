#!/usr/bin/env python3
"""Package the synthetic Windows receiver as a standalone, reviewable CMD."""
import base64
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'scripts/windows-link-throughput.ps1').read_text(encoding='utf-8')
dist = root / 'dist'
dist.mkdir(exist_ok=True)
(dist / 'ThunderDisplay-Bandwidth-Check-source.ps1').write_text(source, encoding='utf-8-sig')
encoded = base64.b64encode(source.encode('utf-16le')).decode('ascii')
chunks = [encoded[i:i + 2000] for i in range(0, len(encoded), 2000)]
lines = ['@echo off', 'setlocal']
lines += [f'set "TD_BANDWIDTH_{i}={chunk}"' for i, chunk in enumerate(chunks)]
parts = '+'.join(f'$env:TD_BANDWIDTH_{i}' for i in range(len(chunks)))
command = f"$text=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String({parts})); & ([scriptblock]::Create($text))"
lines += [f'powershell.exe -NoLogo -NoProfile -Command "{command}"', 'pause', 'endlocal']
assert max(map(len, lines)) < 8191
assert base64.b64decode(''.join(chunks)).decode('utf-16le') == source
(dist / 'ThunderDisplay-Bandwidth-Check.cmd').write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
print('Packaged Windows bandwidth check; embedded source verified.')
