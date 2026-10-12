import json
import pathlib
import socket
import subprocess
import time
import unittest
import urllib.parse
import urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[1]

def free(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET,kind) as s:
        s.bind(('127.0.0.1',0));return s.getsockname()[1]

class NetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.http,cls.tcp=free(),free()
        cls.n1,cls.pst,cls.report=free(socket.SOCK_DGRAM),free(socket.SOCK_DGRAM),free(socket.SOCK_DGRAM)
        cls.process=subprocess.Popen(['python3',str(ROOT/'tools/simulator.py'),'--http-port',str(cls.http),'--tcp-port',str(cls.tcp),'--n1mm-port',str(cls.n1),'--pst-port',str(cls.pst),'--report-port',str(cls.report)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        for _ in range(100):
            try: cls.get('/api/status');break
            except (OSError,ValueError):
                if cls.process.poll() is not None:raise RuntimeError(cls.process.stderr.read())
                time.sleep(.03)
        else:raise RuntimeError('simulator startup timeout')
    @classmethod
    def tearDownClass(cls):
        cls.process.terminate();cls.process.wait(timeout=5)
        cls.process.stdout.close();cls.process.stderr.close()
    @classmethod
    def get(cls,path):
        with urllib.request.urlopen(f'http://127.0.0.1:{cls.http}{path}',timeout=2) as r:return json.load(r)
    @classmethod
    def post(cls,path,**data):
        request=urllib.request.Request(f'http://127.0.0.1:{cls.http}{path}',urllib.parse.urlencode(data).encode())
        with urllib.request.urlopen(request,timeout=2) as r:return json.load(r)
    def setUp(self):
        self.post('/api/move',d='S');self.post('/api/settings',model='G5500');self.post('/api/mode',mode='manual')
    def udp(self,packet,port=None):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:s.sendto(packet.encode(),('127.0.0.1',port or self.n1))
        time.sleep(.04)
    def test_manual_numeric_jog_stop_and_execute(self):
        self.udp('<N1MMRotor><rotor>G5500</rotor><goazi>123</goazi></N1MMRotor>')
        s=self.get('/api/status');self.assertTrue(s['pendingValid']);self.assertEqual(s['pending']['az'],123);self.assertEqual(s['directionAz'],0)
        self.udp('<PST><RIGHT>1</RIGHT></PST>',self.pst)
        s=self.get('/api/status');self.assertEqual(s['directionAz'],1);self.assertEqual(s['pending']['az'],123)
        self.post('/api/execute');self.assertFalse(self.get('/api/status')['pendingValid'])
        self.udp('<PST><STOPR>1</STOPR></PST>',self.pst)
        self.assertEqual(self.get('/api/status')['directionAz'],0)
    def test_n1mm_position_report(self):
        self.post('/api/target',az=137)
        self.post('/api/sim/step',ms=60000)
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
            listener.bind(('127.0.0.1',self.report));listener.settimeout(2)
            self.udp('<N1MMRotor><rotor>G5500</rotor><goazi>180</goazi></N1MMRotor>')
            message=listener.recv(200).decode();self.assertEqual(message,'G5500@1370')
    def test_pst_reply_destination(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
            listener.bind(('127.0.0.1',self.pst+1));listener.settimeout(2)
            self.udp('<PST>AZ?</PST>',self.pst)
            self.assertRegex(listener.recv(200).decode(),r'^AZ:\d+\.\d\r$')
    def test_preset_discards_incoming_but_accepts_stop(self):
        self.post('/api/mode',mode='auto');self.post('/api/preset',slot=1,label='Тест',az=270,el=0)
        self.post('/api/preset/run',slot=1)
        self.udp('<PST><AZIMUTH>90</AZIMUTH></PST>',self.pst)
        self.udp('<PST><LEFT>1</LEFT></PST>',self.pst)
        s=self.get('/api/status');self.assertEqual(s['targetAz'],270);self.assertFalse(s['pendingValid']);self.assertTrue(s['presetActive'])
        self.udp('<PST><STOP>1</STOP></PST>',self.pst)
        s=self.get('/api/status');self.assertFalse(s['presetActive']);self.assertEqual(s['directionAz'],0);self.assertEqual(s['controlMode'],'auto')
    def test_hour_expires(self):
        self.post('/api/mode',mode='auto_1h');self.post('/api/sim/step',ms=3600000)
        self.assertEqual(self.get('/api/status')['controlMode'],'manual')
    def test_tcp_fragmentation_and_shared_state(self):
        with socket.create_connection(('127.0.0.1',self.tcp),timeout=2) as s:
            s.sendall(b'P 1');s.sendall(b'80 0\n');self.assertEqual(s.recv(200),b'RPRT 0\n')
            self.assertEqual(self.get('/api/status')['pending']['az'],180)
            s.sendall(b'p\n');self.assertEqual(len(s.recv(200).splitlines()),2)
            s.sendall(b'S\n');self.assertEqual(s.recv(200),b'RPRT 0\n');self.assertFalse(self.get('/api/status')['pendingValid'])
            s.sendall(b'M090\r');time.sleep(.04);self.assertEqual(self.get('/api/status')['pending']['az'],90)
    def test_profile_presets_are_separate(self):
        self.post('/api/preset',slot=2,label='G5500',az=90,el=45)
        self.post('/api/settings',model='G800DXA');self.post('/api/preset',slot=2,label='G800',az=180)
        s=self.get('/api/status');self.assertEqual(s['el'],0);self.assertFalse(s['presets'][1]['target']['hasEl'])
        self.post('/api/settings',model='G5500');s=self.get('/api/status');self.assertEqual(s['presets'][1]['label'],'G5500');self.assertEqual(s['presets'][1]['target']['el'],45)
    def test_invalid_packet_is_atomic(self):
        self.udp('<PST><AZIMUTH>90</AZIMUTH></PST>',self.pst)
        self.udp('<PST><AZIMUTH>180</AZIMUTH><BAD>1</BAD></PST>',self.pst)
        self.assertEqual(self.get('/api/status')['pending']['az'],90)
    def test_live_udp_port_change(self):
        port=free(socket.SOCK_DGRAM)
        try:
            self.post('/api/settings',pstPort=port)
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as listener:
                listener.bind(('127.0.0.1',port+1));listener.settimeout(2)
                self.udp('<PST>AZ?</PST>',port)
                self.assertRegex(listener.recv(200).decode(),r'^AZ:\d+\.\d\r$')
        finally:self.post('/api/settings',pstPort=self.pst)
    def test_speed_ramp_and_immediate_stop(self):
        values=dict(model='G800DXA',speedEnabled='true',speedPercent=75,
                    startSpeedPercent=25,approachSpeedPercent=25,
                    accelerationMs=2000,decelerationMs=1500,slowdownDegrees=10)
        self.post('/api/settings',**values)
        state=self.get('/api/status')
        for key in ('speedPercent','startSpeedPercent','approachSpeedPercent','accelerationMs','decelerationMs','slowdownDegrees'):
            self.assertEqual(state[key],values[key])
        self.post('/api/move',d='R' if state['az']<225 else 'L')
        self.post('/api/sim/step',ms=250)
        ramp=self.get('/api/status')['speedOutputPercent']
        self.assertGreater(ramp,25);self.assertLess(ramp,75)
        self.post('/api/sim/step',ms=2000)
        self.assertEqual(self.get('/api/status')['speedOutputPercent'],75)
        self.post('/api/move',d='S')
        state=self.get('/api/status')
        self.assertEqual(state['speedOutputPercent'],0);self.assertEqual(state['directionAz'],0)
    def test_speed_ramp_target_approach(self):
        self.post('/api/settings',model='G800DXA',speedEnabled='true',speedPercent=75,
                  startSpeedPercent=25,approachSpeedPercent=25,
                  accelerationMs=100,decelerationMs=100,slowdownDegrees=10)
        az=self.get('/api/status')['az'];target=az+10 if az<430 else az-10
        self.post('/api/target',az=target)
        self.post('/api/sim/step',ms=500)
        first=self.get('/api/status');self.assertNotEqual(first['directionAz'],0)
        self.post('/api/sim/step',ms=2000)
        approach=self.get('/api/status')
        self.assertGreater(approach['speedOutputPercent'],25)
        self.assertLess(approach['speedOutputPercent'],first['speedOutputPercent'])
        self.post('/api/sim/step',ms=10000)
        stopped=self.get('/api/status')
        self.assertEqual(stopped['speedOutputPercent'],0);self.assertEqual(stopped['directionAz'],0)
        self.assertLessEqual(abs(stopped['az']-target),0.51)

if __name__=='__main__':unittest.main()
