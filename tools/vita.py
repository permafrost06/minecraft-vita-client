#!/usr/bin/env python3
"""Use the app's local command file and framebuffer capture over VitaCompanion FTP."""
import argparse
from ftplib import FTP, error_perm
from io import BytesIO
from pathlib import Path
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', default='192.168.100.226')
parser.add_argument('command', choices=['status', 'login', 'disconnect', 'shot', 'log', 'health', 'menu', 'reset', 'jump', 'fly', 'inspect', 'streamtest', 'debug', 'break', 'place', 'slotnext', 'blocknext', 'inventory'])
parser.add_argument('--output', type=Path, default=Path('build-vita/shot.bmp'))
args = parser.parse_args()
with FTP() as ftp:
    ftp.connect(args.host, 1337, timeout=10)
    ftp.login()
    ftp.cwd('ux0:/data/vitablock')
    if args.command in ('log', 'health'):
        data = BytesIO()
        ftp.retrbinary('RETR '+('runtime.log' if args.command == 'log' else 'health.txt'), data.write)
        print(data.getvalue().decode(errors='replace'))
    else:
        if args.command == 'streamtest':
            try:ftp.delete('stream-test.txt')
            except error_perm as e:
                if not str(e).startswith('550'):raise
        if args.command == 'shot':
            try:
                ftp.delete('shot.bmp')
            except error_perm as e:
                if not str(e).startswith('550'):
                    raise
        ftp.storbinary('STOR command.tmp', BytesIO((args.command+'\n').encode()))
        ftp.rename('command.tmp', 'command.txt')
        # Wait for consumption before allowing the next command to overwrite it.
        # Application launch can take several seconds after Companion acknowledges.
        for _ in range(50):
            pending = BytesIO()
            try:
                ftp.retrbinary('RETR command.txt', pending.write)
            except error_perm as e:
                if str(e).startswith('550'):
                    break
                raise
            time.sleep(.2)
        else:
            raise SystemExit('App has not consumed the command; it may still be starting or in the background.')
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
            print('Processed', args.command)
