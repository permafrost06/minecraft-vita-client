#!/usr/bin/env python3
"""Use the app's local command file and framebuffer capture over VitaCompanion FTP."""
import argparse
from ftplib import FTP, error_perm
from io import BytesIO
from pathlib import Path
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', default='192.168.100.226')
parser.add_argument('command', choices=['status', 'login', 'disconnect', 'shot', 'log'])
parser.add_argument('--output', type=Path, default=Path('build-vita/shot.bmp'))
args = parser.parse_args()
with FTP() as ftp:
    ftp.connect(args.host, 1337, timeout=10)
    ftp.login()
    ftp.cwd('ux0:/data/vitablock')
    if args.command == 'log':
        data = BytesIO()
        ftp.retrbinary('RETR runtime.log', data.write)
        print(data.getvalue().decode(errors='replace'))
    else:
        if args.command == 'shot':
            try:
                ftp.delete('shot.bmp')
            except error_perm as e:
                if not str(e).startswith('550'):
                    raise
        ftp.storbinary('STOR command.txt', BytesIO((args.command+'\n').encode()))
        if args.command == 'shot':
            for _ in range(25):
                data = BytesIO()
                try:
                    ftp.retrbinary('RETR shot.bmp', data.write)
                    if len(data.getvalue()) == 54+960*544*4:
                        args.output.parent.mkdir(parents=True, exist_ok=True)
                        args.output.write_bytes(data.getvalue())
                        print(args.output)
                        break
                except error_perm as e:
                    if not str(e).startswith('550'):
                        raise
                time.sleep(.2)
            else:
                raise SystemExit('Screenshot did not arrive; confirm the client is in the foreground.')
        else:
            print('Queued', args.command)
