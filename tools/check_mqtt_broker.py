#!/usr/bin/env python3
"""Exercise the portable production MQTT bridge against a local MQTT 3.1.1 broker.

Requires the mqtt_host native target and Mosquitto at --port. No Python packages.
This covers MQTT traffic/retention; the ESP-MQTT FreeRTOS adapter needs hardware.
"""
import argparse
import json
import select
import socket
import struct
import subprocess
import time
from pathlib import Path


def string(text):
 b=text.encode();return struct.pack('!H',len(b))+b

def packet(kind,data):
 n=len(data);length=b''
 while True:
  value=n%128;n//=128;length+=bytes([value|(128 if n else 0)])
  if not n:return bytes([kind])+length+data

class Client:
 def __init__(self,port,name,will=False):
  self.socket=socket.create_connection(('127.0.0.1',port),timeout=3);self.socket.settimeout(3);self.socket.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
  self.messages=[];self.counter=0
  flags=2|(4|32 if will else 0)
  body=string('MQTT')+bytes([4,flags])+struct.pack('!H',30)+string(name)
  if will:body+=string('g5500/000000000042/availability')+string('offline')
  self.socket.sendall(packet(0x10,body));kind,data=self.read();assert kind==0x20 and data==b'\0\0'
 def exact(self,n):
  out=b''
  while len(out)<n:
   chunk=self.socket.recv(n-len(out))
   if not chunk:raise ConnectionError('MQTT closed')
   out+=chunk
  return out
 def read(self):
  kind=self.exact(1)[0];size=0;factor=1
  while True:
   b=self.exact(1)[0];size+=(b&127)*factor;factor*=128
   if not b&128:break
  return kind,self.exact(size)
 def subscribe(self,topic):
  self.counter+=1;self.socket.sendall(packet(0x82,struct.pack('!H',self.counter)+string(topic)+b'\0'))
  # SUBACK can be preceded by retained publications from another subscription.
  while True:
   kind,data=self.read()
   if kind>>4==9:assert data[-1]!=128;return
   self.accept(kind,data)
 def publish(self,topic,data,retain=False,qos=0):
  self.counter+=1;body=string(topic)
  if qos:body+=struct.pack('!H',self.counter)
  self.socket.sendall(packet(0x30|(2*qos)|bool(retain),body+data.encode()))
 def accept(self,kind,data):
  if kind>>4!=3:return
  n=struct.unpack('!H',data[:2])[0];topic=data[2:2+n].decode();at=2+n;qos=(kind>>1)&3
  if qos:self.socket.sendall(packet(0x40,data[at:at+2]));at+=2
  self.messages.append((topic,data[at:].decode(),bool(kind&1)))
 def poll(self,wait=0):
  while select.select([self.socket],[],[],wait)[0]:
   self.accept(*self.read());wait=min(wait,.02)
 def close(self):self.socket.close()

class Bridge:
 def __init__(self,executable,client):
  self.process=subprocess.Popen([str(executable)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,bufsize=1)
  self.client=client;self.commands=0;self.state=None;self.at=0
 def call(self,line):
  self.process.stdin.write(line+'\n');self.process.stdin.flush()
  while True:
   text=self.process.stdout.readline().rstrip('\n')
   if not text:raise RuntimeError('Bridge exited')
   fields=text.split('\t',4)
   if fields[0]=='D':self.commands=int(fields[1]);return fields[2]
   if fields[0]=='P':self.client.publish(fields[3],fields[4],bool(int(fields[2])),int(fields[1]))
   elif fields[0]=='S':self.state=json.loads(fields[1])
 def tick(self,at):
  self.at=at;self.client.poll()
  for topic,data,retain in self.client.messages:self.call(f'R {int(retain)} {at} {topic} {data}')
  self.client.messages.clear();self.call(f'T {at}')
 def run(self,duration=2500):
  end=self.at+duration
  while self.at<end:self.tick(self.at+10)
 def close(self):self.process.stdin.close();self.process.wait(timeout=5)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',type=Path,required=True);p.add_argument('--port',type=int,default=18884);a=p.parse_args()
 observer=Client(a.port,'g5500_verify_observer');observer.subscribe('homeassistant/#');observer.subscribe('g5500/#')
 gateway=Client(a.port,'g5500_verify_gateway',True);gateway.subscribe('g5500/000000000042/+/set/+');gateway.subscribe('homeassistant/status')
 bridge=Bridge(a.host,gateway)
 try:
  bridge.call('C 1');bridge.run();observer.poll(.1)
  configs={t:json.loads(v) for t,v,_ in observer.messages if t.endswith('/config') and v}
  assert len(configs)>=17, len(configs)
  assert all(c.get('retain',False) is False for c in configs.values())
  assert all(c['dev']['ids']==['g5500_000000000042_000000000042'] for c in configs.values())
  target='g5500/000000000042/000000000042/set/target_az'
  observer.publish(target,'70');time.sleep(.05);bridge.run(50);bridge.call('S');assert bridge.state['axes'][0]['target']==70
  assert bridge.commands==1
  previous=len(observer.messages);observer.publish('homeassistant/status','online');time.sleep(.05);bridge.run();observer.poll(.1)
  assert sum(t.endswith('/config') and bool(v) for t,v,_ in observer.messages[previous:])>=17
  before=bridge.commands;bridge.call('C 0');gateway.close();observer.poll(.2)
  assert any(t.endswith('/availability') and v=='offline' for t,v,_ in observer.messages)
  observer.publish(target,'90') # Nonretained offline commands are not replayed.
  observer.publish(target,'110',True) # Retained subscription replay must be rejected.
  gateway=Client(a.port,'g5500_verify_gateway',True);gateway.subscribe('g5500/000000000042/+/set/+');gateway.subscribe('homeassistant/status');bridge.client=gateway
  bridge.call('C 1');time.sleep(.05);bridge.run();assert bridge.commands==before
  bridge.call('S');assert bridge.state['axes'][0]['target']==70
  observer.publish(target,'',True) # Remove the intentionally bad retained test command.
  # A fresh observer receives the retained config/state tree.
  fresh=Client(a.port,'g5500_verify_fresh');fresh.subscribe('homeassistant/#');fresh.subscribe('g5500/#');fresh.poll(.1)
  assert any(t.endswith('/config') and retained for t,_,retained in fresh.messages)
  assert any(t.endswith('/state') and retained for t,_,retained in fresh.messages)
  fresh.close()
  print(f'PASS: Mosquitto discovery ({len(configs)} entities), state, command, HA birth, LWT, clean reconnect and retained-command rejection')
 finally:
  bridge.close();gateway.close();observer.close()
if __name__=='__main__':main()
