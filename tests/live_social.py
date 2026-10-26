#!/usr/bin/env python3
"""Opt-in isolated vanilla 1.19.4 chat/audio/workstation validation."""
import json, select, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import test_server

def main():
 if not test_server.running():raise SystemExit('Start tools/test_server.py first')
 log=(ROOT/'build-host/live-social.jsonl').open('w')
 proc=subprocess.Popen([str(ROOT/'build-host/social_client'),'127.0.0.1','25566'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
 state={}
 def send(s):proc.stdin.write(s+'\n');proc.stdin.flush()
 def command(s):
  reply=test_server.command(s);log.write('# '+s+' => '+reply+'\n');log.flush();return reply
 def wait(predicate,label,timeout=15):
  nonlocal state
  end=time.monotonic()+timeout
  while time.monotonic()<end:
   if proc.poll() is not None:raise AssertionError(f'Client exited: {state}')
   if not select.select([proc.stdout],[],[],.5)[0]:continue
   line=proc.stdout.readline();log.write(line);log.flush();state=json.loads(line)
   if predicate(state):print('Passed:',label,flush=True);return state
  raise AssertionError(f'Timed out: {label}: {state}')
 def open_menu(block,kind,item):
  send('close');wait(lambda s:s['window']==0,'close menu')
  command('clear SocialProbe');command('item replace entity SocialProbe hotbar.0 with '+item)
  command('setblock 0 -60 2 '+block);command('tp SocialProbe 0.5 -60 0.5 0 0')
  wait(lambda s:s['window']==0 and s['slots'][36][1]>0,'receive fixture item')
  send('open 0 -60 2');wait(lambda s:s['type']==kind and s['window']>0 and len(s['slots'])>=38,'open '+block)
 def put(slot,destination):
  send(f'click {slot} 0 0');wait(lambda s:s['carried'][1]>0,'pickup ingredient')
  send(f'click {destination} 0 0');wait(lambda s:s['carried'][1]==0 and s['slots'][destination][1]>0,'place ingredient')
 try:
  wait(lambda s:s['ready'] and s['recipes']>0,'login and declared stonecutting recipes')
  command('gamemode creative SocialProbe');command('tp SocialProbe 0.5 -60 0.5')
  before=state['chat'];command('tellraw SocialProbe {"text":"Chat fixture"}')
  wait(lambda s:s['chat']>before,'system chat')
  before=state['chat'];send('say hello from Vita protocol');wait(lambda s:s['chat']>before,'unsigned outgoing chat echoed by vanilla')
  before=state['sounds'];command('playsound minecraft:entity.experience_orb.pickup master SocialProbe 0.5 -60 0.5 1 1')
  wait(lambda s:s['sounds']>before,'registry sound packet')
  before=state['chat'];send('say /seed');wait(lambda s:s['chat']>before,'unsigned command response')
  open_menu('stonecutter',24,'stone 32');put(29,0)
  wait(lambda s:s['options']>0,'stonecutter choices match input')
  choices=state['choices'][:]
  for index,expected in enumerate(choices):
   send(f'button {index}');wait(lambda s:s['slots'][1]==expected,f'stonecutter recipe {index}: {expected}')
  send('click 1 0 1');wait(lambda s:s['slots'][0][1]==0,'shift-taking stonecutter output crafts the available stack')
  open_menu('anvil',7,'diamond_sword');put(30,0);send('rename Vita Blade')
  wait(lambda s:s['slots'][2][1]==1,'anvil rename produces server output')
  send('click 2 0 1');wait(lambda s:s['slots'][0][1]==0,'take renamed sword')
  assert 'Vita Blade' in command('data get entity SocialProbe Inventory')
  open_menu('enchanting_table',12,'diamond_pickaxe');command('item replace entity SocialProbe hotbar.1 with lapis_lazuli 64');command('experience set SocialProbe 30 levels')
  put(29,0);put(30,1);wait(lambda s:s['properties'][0]>0,'enchantment offers')
  send('button 0');wait(lambda s:s['properties'][0]==0,'enchantment selection applied')
  send('click 0 0 1');wait(lambda s:s['slots'][0][1]==0,'take enchanted pickaxe')
  assert 'Enchantments' in command('data get entity SocialProbe Inventory')
  print('Live social checks passed; evidence build-host/live-social.jsonl')
 finally:
  if proc.poll() is None:send('quit');proc.wait(5)
  command('setblock 0 -60 2 air');command('clear SocialProbe');log.close()

if __name__=='__main__':main()
