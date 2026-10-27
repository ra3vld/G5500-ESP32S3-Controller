#!/usr/bin/env python3
"""Check production MQTT commands through an isolated Home Assistant REST API.

Use only a disposable HA/broker with the mqtt_host native bridge target.
Credentials are read from a token file and never logged.
"""
import argparse
import json
from pathlib import Path
import time
import urllib.request
from check_mqtt_broker import Client,Bridge

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',type=Path,required=True);p.add_argument('--port',type=int,default=18884)
 p.add_argument('--ha',default='http://127.0.0.1:18123');p.add_argument('--token',type=Path,required=True)
 p.add_argument('--discovery-dump',type=Path,help='Controller and HUD --dump/--dump-hud JSON pairs from mqtt_tests');a=p.parse_args()
 token=a.token.read_text().strip()
 def api(path,data=None):
  headers={'Authorization':'Bearer '+token}
  if data is not None:headers['Content-Type']='application/json';data=json.dumps(data).encode()
  with urllib.request.urlopen(urllib.request.Request(a.ha+path,data=data,headers=headers),timeout=10) as r:
   body=r.read().decode();return body if path=='/api/template' else json.loads(body)
 client=Client(a.port,'g5500_verify_gateway',True);client.subscribe('g5500/000000000042/+/set/+');client.subscribe('homeassistant/status')
 bridge=Bridge(a.host,client)
 def states():return {s['entity_id']:s for s in api('/api/states')}
 def wait_state(entity,expected):
  for _ in range(100):
   bridge.run(30);time.sleep(.03);s=states().get(entity)
   if s and s['state']==str(expected):return
  raise AssertionError((entity,expected,states().get(entity)))
 def service(domain,name,entity,**fields):
  api('/api/services/'+domain+'/'+name,dict(entity_id=entity,**fields));time.sleep(.04);bridge.run(50)
 try:
  bridge.call('C 1');bridge.run();wait_state('sensor.g5500_azimut','20.0')
  service('number','set_value','number.g5500_tsel_az',value=65);bridge.call('S');assert bridge.state['axes'][0]['target']==65
  service('select','select_option','select.g5500_rezhim',option='manual');bridge.call('S');assert bridge.state['controlMode']=='manual'
  service('number','set_value','number.g5500_tsel_el',value=55);bridge.call('S');assert bridge.state['pendingValid'] and bridge.state['pending']['el']==55
  wait_state('binary_sensor.g5500_ozhidaet_podtverzhdeniia','on')
  service('button','press','button.g5500_podtverdit_tsel');bridge.call('S');assert not bridge.state['pendingValid'] and bridge.state['axes'][1]['target']==55
  service('button','press','button.g5500_stop');bridge.call('S');assert not bridge.state['moving']
  service('select','select_option','select.g5500_rezhim',option='auto_1h');bridge.call('S');assert bridge.state['controlMode']=='auto_1h'
  print('PASS: real Home Assistant discovery/state and HA services → Mosquitto → production bridge: target, manual pending, execute, STOP and mode')
  if a.discovery_dump:
   messages=json.loads(a.discovery_dump.read_text());latest=dict(messages)
   for topic,payload in latest.items():client.publish(topic,payload,True)
   client.publish('g5500/000000000042/availability','online',True);client.poll(.1)
   first='number.first_tsel_az';second='number.second_tsel_az'
   selected='number.tekushchaia_vybrannaia_povorotka_tsel_az'
   for _ in range(100):
    all_states=states()
    if all(e in all_states and all_states[e]['state']!='unavailable' for e in (first,second,selected)):break
    time.sleep(.05)
   else:raise AssertionError({e:all_states.get(e) for e in (first,second,selected)})
   devices=[api('/api/template',{'template':"{{ device_id('"+e+"') }}"}) for e in (first,second,selected)]
   assert len(set(devices))==3 and all(d not in ('None','') for d in devices),devices
   first_state='g5500/000000000042/000000000001/state'
   stale=json.loads(latest[first_state]);stale['available']=False;client.publish(first_state,json.dumps(stale),True)
   for _ in range(100):
    all_states=states()
    if all_states[first]['state']=='unavailable':break
    time.sleep(.05)
   else:raise AssertionError('First peer did not become unavailable')
   assert all_states[second]['state']!='unavailable'
   for topic,payload in latest.items():
    if topic.endswith('/config') and '/000000000001_' in topic:client.publish(topic,'',True)
   for _ in range(100):
    all_states=states()
    if first not in all_states:break
    time.sleep(.05)
   else:raise AssertionError('Deleted peer entities remain in HA')
   assert second in all_states and selected in all_states
   print('PASS: real HA separate catalog devices and current selection, individual freshness and discovery tombstones')
 finally:bridge.close();client.close()
if __name__=='__main__':main()
