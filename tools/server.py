#!/usr/bin/env python3
"""Manage the local vanilla Java 1.19.4 test server. Works with ARM64 Java."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1] / '.deps/server-1.19.4'
PID_FILE = ROOT / 'server.pid'

def get(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()

def download():
    ROOT.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(get('https://piston-meta.mojang.com/mc/game/version_manifest_v2.json'))
    version = next(v for v in manifest['versions'] if v['id'] == '1.19.4')
    metadata = get(version['url'])
    if hashlib.sha1(metadata).hexdigest() != version['sha1']:
        raise SystemExit('Version metadata checksum mismatch')
    server = json.loads(metadata)['downloads']['server']
    jar = ROOT / 'server.jar'
    if not jar.exists() or hashlib.sha1(jar.read_bytes()).hexdigest() != server['sha1']:
        data = get(server['url'])
        if hashlib.sha1(data).hexdigest() != server['sha1']:
            raise SystemExit('Server download checksum mismatch')
        jar.write_bytes(data)
    (ROOT / 'version.json').write_bytes(metadata)
    print('Verified vanilla 1.19.4:', jar)

def running_pid():
    try:
        pid = int(PID_FILE.read_text().strip())
        # Check ownership before acting on a persisted, possibly recycled PID.
        if pid < 2 or Path(f'/proc/{pid}/cwd').resolve() != ROOT:
            return None
        command = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
        if b'server.jar' not in command:
            return None
        os.kill(pid, 0)
        return pid
    except (OSError, ValueError):
        return None

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('command', choices=['download', 'start', 'stop', 'status'])
parser.add_argument('--bind', default='127.0.0.1', help='Bind address on first setup; existing properties are preserved')
parser.add_argument('--java', default='java')
args = parser.parse_args()
pid = running_pid()
if args.command == 'download':
    download()
elif args.command == 'status':
    print(f'Running, PID {pid}' if pid else 'Stopped')
    print('Properties:', ROOT / 'server.properties')
    print('Log:', ROOT / 'console.log')
elif args.command == 'stop':
    if pid:
        os.kill(pid, signal.SIGTERM)
        for _ in range(100):
            if not running_pid():
                break
            time.sleep(.2)
        else:
            raise SystemExit('Shutdown is still in progress; inspect console.log')
        print('Stopped cleanly')
    else:
        print('Already stopped')
else:
    if pid:
        raise SystemExit(f'Already running, PID {pid}')
    download()
    eula = ROOT / 'eula.txt'
    if not eula.exists() or 'eula=true' not in eula.read_text().splitlines():
        raise SystemExit(f'Review https://aka.ms/MinecraftEULA and record acceptance in {eula} before starting.')
    properties = ROOT / 'server.properties'
    if not properties.exists():
        properties.write_text(f'server-ip={args.bind}\nserver-port=25565\nonline-mode=false\nenforce-secure-profile=false\nview-distance=2\nsimulation-distance=2\nmax-players=4\ngamemode=creative\ndifficulty=peaceful\n')
    with (ROOT / 'console.log').open('ab') as log:
        child = subprocess.Popen([args.java, '-Xms256M', '-Xmx1G', '-XX:ActiveProcessorCount=2', '-jar', 'server.jar', 'nogui'], cwd=ROOT,
                                 stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    PID_FILE.write_text(str(child.pid)+'\n')
    print('Started, PID', child.pid, '(wait for Done in console.log)')
