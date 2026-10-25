#!/usr/bin/env python3
"""Wire-level fixtures: test real TCP fragmentation, compression and replies.
These fixtures do not substitute for a vanilla/Paper compatibility test.
"""
import json
import socket
import struct
import subprocess
import sys
import threading
import zlib

PROBE = sys.argv[1]

def vi(n):
    data = bytearray()
    while True:
        b = n & 127
        n >>= 7
        data.append(b | (128 if n else 0))
        if not n:
            return bytes(data)

def read_exact(s, n):
    data = b''
    while len(data) < n:
        part = s.recv(n-len(data))
        if not part:
            raise EOFError('client closed unexpectedly')
        data += part
    return data

def read_vi(s):
    n = 0
    for shift in range(0,35,7):
        b = read_exact(s,1)[0]
        n |= (b & 127) << shift
        if not b & 128:
            return n
    raise ValueError('VarInt overflow')

def packet(s, threshold=-1):
    raw = read_exact(s,read_vi(s))
    if threshold >= 0:
        n, pos = decode_vi(raw)
        raw = zlib.decompress(raw[pos:]) if n else raw[pos:]
    return raw

def decode_vi(raw):
    n = 0
    for i,b in enumerate(raw[:5]):
        n |= (b & 127) << (7*i)
        if not b & 128:
            return n,i+1
    raise ValueError('VarInt overflow')

def send(s, p, threshold=-1, fragmented=False):
    if threshold >= 0:
        p = vi(len(p))+zlib.compress(p) if len(p) >= threshold else b'\x00'+p
    wire = vi(len(p))+p
    if fragmented:
        for b in wire:
            s.sendall(bytes([b]))
    else:
        s.sendall(wire)

def string(s):
    b = s.encode()
    return vi(len(b))+b

def run(mode, handler, success=True):
    listener = socket.socket()
    listener.bind(('127.0.0.1',0))
    listener.listen()
    errors = []
    def server():
        try:
            s,_ = listener.accept()
            with s:
                s.settimeout(5)
                handshake = packet(s)
                assert handshake[:3] == b'\x00\xfa\x05', handshake
                handler(s)
        except BaseException as e:
            errors.append(e)
        finally:
            listener.close()
    worker = threading.Thread(target=server,daemon=True)
    worker.start()
    result = subprocess.run([PROBE, mode, '127.0.0.1', str(listener.getsockname()[1]), 'VitaPlayer', '1'], capture_output=True,text=True,timeout=10)
    worker.join(6)
    assert not worker.is_alive(), 'server fixture hung'
    assert not errors, errors
    assert (result.returncode == 0) == success, result.stdout+result.stderr
    return result.stdout

def status(s):
    assert packet(s) == b'\x00'
    send(s,b'\x00'+string(json.dumps({'version':{'name':'1.19.4','protocol':762},'players':{'max':20,'online':0},'description':{'text':'Test server'}})),fragmented=True)
    ping = packet(s)
    assert ping[0] == 1 and len(ping) == 9
    send(s,ping,fragmented=True)

assert 'Status and ping OK' in run('status',status)

def login(s, threshold):
    assert packet(s) == b'\x00'+string('VitaPlayer')+b'\x00'
    if threshold >= 0:
        send(s,b'\x03'+vi(threshold),fragmented=True)
    send(s,b'\x02'+bytes(16)+string('VitaPlayer')+vi(1)+string('test')+string('value')+b'\x01'+string('signature'),threshold,True)
    # A client must wait for Join Game before sending its play settings.
    import select
    assert not select.select([s], [], [], 0.05)[0], 'sent play packet before Join Game'
    send(s,b'\x28'+bytes(1000),threshold)
    assert packet(s,threshold) == b'\x08\x05en_us\x02\x00\x01\x7f\x01\x00\x01' 
    send(s,b'\x24'+bytes(4096),threshold)
    send(s,b'\x3c'+struct.pack('>dddffB',10.,64.,-5.,90.,0.,0)+vi(42),threshold,True)
    assert packet(s,threshold) == b'\x00'+vi(42)
    reply = packet(s,threshold)
    assert reply == b'\x15'+struct.pack('>dddffB',10.,64.,-5.,90.,0.,0)
    send(s,b'\x3c'+struct.pack('>dddffB',1.,2.,3.,10.,5.,31)+vi(43),threshold)
    assert packet(s,threshold) == b'\x00'+vi(43)
    assert packet(s,threshold) == b'\x15'+struct.pack('>dddffB',11.,66.,-2.,100.,5.,0)
    send(s,b'\x23'+struct.pack('>q',123456789012345),threshold)
    assert packet(s,threshold) == b'\x12'+struct.pack('>q',123456789012345)
    send(s,b'\x32'+struct.pack('>i',123),threshold)
    assert packet(s,threshold) == b'\x20'+struct.pack('>i',123)
    assert s.recv(1) == b''  # probe closes after its test duration

for threshold in (-1,0,256):
    output = run('login',lambda s:login(s,threshold))
    assert 'joined=1' in output and 'chunks=1' in output and 'keepalives=1' in output and 'teleports=2' in output, output

def auth(s):
    packet(s)
    send(s,b'\x01')
assert 'Microsoft authentication' in run('login',auth,False)

def disconnect(s):
    packet(s)
    send(s,b'\x00'+string('{"text":"Whitelist required"}'))
assert 'Whitelist required' in run('login',disconnect,False)

def oversized(s):
    packet(s)
    s.sendall(vi(2*1024*1024+1))
assert 'oversized' in run('login',oversized,False)

def bad_zlib(s):
    packet(s)
    send(s,b'\x03\x00')
    s.sendall(b'\x05\x10oops')
assert 'Invalid zlib' in run('login',bad_zlib,False)

def truncated(s):
    packet(s)
    s.sendall(b'\x10\x02abc')
assert 'Truncated' in run('login',truncated,False)
print('TCP fixtures passed: status/pong, fragmentation, all compression modes, login, relative teleports, keepalive, auth rejection, disconnect, oversized/truncated/corrupt packets')
