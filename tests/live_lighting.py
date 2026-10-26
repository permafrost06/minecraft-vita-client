#!/usr/bin/env python3
"""Opt-in lighting checks on the isolated vanilla fixture."""
import json, select, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import test_server

def main():
 if not test_server.running():raise SystemExit('Start tools/test_server.py first')
 with (ROOT/'build-host/live-lighting.jsonl').open('w') as log:
  proc=subprocess.Popen([str(ROOT/'build-host/live_gameplay'),'127.0.0.1','25566'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
  state={}
  def send(command):proc.stdin.write(command+'\n');proc.stdin.flush()
  def rcon(command):
   response=test_server.command(command);log.write('# '+command+' => '+response.replace('\n',' ')+'\n');log.flush();return response
  def wait(predicate,label,timeout=20):
   nonlocal state
   end=time.monotonic()+timeout
   while time.monotonic()<end:
    if proc.poll() is not None:raise AssertionError(f'Client exited {proc.returncode}: {state}')
    if not select.select([proc.stdout],[],[],.5)[0]:continue
    line=proc.stdout.readline();log.write(line);log.flush()
    try:state=json.loads(line)
    except json.JSONDecodeError:continue
    if predicate(state):print('Passed:',label,flush=True);return state
   raise AssertionError(f'Timed out: {label}: {state}')
  try:
   wait(lambda s:s.get('health_updates',0)>0 and s.get('teleports',0)>0,'login and initial server state')
   if state['health']<=0:
    send('respawn');wait(lambda s:s.get('ready') and s['health']>0,'recover dead fixture player')
   rcon('execute in minecraft:overworld run tp SurvivalProbe 0.5 -60 0.5 0 0')
   wait(lambda s:s['ready'] and s['dimension']=='minecraft:overworld','initial overworld destination')
   for command in ['gamemode creative SurvivalProbe','execute in minecraft:overworld run tp SurvivalProbe 0.5 81 0.5','fill -3 80 -3 3 84 3 stone hollow']:
    rcon(command)
   send('sample 0 82 0')
   wait(lambda s:s['ready'] and s['sample_light']==0,'sealed room has no sky or block light')
   updates=state['light_updates'];rcon('setblock 1 82 1 glowstone')
   wait(lambda s:s['sample_light']>>4>=12 and s['light_updates']>updates,'block light propagates from placed glowstone')
   updates=state['light_updates'];rcon('setblock 1 82 1 air')
   wait(lambda s:s['sample_light']==0 and s['light_updates']>updates,'removing light restores darkness')
   rcon('fill -2 84 -2 2 84 2 air')
   wait(lambda s:(s['sample_light']&15)==15,'opening roof supplies server skylight')
   send('quit');proc.wait(5);assert proc.returncode==0
   print('Lighting end-to-end checks passed; evidence: build-host/live-lighting.jsonl')
  finally:
   rcon('worldborder set 59999968')
   if proc.poll() is None:
    proc.terminate()
    try:proc.wait(5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
if __name__=='__main__':main()
