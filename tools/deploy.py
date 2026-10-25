#!/usr/bin/env python3
"""Install build-vita/eboot.bin on the Vita via VitaCompanion and relaunch the app.

The upload goes to a temporary file, is read back and compared, and the previous
executable is kept in .deps/device-backups before the rename.
"""
import argparse, socket, time
from datetime import datetime, timezone
from ftplib import FTP, error_perm
from io import BytesIO
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', default='192.168.100.226')
parser.add_argument('--eboot', type=Path, default=Path('build-vita/eboot.bin'))
parser.add_argument('--no-launch', action='store_true')
args = parser.parse_args()
TITLE = 'VBLK00001'

def companion(command):
    with socket.create_connection((args.host, 1338), timeout=10) as s:
        s.sendall((command+'\n').encode())
        try: return s.recv(256).decode(errors='replace').strip()
        except socket.timeout: return ''

print('quit:', companion(f'quit {TITLE}'))
time.sleep(1)
data = args.eboot.read_bytes()
with FTP() as ftp:
    ftp.connect(args.host, 1337, timeout=20); ftp.login()
    ftp.cwd(f'ux0:/app/{TITLE}')
    old = BytesIO()
    try:
        ftp.retrbinary('RETR eboot.bin', old.write)
        backups = Path('.deps/device-backups'); backups.mkdir(parents=True, exist_ok=True)
        (backups / f"eboot-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}.bin").write_bytes(old.getvalue())
    except error_perm:
        pass
    ftp.storbinary('STOR eboot.tmp', BytesIO(data))
    check = BytesIO(); ftp.retrbinary('RETR eboot.tmp', check.write)
    if check.getvalue() != data: raise SystemExit('Upload verification failed')
    try: ftp.delete('eboot.bin')
    except error_perm: pass
    ftp.rename('eboot.tmp', 'eboot.bin')
print(f'installed {len(data)} bytes, verified')
if not args.no_launch:
    time.sleep(0.5)
    print('launch:', companion(f'launch {TITLE}'))
