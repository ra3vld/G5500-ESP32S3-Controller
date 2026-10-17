#!/usr/bin/env python3
"""UDP/TCP/HTTP adapter for the actual fork C++ core, with virtual motors only."""
import argparse
import asyncio
import json
import pathlib
import subprocess
import time
import urllib.parse
from generate_web_assets import pack_html
from simulator_live import Live

ROOT = pathlib.Path(__file__).resolve().parents[1]

class Core:
    def __init__(self, log):
        self.process = subprocess.Popen([str(ROOT / 'build/simulator')], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, bufsize=1)
        self.log = log
        self.state = self.call('step 0')['state']
    def call(self, command):
        previous = getattr(self, 'state', {}).get('hudSelectionEpoch', 0)
        self.process.stdin.write(command + '\n'); self.process.stdin.flush()
        result = json.loads(self.process.stdout.readline())
        self.state = result['state']
        self.state['livePort'] = getattr(self, 'live_port', 0)
        if self.state.get('hudSelectionEpoch', 0) != previous and hasattr(self, 'selection_changed'):
            self.selection_changed()
        if self.log and not command.startswith('step '):
            with open(self.log, 'a', encoding='utf-8') as stream:
                stream.write(json.dumps({'time': time.time(), 'input': command, 'result': result}, ensure_ascii=False) + '\n')
        return result
    def packet(self, kind, data):
        # Preserve datagram bytes, including CR/LF; CP1251 metadata is accepted.
        text = data.decode('utf-8-sig', errors='replace')
        return self.call(kind + ' ' + urllib.parse.urlencode({'packet': text}))
    def network_log(self, direction, data, addr):
        if self.log:
            with open(self.log, 'a', encoding='utf-8') as stream:
                stream.write(json.dumps({'time': time.time(), 'direction': direction,
                    'peer': list(addr), 'packetHex': data.hex()}) + '\n')

class Udp(asyncio.DatagramProtocol):
    def __init__(self, core, peers, report_port, response_port):
        self.core, self.peers, self.report_port, self.response_port = core, peers, report_port, response_port
    def connection_made(self, transport): self.transport = transport
    def datagram_received(self, data, addr):
        if len(data) > 4096: return
        self.core.network_log('receive', data, addr)
        result = self.core.packet('udp', data)
        detail = json.loads(result['body'])
        if detail['matched'] and detail['result'] != 'invalid' and b'N1MMRotor' in data:
            self.peers[addr[0]] = time.monotonic()
            if len(self.peers) > 8: self.peers.pop(next(iter(self.peers)))
        if result['reply']:
            destination=(addr[0], self.core.state['pstPort']+1)
            data=result['reply'].encode();self.transport.sendto(data, destination)
            self.core.network_log('send', data, destination)

