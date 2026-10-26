#!/usr/bin/env python3
"""Opt-in sleeping, gliding and riding checks on tools/test_server.py."""
import sys,subprocess,select,json,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'));import test_server
if not test_server.running():raise SystemExit('Start tools/test_server.py first')
for command in ['gamerule playersSleepingPercentage 101','gamerule doDaylightCycle false','time set midnight','fill -2 -60 -2 2 -56 4 air','fill -2 -61 -2 2 -61 4 stone','setblock 0 -60 2 red_bed[facing=south,part=foot]','setblock 0 -60 3 red_bed[facing=south,part=head]']:test_server.command(command)
log=(ROOT/'build-host/live-mobility.jsonl').open('w')
p=subprocess.Popen(['build-host/live_gameplay','127.0.0.1','25566'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
def send(s):p.stdin.write(s+'\n');p.stdin.flush()
def wait(pred):
 end=time.monotonic()+15
 while time.monotonic()<end:
  if select.select([p.stdout],[],[],.5)[0]:
   line=p.stdout.readline();log.write(line);log.flush();s=json.loads(line)
   if pred(s):print(s['sleeping'],s['x'],s['y'],s['z']);return s
 raise RuntimeError('Timeout')
try:
 wait(lambda s:s['ready']);print(test_server.command('tp SurvivalProbe 0.5 -60 0.5 0 30'));wait(lambda s:s['z']<1)
 send('sample 0 -60 2');send('open');wait(lambda s:s['sleeping']);send('input 1 0 0 0');time.sleep(1);wait(lambda s:s['sleeping']);send('input 0 0 0 1');wait(lambda s:not s['sleeping']);send('idle');test_server.command('item replace entity SurvivalProbe armor.chest with elytra')
 test_server.command('tp SurvivalProbe 0.5 40 0.5 0 0');wait(lambda s:s['y']>35 and not s['sleeping'])
 send('input 0 0 0 1');wait(lambda s:s['gliding'])
 start=wait(lambda s:s['gliding']);time.sleep(2);end=wait(lambda s:s['gliding'] and s['z']>start['z']+3)
 assert end['teleports']==start['teleports'],(start,end)
 assert end['y']>start['y']-15,(start,end)
 send('idle');test_server.command('tp SurvivalProbe 0.5 -60 0.5 0 0');wait(lambda s:not s['gliding'] and s['y']<-59)
 test_server.command('item replace entity SurvivalProbe armor.chest with air')
 send('quit');p.wait(5)
finally:
 if p.poll() is None:p.terminate();p.wait()
 for command in ['setblock 0 -60 2 air','setblock 0 -60 3 air','gamerule playersSleepingPercentage 100','gamerule doDaylightCycle true']:test_server.command(command)
 log.close()
