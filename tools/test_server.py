#!/usr/bin/env python3
"""Manage separate vanilla fixture servers with loopback RCON.
survival: loopback-only survival fixture (ports 25566/25576).
vita: creative fixture for device tests (ports 25567/25577). Vanilla binds RCON
 to the same server-ip as the game port, so a LAN bind exposes password RCON there.
"""
import argparse,hashlib,os,secrets,signal,socket,struct,subprocess,time
from pathlib import Path
DEPS=Path(__file__).resolve().parents[1]/'.deps'
SOURCE=DEPS/'server-1.19.4'
PROFILES={'survival':('server-survival',25566,25576,'survival','normal'),'vita':('server-vita',25567,25577,'creative','peaceful')}
def use(profile):
 global ROOT,PID,PORT,RCON,MODE,DIFFICULTY
 name,PORT,RCON,MODE,DIFFICULTY=PROFILES[profile];ROOT=DEPS/name;PID=ROOT/'server.pid'
use(os.environ.get('VITABLOCK_FIXTURE','survival'))
def running():
 try:
  pid=int(PID.read_text());args=Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
  if pid<2 or Path(f'/proc/{pid}/cwd').resolve()!=ROOT or b'server.jar' not in args:return None
  os.kill(pid,0);return pid
 except (OSError,ValueError):return None

def command(text):
 if '\0' in text or len(text.encode())>32768:raise ValueError('Invalid command')
 password=(ROOT/'rcon-password').read_text().strip()
 def exact(s,n):
  data=b''
  while len(data)<n:
   part=s.recv(n-len(data))
   if not part:raise ConnectionError('RCON closed')
   data+=part
  return data
 def send(s,seq,kind,value):
  body=struct.pack('<ii',seq,kind)+value.encode()+b'\0\0';s.sendall(struct.pack('<i',len(body))+body)
 def receive(s):
  n=struct.unpack('<i',exact(s,4))[0]
  if not 10<=n<=1024*1024:raise ValueError('Invalid RCON packet')
  data=exact(s,n);seq,kind=struct.unpack('<ii',data[:8]);return seq,kind,data[8:-2].decode(errors='replace')
 host=next((l.split('=',1)[1].strip() for l in (ROOT/'server.properties').read_text().splitlines() if l.startswith('server-ip=')),'') or '127.0.0.1'
 with socket.create_connection((host,RCON),timeout=5) as s:
  send(s,1,3,password)
  for _ in range(3):
   seq,kind,_=receive(s)
   if seq==-1:raise PermissionError('RCON authentication rejected')
   if seq==1 and kind==2:break
  else:raise ConnectionError('No RCON authentication response')
  send(s,2,2,text);seq,_,response=receive(s)
  if seq!=2:raise ConnectionError('Unexpected RCON response')
  return response

def start(java='java',bind='127.0.0.1'):
 if running():return 'Already running'
 jar=SOURCE/'server.jar';eula=SOURCE/'eula.txt'
 if not jar.exists() or hashlib.sha1(jar.read_bytes()).hexdigest()!='8f3112a1049751cc472ec13e397eade5336ca7ae':raise RuntimeError('Verified vanilla 1.19.4 server.jar is required')
 if not eula.exists() or 'eula=true' not in eula.read_text().splitlines():raise RuntimeError('Existing accepted server EULA is required')
 ROOT.mkdir(exist_ok=True)
 target=ROOT/'server.jar'
 if not target.exists():target.symlink_to(jar)
 (ROOT/'eula.txt').write_bytes(eula.read_bytes())
 secret=ROOT/'rcon-password'
 if not secret.exists():
  fd=os.open(secret,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
  with os.fdopen(fd,'w') as f:f.write(secrets.token_hex(24)+'\n')
 properties=ROOT/'server.properties'
 if not properties.exists():
  properties.write_text(f'server-ip={bind}\nserver-port={PORT}\nonline-mode=false\nenforce-secure-profile=false\nview-distance=2\nsimulation-distance=2\nmax-players=4\ngamemode={MODE}\nforce-gamemode=true\ndifficulty={DIFFICULTY}\nspawn-monsters=false\nspawn-animals=true\nspawn-npcs=false\nspawn-protection=0\nlevel-type=minecraft:flat\ngenerate-structures=false\nlevel-seed=12345\nenable-rcon=true\nrcon.port={RCON}\nbroadcast-rcon-to-ops=false\nrcon.password='+secret.read_text().strip()+'\n')
  properties.chmod(0o600)
 with (ROOT/'console.log').open('ab') as log:
  proc=subprocess.Popen([java,'-Xms256M','-Xmx512M','-XX:ActiveProcessorCount=2','-jar','server.jar','nogui'],cwd=ROOT,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
 PID.write_text(str(proc.pid)+'\n');return f'Started isolated {ROOT.name} fixture, PID {proc.pid}'

def stop():
 pid=running()
 if not pid:return 'Already stopped'
 os.kill(pid,signal.SIGTERM)
 for _ in range(100):
  if not running():return 'Stopped cleanly'
  time.sleep(.2)
 raise RuntimeError('Server is still shutting down')
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('action',choices=['start','stop','status','command']);parser.add_argument('text',nargs='?')
 parser.add_argument('--profile',choices=sorted(PROFILES),default=os.environ.get('VITABLOCK_FIXTURE','survival'))
 parser.add_argument('--bind',default='127.0.0.1',help='Game and RCON bind address on first setup')
 args=parser.parse_args();use(args.profile)
 if args.action=='start':print(start(bind=args.bind))
 elif args.action=='stop':print(stop())
 elif args.action=='status':print('Running' if running() else 'Stopped')
 elif args.text:print(command(args.text))
 else:parser.error('command needs a command string')
