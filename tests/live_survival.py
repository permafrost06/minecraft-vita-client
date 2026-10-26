#!/usr/bin/env python3
"""Opt-in end-to-end survival checks on tools/test_server.py's isolated server."""
import json,select,subprocess,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import test_server

def main():
 if not test_server.running():raise SystemExit('Start tools/test_server.py first')
 for command in ['gamerule keepInventory true','gamerule doMobSpawning false','gamerule doDaylightCycle false','time set day','weather clear','gamerule doImmediateRespawn false','kill @e[type=!minecraft:player]']:
  test_server.command(command)
 log=ROOT/'build-host/live-survival.jsonl'
 with log.open('w') as out:
  proc=subprocess.Popen([str(ROOT/'build-host/live_gameplay'),'127.0.0.1','25566'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
  state={}
  def send(command):proc.stdin.write(command+'\n');proc.stdin.flush()
  def wait(predicate,label,timeout=15):
   nonlocal state
   end=time.monotonic()+timeout
   while time.monotonic()<end:
    if proc.poll() is not None:raise AssertionError(f'Client exited {proc.returncode}: {state}')
    if not select.select([proc.stdout],[],[],.5)[0]:continue
    line=proc.stdout.readline();out.write(line);out.flush()
    try:state=json.loads(line)
    except json.JSONDecodeError:continue
    if predicate(state):print('Passed:',label,flush=True);return state
   raise AssertionError(f'Timed out: {label}: {state}')
  def count(s,item):return sum(n for ident,n,_ in s.get('items',[]) if ident==item)
  def rcon(command):
   response=test_server.command(command);out.write('# '+command+' => '+response.replace('\n',' ')+'\n');out.flush();return response
  try:
   wait(lambda s:s.get('ready'),'login, health and terrain')
   if state['health']<=0:
    send('respawn');wait(lambda s:s.get('ready') and s['health']>0,'recover previous dead fixture player')
   for command in ['gamemode survival SurvivalProbe','clear SurvivalProbe','effect clear SurvivalProbe','effect give SurvivalProbe minecraft:instant_health 1 10 true','tp SurvivalProbe 0.5 -60 0.5 0 45','setblock 0 -60 2 minecraft:dirt']:
    rcon(command)
   wait(lambda s:s.get('ready') and s['block']==10 and abs(s['x']-.5)<.01 and abs(s['y']+60)<.01,'controlled mining setup')
   send('mine');wait(lambda s:s['block']==0 and s['finishes']>=1 and s['ack']>=2,'timed survival digging and server block confirmation')
   send('idle');send('move 0.5 -60 2.5');wait(lambda s:count(s,15)>=1,'dropped dirt pickup synchronized into inventory')
   send('idle')
   rcon('tp SurvivalProbe 0.5 -60 0.5 180 0');rcon('clear SurvivalProbe');rcon('damage SurvivalProbe 6 minecraft:out_of_world')
   wait(lambda s:s['health']<20,'server damage and health update')
   rcon('effect give SurvivalProbe minecraft:hunger 2 255 true');wait(lambda s:s['food']<18,'server hunger effect')
   rcon('effect clear SurvivalProbe minecraft:hunger');rcon('give SurvivalProbe minecraft:apple 2')
   wait(lambda s:count(s,755)==2,'food inventory update');old_food=state['food']
   send('use');wait(lambda s:count(s,755)<2 and s['food']>old_food,'held item use consumes food and raises server hunger')
   send('idle');rcon('clear SurvivalProbe');rcon('give SurvivalProbe minecraft:bow');rcon('give SurvivalProbe minecraft:arrow 8')
   wait(lambda s:count(s,756)==1 and count(s,757)==8,'bow and arrow inventory')
   before=state['spawned'];send('use');wait(lambda s:s['server_using'],'bow draw metadata')
   time.sleep(1.1);send('idle');wait(lambda s:s['spawned']>before and count(s,757)==7,'bow release spawns a server projectile and consumes an arrow')
   rcon('clear SurvivalProbe');rcon('item replace entity SurvivalProbe weapon.offhand with minecraft:shield')
   wait(lambda s:any(slot==45 for _,_,slot in s['items']),'offhand item update')
   send('use');wait(lambda s:s['server_using'] and s['offhand'],'offhand shield use');send('idle')
   rcon('clear SurvivalProbe');rcon('give SurvivalProbe minecraft:diamond_sword');rcon('tp SurvivalProbe 0.5 -60 0.5 0 10')
   rcon('summon minecraft:cow 0.5 -60 2.2 {NoAI:1b,Silent:1b,Attributes:[{Name:"generic.knockback_resistance",Base:1.0d}]}')
   wait(lambda s:any(e[1]==18 for e in s['entities']),'server mob spawn and metadata')
   send('mine');wait(lambda s:s['attacks']>=2 and not any(e[1]==18 for e in s['entities']),'entity attack and server-confirmed mob death',20);send('idle')
   deaths=state['deaths'];teleports=state['teleports'];rcon('kill SurvivalProbe')
   wait(lambda s:s['health']==0 and s['deaths']>deaths,'player death notification')
   send('respawn');wait(lambda s:s.get('ready') and s['health']==20 and s['teleports']>teleports,'respawn request, dimension reset, health and teleport acknowledgement')
   send('quit');proc.wait(5)
   assert proc.returncode==0
   print('Survival end-to-end checks passed; evidence:',log)
  finally:
   if proc.poll() is None:
    proc.terminate()
    try:proc.wait(5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
if __name__=='__main__':main()
