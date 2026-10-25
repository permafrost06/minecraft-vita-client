#!/usr/bin/env python3
"""Read a saved player's position from the local vanilla test server."""
import argparse,gzip,hashlib,io,json,struct,uuid
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('username');args=parser.parse_args()
uid=uuid.UUID(bytes=hashlib.md5(('OfflinePlayer:'+args.username).encode()).digest(),version=3)
path=Path(__file__).resolve().parents[1]/'.deps/server-1.19.4/world/playerdata'/f'{uid}.dat'
raw=gzip.decompress(path.read_bytes())
if len(raw)>2*1024*1024:raise SystemExit('Player data exceeds size bound')
f=io.BytesIO(raw)
def read(n):
 p=f.read(n)
 if len(p)!=n:raise ValueError('Truncated NBT')
 return p
def number(fmt):return struct.unpack('>'+fmt,read(struct.calcsize('>'+fmt)))[0]
def string():return read(number('H')).decode()
def value(t,depth=0):
 if depth>32:raise ValueError('NBT nesting limit')
 if t in (1,2,3,4,5,6):return number({1:'b',2:'h',3:'i',4:'q',5:'f',6:'d'}[t])
 if t==8:return string()
 if t in (7,11,12):
  n=number('i')
  if n<0 or n>1048576:raise ValueError('Invalid array size')
  return [number({7:'b',11:'i',12:'q'}[t]) for _ in range(n)]
 if t==9:
  child=number('B');n=number('i')
  if n<0 or n>1048576:raise ValueError('Invalid list size')
  return [value(child,depth+1) for _ in range(n)]
 if t==10:
  obj={}
  while (child:=number('B')):
   name=string();obj[name]=value(child,depth+1)
  return obj
 raise ValueError('Unsupported NBT tag')
assert number('B')==10
string();data=value(10)
print(json.dumps({'username':args.username,'uuid':str(uid),'position':data['Pos'],'rotation':data['Rotation'],'on_ground':data.get('OnGround'),'abilities':data.get('abilities')},indent=2))
