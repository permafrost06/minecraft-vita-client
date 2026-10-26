#!/usr/bin/env python3
"""Opt-in movement and dimension checks on the isolated vanilla fixture."""
import json, select, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import test_server

def main():
 if not test_server.running():raise SystemExit('Start tools/test_server.py first')
 with (ROOT/'build-host/live-movement.jsonl').open('w') as log:
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
  def teleport(x,y,z,yaw=0):
   before=state['teleports'];send('idle');rcon(f'tp SurvivalProbe {x} {y} {z} {yaw} 0')
   wait(lambda s:s['ready'] and s['teleports']>before and abs(s['y']-y)<.1,'server teleport and loaded destination')
  try:
   wait(lambda s:s.get('health_updates',0)>0 and s.get('teleports',0)>0,'login and initial server state')
   if state['health']<=0:
    send('respawn');wait(lambda s:s.get('ready') and s['health']>0,'recover dead fixture player')
   rcon('execute in minecraft:overworld run tp SurvivalProbe 0.5 -60 0.5 0 0')
   wait(lambda s:s['ready'] and s['dimension']=='minecraft:overworld','initial overworld destination')
   for command in ['gamemode survival SurvivalProbe','effect clear SurvivalProbe','effect give SurvivalProbe minecraft:instant_health 1 10 true','effect give SurvivalProbe minecraft:saturation 1 10 true','worldborder set 59999968','clear SurvivalProbe','fill -4 -60 -4 4 -56 40 air','fill -4 -61 -4 4 -61 40 stone']:
    rcon(command)
   teleport(.5,-60,.5)
   send('input 1 0 0 0');start=state['ticks'];wait(lambda s:s['ticks']>=start+25,'walking with vanilla movement validation')
   walk=state['z']-.5;assert 4<walk<7,state
   teleport(.5,-60,.5)
   send('input 1 0 1 0');start=state['ticks'];before=state['teleports']
   wait(lambda s:s['ticks']>=start+25 and s['sprint'],'sprinting and server attribute updates')
   assert state['z']-.5>walk*1.15 and state['teleports']==before,(walk,state)
   assert '0.13' in rcon('attribute SurvivalProbe minecraft:generic.movement_speed get'),state
   teleport(.5,-60,.5)
   rcon('fill 0 -59 2 0 -59 7 minecraft:oak_slab[type=top,waterlogged=false]')
   send('input 1 1 0 0');wait(lambda s:s['sneak'] and s['z']>3 and s['eye']<1.3,'crouch command, eye height and low ceiling passage')
   send('idle');rcon('fill 0 -59 2 0 -59 7 air')
   teleport(.5,-60,.5);rcon('fill -2 -60 -2 2 -56 4 water');send('input 0 0 0 1')
   wait(lambda s:s['water'] and s['y']>-58.5,'water jump and buoyancy')
   send('idle');rcon('fill -2 -60 -2 2 -56 4 air');teleport(.5,-60,.5)
   rcon('worldborder center 0.0 0.0');rcon('worldborder set 6');send('input 1 0 0 0')
   wait(lambda s:s['border']==6 and abs(s['z']-2.7)<.01,'server border packets and player collision')
   send('idle');rcon('worldborder set 59999968')
   # Same coordinates in different dimensions verify cache reset and registry heights.
   for dimension,min_y,height in [('the_nether',0,256),('the_end',0,256),('overworld',-64,384)]:
    send('idle');rcon(f'execute in minecraft:{dimension} run forceload add -16 -16 16 16')
    deadline=time.monotonic()+45
    while True:
     response=rcon(f'execute in minecraft:{dimension} run fill -2 80 -2 2 84 2 air')
     if 'not loaded' not in response:break
     if time.monotonic()>deadline:raise AssertionError('Destination chunks did not load: '+dimension)
     time.sleep(.25)
    rcon(f'execute in minecraft:{dimension} run fill -2 79 -2 2 79 2 obsidian')
    before=state['teleports'];rcon(f'execute in minecraft:{dimension} run tp SurvivalProbe 0.5 80 0.5')
    wait(lambda s:s['ready'] and s['teleports']>before and s['dimension']=='minecraft:'+dimension and s['min_y']==min_y and s['height']==height and abs(s['y']-80)<.01,'dimension transition: '+dimension,45)
    rcon(f'execute in minecraft:{dimension} run forceload remove -16 -16 16 16')
   send('quit');proc.wait(5);assert proc.returncode==0
   print('Movement end-to-end checks passed; evidence: build-host/live-movement.jsonl')
  finally:
   rcon('worldborder set 59999968')
   if proc.poll() is None:
    proc.terminate()
    try:proc.wait(5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
if __name__=='__main__':main()