async def main(args):
    core = Core(args.log)
    core.live_port = args.live_port or args.http_port + 1
    live = Live(core)
    core.call('http /api/settings ' + urllib.parse.urlencode({'n1mmPort': args.n1mm_port, 'pstPort': args.pst_port, 'reportPort': args.report_port, 'rotorName': args.name}))
    if args.hud_port:
        core.call('hud_init '+urllib.parse.urlencode({'role':args.role,'port':args.hud_port,'id':args.device_id}))
    peers = {}; loop = asyncio.get_running_loop(); transports = []; tcp_sessions = {}
    def selection_changed():
        peers.clear()
        for context in tcp_sessions.values():
            context['discarding'] = context['discarding'] or bool(context['pending'])
            context['pending'].clear()
            # StreamReader already owns received bytes. Snapshot its unread
            # buffer at the same boundary as firmware's socket FIONREAD.
            context['stale'] = len(context['reader']._buffer)
    core.selection_changed = selection_changed
    for port in (args.n1mm_port, args.pst_port):
        transport, _ = await loop.create_datagram_endpoint(lambda: Udp(core, peers, args.report_port, args.pst_port+1), local_addr=(args.host, port))
        transports.append(transport)
    async def tick():
        last = time.monotonic(); report = last
        while True:
            await asyncio.sleep(.005)
            now = time.monotonic(); core.call('step ' + str(max(0, round((now-last)*1000)))); last = now
            await live.publish()
            if now-report >= .5 and (core.state.get("deviceRole")!="hud" or core.state.get("hudOnline")):
                report = now
                message = core.state['n1mmReport'].encode()
                hosts = set(peers)
                if core.state['reportHost']: hosts.add(core.state['reportHost'])
                for host in hosts: transports[0].sendto(message, (host, core.state['reportPort']))
                for host in hosts: core.network_log('send', message, (host, core.state['reportPort']))
    async def tcp(reader, writer):
        pending = bytearray()
        context = dict(reader=reader, pending=pending, discarding=False, stale=0)
        tcp_sessions[writer] = context
        try:
            while True:
                data = await reader.read(256)
                if not data: break
                epoch = core.state.get('hudSelectionEpoch', 0)
                for byte in data:
                    # A selection may change while a previous command waits
                    # for its ACK. Its unread chunk must not migrate either.
                    old_chunk = epoch != core.state.get('hudSelectionEpoch', 0)
                    if old_chunk or context['stale']:
                        if not old_chunk: context['stale'] -= 1
                        pending.clear()
                        context['discarding'] = byte not in (10, 13)
                        continue
                    if byte in (10, 13):
                        if context['discarding']:
                            context['discarding'] = False
                            continue
                        if not pending: continue
                        text=bytes(pending); pending.clear()
                        if text.strip() in (b'q', b'\\quit'): return
                        result=core.packet('line',text)
                        detail=json.loads(result['body'])
                        if detail.get('result')=='deferred':
                            request=detail['requestId'];deadline=time.monotonic()+2
                            while time.monotonic()<deadline:
                                await asyncio.sleep(.02)
                                outcome=json.loads(core.call('http /api/hud/command id='+str(request))['body'])
                                if outcome['result']!='deferred':break
                            success=outcome.get('result') in ('ok','pending')
                            ham=result['reply'].startswith('RPRT ')
                            result['reply']=('RPRT 0\n' if success else 'RPRT -8\n' if outcome.get('result')=='ignored' else 'RPRT -1\n') if ham else ('' if success or outcome.get('result')=='ignored' else '?>\r\n')
                        writer.write((result['reply'] or ('RPRT -11\n' if not detail['matched'] else '')).encode())
                        await writer.drain()
                    else:
                        if context['discarding']: continue
                        pending.append(byte)
                        if len(pending)>512: return
        except (ConnectionError, ValueError): pass
        finally:
            tcp_sessions.pop(writer,None)
            writer.close()
            try: await writer.wait_closed()
            except ConnectionError: pass
    async def http(reader, writer):
        status = 200; content_type='application/json'; body=b''; extra_headers=''
        try:
            request = await asyncio.wait_for(reader.readline(), 5)
            method, url, _ = request.decode().strip().split(' ', 2)
            headers={}
            while True:
                header=await asyncio.wait_for(reader.readline(),5)
                if header in (b'\r\n', b'\n', b''): break
                key,value=header.decode().split(':',1);headers[key.lower()]=value.strip()
            length=int(headers.get('content-length',0))
            if not 0<=length<=8192: raise ValueError('body size')
            payload=await asyncio.wait_for(reader.readexactly(length),5)
            path, _, query=url.partition('?')
            if path=='/':
                content_type='text/html; charset=utf-8'
                source=(ROOT/'G5500_Fork/web_page.hpp').read_text()
                body=source.split('R"HTML(',1)[1].split(')HTML";',1)[0].encode()
                extra_headers='Vary: Accept-Encoding\r\n'
                if 'gzip' in [part.strip().lower() for part in headers.get('accept-encoding','').split(',')]:
                    body=pack_html(body);extra_headers+='Content-Encoding: gzip\r\n'
            elif path=='/api/status': body=json.dumps(core.state).encode()
            elif path=='/api/rssi': body=b'{"values":[],"min":null,"max":null,"avg":null,"count":0}'
            elif path=='/api/diaglog': content_type='text/plain';body=b'Host simulator: shared firmware core; virtual calibrated sensors.'
            elif path=='/api/sim/encoder' and method=='POST':
                delta=int(urllib.parse.parse_qs(payload.decode()).get('delta',['0'])[0])
                if not -20<=delta<=20: raise ValueError('encoder range')
                body=core.call('encoder '+str(delta))['body'].encode()
            elif path=='/api/sim/step' and method=='POST':
                form=urllib.parse.parse_qs(payload.decode());ms=int(form.get('ms',['0'])[0])
                if not 0<=ms<=3600000: raise ValueError('step range')
                body=json.dumps(core.call('step '+str(ms))['state']).encode()
            else:
                old_ports=(core.state['n1mmPort'],core.state['pstPort'])
                result=core.call('http '+path+' '+(payload.decode() if method=='POST' else query))
                status=result['status'];body=(result['body'] or '{"ok":true}').encode()
                new_ports=(core.state['n1mmPort'],core.state['pstPort'])
                if status==200 and old_ports!=new_ports:
                    # Keep old listeners until every newly requested port is available.
                    created={}
                    try:
                        for port in new_ports:
                            if port not in old_ports:
                                transport,_=await loop.create_datagram_endpoint(
                                    lambda: Udp(core,peers,args.report_port,new_ports[1]+1),
                                    local_addr=(args.host,port))
                                created[port]=transport
                    except OSError:
                        for transport in created.values(): transport.close()
                        core.call('http /api/settings '+urllib.parse.urlencode({'n1mmPort':old_ports[0],'pstPort':old_ports[1]}))
                        status=400;body=b'{"ok":false,"result":"port_in_use"}'
                    else:
                        retained=dict(zip(old_ports,transports))
                        for port,transport in retained.items():
                            if port not in new_ports: transport.close()
                        transports[:]=[retained[port] if port in retained else created[port] for port in new_ports]
        except (ValueError, TimeoutError, asyncio.IncompleteReadError): status=400;body=b'{"ok":false}'
        writer.write(f'HTTP/1.1 {status} Result\r\nContent-Type: {content_type}\r\nContent-Length: {len(body)}\r\n{extra_headers}Connection: close\r\n\r\n'.encode()+body)
        try: await writer.drain()
        except ConnectionError: pass
        writer.close();await writer.wait_closed()
    servers=[await asyncio.start_server(live.connection,args.host,core.live_port),await asyncio.start_server(http,args.host,args.http_port),await asyncio.start_server(tcp,args.host,args.tcp_port,limit=1024)]
    print(f'Virtual motors ONLY: HTTP {args.http_port}, TCP {args.tcp_port}, N1MM {args.n1mm_port}, PstRotator UDP {args.pst_port}',flush=True)
    ticker=asyncio.create_task(tick())
    try: await asyncio.Future()
    finally:
        ticker.cancel()
        for server in servers: server.close();await server.wait_closed()
        for transport in transports: transport.close()
        core.process.terminate();core.process.wait()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',default='127.0.0.1');parser.add_argument('--http-port',type=int,default=8080)
    parser.add_argument('--live-port',type=int,default=0)
    parser.add_argument('--tcp-port',type=int,default=4533);parser.add_argument('--n1mm-port',type=int,default=12040)
    parser.add_argument('--pst-port',type=int,default=1234);parser.add_argument('--report-port',type=int,default=13010)
    parser.add_argument('--name',default='G5500');parser.add_argument('--log')
    parser.add_argument('--role',choices=['controller','hud'],default='controller')
    parser.add_argument('--device-id',default='1',help='Unique hexadecimal HUD device identity')
    parser.add_argument('--hud-port',type=int,default=0,help='Enable the actual HUD/1 UDP transport on this port')
    try: asyncio.run(main(parser.parse_args()))
    except KeyboardInterrupt: pass
