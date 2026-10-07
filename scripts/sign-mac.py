#!/usr/bin/env python3
"""Keep a project-local signing identity so rebuilding does not change the DR."""
import hashlib
import json
import os
import plistlib
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
STORE = ROOT / '.local-signing'


def run(args, env=None):
    result = subprocess.run(args, capture_output=True, env=env)
    if result.returncode:
        # Never include command arguments: security receives private passwords.
        raise RuntimeError(f'{Path(args[0]).name} {args[1]} failed: {result.stderr.decode(errors="replace").strip()}')
    return result.stdout


def local_identity():
    STORE.mkdir(mode=0o700, exist_ok=True)
    STORE.chmod(0o700)
    metadata = STORE / 'signer.json'
    keychain = STORE / 'codesign.keychain-db'
    if not metadata.exists():
        if keychain.exists():
            raise RuntimeError('Incomplete local signing setup. Preserve or remove .local-signing before retrying.')
        password = secrets.token_urlsafe(32)
        with tempfile.TemporaryDirectory(dir=STORE) as temporary:
            folder = Path(temporary)
            config = folder / 'certificate.cnf'
            config.write_text('[req]\nprompt=no\ndistinguished_name=subject\nx509_extensions=signing\n'
                f'[subject]\nCN=ThunderDisplay Dev {uuid.uuid4().hex}\n'
                '[signing]\nbasicConstraints=critical,CA:false\nkeyUsage=critical,digitalSignature\n'
                'extendedKeyUsage=critical,codeSigning\nsubjectKeyIdentifier=hash\n')
            key, cert, p12 = folder / 'key.pem', folder / 'cert.pem', folder / 'identity.p12'
            run(['/usr/bin/openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes', '-x509',
                 '-sha256', '-days', '3650', '-config', str(config), '-keyout', str(key), '-out', str(cert)])
            key.chmod(0o600)
            environment = dict(os.environ, TD_LOCAL_P12_PASSWORD=password)
            run(['/usr/bin/openssl', 'pkcs12', '-export', '-inkey', str(key), '-in', str(cert),
                 '-out', str(p12), '-passout', 'env:TD_LOCAL_P12_PASSWORD'], env=environment)
            p12.chmod(0o600)
            run(['/usr/bin/security', 'create-keychain', '-p', password, str(keychain)])
            run(['/usr/bin/security', 'unlock-keychain', '-p', password, str(keychain)])
            run(['/usr/bin/security', 'import', str(p12), '-k', str(keychain), '-f', 'pkcs12',
                 '-P', password, '-x', '-T', '/usr/bin/codesign'])
            run(['/usr/bin/security', 'set-key-partition-list', '-S', 'apple-tool:,apple:',
                 '-s', '-k', password, str(keychain)])
            der = run(['/usr/bin/openssl', 'x509', '-in', str(cert), '-outform', 'DER'])
            (STORE / 'certificate.pem').write_bytes(cert.read_bytes())
            with open(metadata, 'x', opener=lambda path, flags: os.open(path, flags, 0o600)) as file:
                json.dump({'password': password, 'identity': hashlib.sha1(der).hexdigest()}, file)
    settings = json.loads(metadata.read_text())
    run(['/usr/bin/security', 'unlock-keychain', '-p', settings['password'], str(keychain)])
    return settings['identity'], keychain


def main():
    os.umask(0o077)
    app = Path(sys.argv[1]).resolve()
    identity = os.environ.get('THUNDERDISPLAY_SIGN_IDENTITY')
    keychain = None
    bundle_id = plistlib.loads((app / 'Contents/Info.plist').read_bytes())['CFBundleIdentifier']
    command = ['/usr/bin/codesign', '--force', '--identifier', bundle_id]
    if identity:
        command += ['--sign', identity]
    else:
        identity, keychain = local_identity()
        command += ['--sign', identity, '--keychain', str(keychain), '--timestamp=none']
    command.append(str(app))
    try:
        helper = app / 'Contents/MacOS/ThunderDisplayBoot'
        if helper.exists():
            helper_command = command[:-1] + [str(helper)]
            helper_command[helper_command.index('--identifier') + 1] = 'dev.thunderdisplay.boot'
            run(helper_command)
        run(command)
    finally:
        if keychain is not None:
            run(['/usr/bin/security', 'lock-keychain', str(keychain)])
    # The private signing store retains 0700/0600. This public resource manifest
    # must also be readable after the application is copied to a root-owned path.
    (app / 'Contents/_CodeSignature/CodeResources').chmod(0o644)
    run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)])
    print('Mac app signed and verified with ' + ('configured identity' if os.environ.get('THUNDERDISPLAY_SIGN_IDENTITY') else 'persistent project-local identity'))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
