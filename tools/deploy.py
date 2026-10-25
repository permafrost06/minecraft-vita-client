#!/usr/bin/env python3
"""Install build-vita/eboot.bin on the Vita via VitaCompanion and relaunch the app.

The upload goes to a temporary file, is read back and compared, and the previous
executable is kept in .deps/device-backups before the rename.
"""
import argparse, socket, time
from datetime import datetime, timezone
from ftplib import FTP
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

data = args.eboot.read_bytes()
if not data:
    raise SystemExit('Refusing to install an empty executable')
reply = companion(f'quit {TITLE}')
print('quit:', reply)
if reply != 'Quit.':
    raise SystemExit('App stop was not acknowledged; inspect device state before retrying')
time.sleep(1)
with FTP() as ftp:
    ftp.connect(args.host, 1337, timeout=20); ftp.login()
    ftp.cwd(f'ux0:/app/{TITLE}')
    old = BytesIO()
    ftp.retrbinary('RETR eboot.bin', old.write)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%f')
    backups = Path('.deps/device-backups'); backups.mkdir(parents=True, exist_ok=True)
    with (backups / f'eboot-{stamp}.bin').open('xb') as backup:
        backup.write(old.getvalue())
    ftp.storbinary('STOR eboot.tmp', BytesIO(data))
    check = BytesIO(); ftp.retrbinary('RETR eboot.tmp', check.write)
    if check.getvalue() != data: raise SystemExit('Upload verification failed')
    previous = f'eboot.{stamp}'
    ftp.rename('eboot.bin', previous)
    try:
        ftp.rename('eboot.tmp', 'eboot.bin')
    except Exception:
        ftp.rename(previous, 'eboot.bin')
        raise
    installed = BytesIO(); ftp.retrbinary('RETR eboot.bin', installed.write)
    if installed.getvalue() != data:
        raise SystemExit(f'Installed executable verification failed; previous binary retained as {previous}')
print(f'installed {len(data)} bytes, verified')
if not args.no_launch:
    time.sleep(0.5)
    print('launch:', companion(f'launch {TITLE}'))
