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
   rcon('gamerule doDaylightCycle false');rcon('weather clear');rcon('time set noon')
   wait(lambda s:s['sky_factor']>.99 and s['sample_brightness']>.99,'clear noon uses full skylight')
   rcon('time set midnight')
   wait(lambda s:s['sky_factor']<.21 and s['sample_brightness']<.2,'midnight dims sky-lit terrain')
   stopped=state['day_time'];time.sleep(.5)
   wait(lambda s:abs(s['day_time']-stopped)<.01,'disabled daylight cycle stays frozen')
   rcon('time set noon');rcon('weather thunder')
   wait(lambda s:s['rain']>.99 and s['thunder']>.99 and s['sky_factor']<.5,'thunder darkens daytime sky',timeout=45)
   rcon('weather clear');rcon('fill -3 80 -3 3 84 3 stone hollow');rcon('setblock 1 82 1 glowstone')
   wait(lambda s:s['sample_light']>>4>=12 and (s['sample_light']&15)==0,'sealed glowstone room supplies block light')
   rcon('time set noon');wait(lambda s:s['day_time']==6000,'noon clock received');glow=state['sample_brightness']
   rcon('time set midnight')
   wait(lambda s:s['day_time']==18000 and abs(s['sample_brightness']-glow)<.001,'block light keeps its brightness at night')
   rcon('setblock 0 81 1 oak_sign[rotation=4]{Text1:\'{"text":"Vita sign"}\'}')
   send('sample 0 81 1')
   wait(lambda s:s['sample_entity_bytes']>0 and s['sample_text']=='Vita sign','live sign entity data and text')
   updates=state['block_entity_updates']
   rcon('data merge block 0 81 1 {Text1:\'{"text":"Updated sign"}\'}')
   wait(lambda s:s['sample_text']=='Updated sign' and s['block_entity_updates']>updates,'sign NBT update replaces presentation data')
   send('quit');proc.wait(5);assert proc.returncode==0
   proc=subprocess.Popen([str(ROOT/'build-host/live_gameplay'),'127.0.0.1','25566'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
   wait(lambda s:s.get('ready'),'reconnect for initial chunk block entities')
   send('sample 0 81 1')
   wait(lambda s:s['sample_text']=='Updated sign','chunk reload preserves sign NBT and text')
   rcon('setblock 0 81 1 air')
   wait(lambda s:s['sample_entity_type']==-1,'breaking sign releases cached block entity')
   rcon('setblock 0 81 1 chest[facing=north]')
   send('sample 0 81 1');send('open')
   wait(lambda s:s['window']>0 and s['sample_entity_action']==1 and s['sample_entity_parameter']>0,'server chest open action creates lid state')
   send('close')
   wait(lambda s:s['window']==0 and s['sample_entity_action']==1 and s['sample_entity_parameter']==0,'server chest close action restores lid')
   rcon('setblock 0 81 1 air')
   wait(lambda s:s['sample_entity_type']==-1,'breaking chest releases action record')
   send('quit');proc.wait(5);assert proc.returncode==0
   print('Lighting end-to-end checks passed; evidence: build-host/live-lighting.jsonl')
  finally:
   rcon('worldborder set 59999968')
   rcon('gamerule doDaylightCycle true');rcon('weather clear');rcon('time set day')
   rcon('fill -3 80 -3 3 84 3 air')
   if proc.poll() is None:
    proc.terminate()
    try:proc.wait(5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
if __name__=='__main__':main()
