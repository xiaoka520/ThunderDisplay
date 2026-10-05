#!/usr/bin/env python3
"""HLSL syntax / SPIR-V check; does not substitute for Windows D3DCompile/GPU QA."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
compiler = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/toolchains/glslang-16.6.0/bin/glslang'
folder = ROOT / 'build/tests'
folder.mkdir(parents=True, exist_ok=True)
source = (ROOT / 'windows-client/src/scaler.hpp').read_text()
shader = folder / 'scaler.hlsl'
shader.write_text(re.search(r'R"hlsl\((.*?)\)hlsl"', source, re.S).group(1))
for stage, entry in [('vert', 'vs'), ('frag', 'ps')]:
    subprocess.run([str(compiler), '-D', '-V', '-S', stage, '-e', entry, '--auto-map-bindings',
        str(shader), '-o', str(folder / f'scaler-{stage}.spv')], check=True)
print('Scaling vertex/pixel HLSL syntax and SPIR-V compilation passed; Windows GPU validation remains pending')
