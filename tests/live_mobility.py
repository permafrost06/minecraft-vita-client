#!/usr/bin/env python3
"""Opt-in sleeping, gliding and riding checks on tools/test_server.py.

Uses the dedicated SurvivalProbe and temporary blocks/entities in the isolated
fixture world. Run from the repository root after starting the fixture server.
"""
import json
import select
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import test_server


def main():
    if not test_server.running():
        raise SystemExit('Start tools/test_server.py first')
    log = (ROOT / 'build-host/live-mobility.jsonl').open('w')
    proc = None
    state = {}

    def command(text):
        response = test_server.command(text)
        log.write('# ' + text + ' => ' + response.replace('\n', ' ') + '\n')
        log.flush()
        return response

    def send(text):
        proc.stdin.write(text + '\n')
        proc.stdin.flush()

    def wait(predicate, label):
        nonlocal state
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise AssertionError(f'Client exited {proc.returncode}: {state}')
            if not select.select([proc.stdout], [], [], .5)[0]:
                continue
            line = proc.stdout.readline()
            log.write(line)
            log.flush()
            state = json.loads(line)
            if predicate(state):
                print('Passed:', label, flush=True)
                return state
        raise AssertionError(f'Timed out: {label}: {state}')

    def teleport(x, y, z, pitch=0):
        send('idle')
        before = state['teleports']
        command(f'tp SurvivalProbe {x} {y} {z} 0 {pitch}')
        return wait(lambda s: s['ready'] and s['teleports'] > before and abs(s['y']-y) < 2,
                    'server teleport and loaded destination')

    original_sleep = command('gamerule playersSleepingPercentage').split(':')[-1].strip()
    original_cycle = command('gamerule doDaylightCycle').split(':')[-1].strip()
    try:
        for text in ['gamerule playersSleepingPercentage 101', 'gamerule doDaylightCycle false',
                     'time set midnight', 'fill -2 -60 -2 2 -56 4 air',
                     'fill -2 -61 -2 2 -61 4 stone',
                     'setblock 0 -60 2 red_bed[facing=south,part=foot]',
                     'setblock 0 -60 3 red_bed[facing=south,part=head]']:
            command(text)
        proc = subprocess.Popen([str(ROOT / 'build-host/live_gameplay'), '127.0.0.1', '25566'],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        wait(lambda s: s['ready'], 'login')
        teleport(.5, -60, .5, 30)
        send('sample 0 -60 2')
        send('open')
        sleeping = wait(lambda s: s['sleeping'], 'bed entry and server pose')
        send('input 1 0 0 0')
        settled = wait(lambda s: s['ticks'] > sleeping['ticks'] + 20, 'sleeping blocks walking')
        assert settled['sleeping'] and settled['x'] == sleeping['x'] and settled['y'] == sleeping['y'] and settled['z'] == sleeping['z']
        send('input 0 0 0 1')
        wait(lambda s: not s['sleeping'], 'wake request and server wake')
        command('item replace entity SurvivalProbe armor.chest with elytra')
        teleport(.5, 40, .5)
        send('input 0 0 0 1')
        start = wait(lambda s: s['gliding'], 'airborne jump activates elytra')
        end = wait(lambda s: s['gliding'] and s['ticks'] > start['ticks'] + 40,
                   'sustained gliding')
        assert end['z'] > start['z'] + 3 and end['y'] > start['y'] - 15, (start, end)
        assert end['teleports'] == start['teleports'], (start, end)
        teleport(.5, -60, .5)
        wait(lambda s: not s['gliding'], 'landing stops gliding')
        command('item replace entity SurvivalProbe armor.chest with air')
        for text in ['time set noon', 'fill -8 -60 -8 8 -57 24 water',
                     'kill @e[tag=mobility_test]']:
            command(text)
        teleport(.5, -56.5, .5)
        command('summon boat 0.5 -56.5 0.5 {Tags:["mobility_test"]}')
        command('ride SurvivalProbe mount @e[type=boat,tag=mobility_test,limit=1]')
        start = wait(lambda s: s['vehicle'] >= 0 and s['boat'], 'boat mount')
        send('input 1 0 0 0')
        end = wait(lambda s: s['boat'] and s['ticks'] > start['ticks'] + 40, 'boat forward travel')
        assert end['z'] > start['z'] + 3 and end['vehicle_corrections'] == start['vehicle_corrections'], (start, end)
        send('input 0 1 0 0')
        wait(lambda s: s['vehicle'] < 0, 'boat dismount')
        send('idle')
        for text in ['kill @e[tag=mobility_test]', 'fill -8 -60 -8 8 -57 24 air',
                     'fill 0 -61 -2 0 -61 24 redstone_block',
                     'fill 0 -60 -2 0 -60 24 powered_rail[shape=north_south,powered=true]']:
            command(text)
        teleport(.5, -59, .5)
        command('summon minecart 0.5 -59.9 0.5 {Tags:["mobility_test"],Motion:[0.0,0.0,0.2]}')
        command('ride SurvivalProbe mount @e[type=minecart,tag=mobility_test,limit=1]')
        start = wait(lambda s: s['vehicle'] >= 0 and not s['boat'], 'minecart mount')
        wait(lambda s: s['vehicle'] >= 0 and s['z'] > start['z'] + 3, 'server-driven minecart travel')
        send('input 0 1 0 0')
        wait(lambda s: s['vehicle'] < 0, 'minecart dismount')
        send('quit')
        proc.wait(5)
        assert proc.returncode == 0
        print('Mobility checks passed; evidence: build-host/live-mobility.jsonl')
    finally:
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        for text in ['kill @e[tag=mobility_test]', 'fill -8 -60 -8 8 -57 24 air',
                     'fill 0 -61 -2 0 -61 24 stone',
                     f'gamerule playersSleepingPercentage {original_sleep}',
                     f'gamerule doDaylightCycle {original_cycle}']:
            command(text)
        log.close()


if __name__ == '__main__':
    main()
