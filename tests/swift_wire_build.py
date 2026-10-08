"""Build the production Wire module and its native pixel helper for socket checks."""
from pathlib import Path
import subprocess


def compile_wire(compiler, folder, env):
    root = Path(__file__).resolve().parents[1]
    native = root / 'mac-host/Sources/RawPixelSupport'
    include = native / 'include'
    obj = folder / 'RawPixelSupport.o'
    subprocess.run(['xcrun', 'clang', '-std=c11', '-O3', '-Wall', '-Wextra', '-Werror',
                    '-mmacosx-version-min=13.0', '-fPIC', '-I', str(include),
                    '-c', str(native / 'RawPixelSupport.c'), '-o', str(obj)], env=env, check=True)
    compiler = compiler + ['-I', str(include)]
    wire = sorted((root / 'mac-host/Sources/Wire').glob('*.swift'))
    subprocess.run(compiler + ['-emit-library', '-emit-module', '-module-name', 'Wire',
                   '-emit-module-path', str(folder / 'Wire.swiftmodule')] + list(map(str, wire)) +
                   [str(obj), '-o', str(folder / 'libWire.dylib')], env=env, check=True)
    return compiler
