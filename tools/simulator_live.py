"""Bounded websocket adapter; commands/state are handled by the firmware C++ core."""
import asyncio
import base64
import hashlib
import json
import struct
import urllib.parse

class Live:
    def __init__(self, core):
        self.core = core
        self.clients = set()
        self.revision = 0
        self.last = None
        self.last_sent = 0.0
        self.sent_revision = {}
        self.modes = {}
        self.sent_metadata = {}
        self.metadata_revision = 0
        self.metadata_hash = None

    async def connection(self, reader, writer):
        try:
            if len(self.clients) >= 4:
                return
            headers = await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'), 1)
            if len(headers) > 4096 or not any(headers.startswith(b'GET '+path+b' HTTP/1.1\r\n') for path in (b'/api/live',b'/api/control')):
                return
            fields = dict(line.split(':', 1) for line in headers.decode().split('\r\n')[1:] if ':' in line)
            fields = {k.lower(): v.strip() for k, v in fields.items()}
            key = fields.get('sec-websocket-key', '')
            if not key:
                return
            accept = base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
            if len(self.clients) >= 4:
                return
            self.clients.add(writer)
            writer.write(f'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n'.encode())
            await writer.drain()
            last_id = 0
            while True:
                head = await reader.readexactly(2)
                if head[0] == 0x88:
                    return
                if head[0] != 0x81 or not head[1] & 128:
                    return
                size = head[1] & 127
                if size == 126:
                    size = struct.unpack('!H', await reader.readexactly(2))[0]
                elif size == 127:
                    return
                if size > 512:
                    return
                mask = await reader.readexactly(4)
                payload = await asyncio.wait_for(reader.readexactly(size), .5)
                command = bytes(c ^ mask[i % 4] for i, c in enumerate(payload)).decode()
                if command == 'v=2&telemetry=1':
                    self.modes[writer] = 'binary'
                    self.sent_revision.pop(writer, None)
                    self.sent_metadata.pop(writer, None)
                    continue
                if command == 'v=1&control=1':
                    self.modes[writer] = 'control'
                    self.sent_revision.pop(writer, None)
                    continue
                if command == 'v=1&ping=1':
                    await self.send(writer, b'', opcode=10)
                    continue
                form = urllib.parse.parse_qs(command)
                cid = int(form.get('cid', ['0'])[0])
                if cid <= last_id:
                    await self.result(writer, cid, {'ok': False, 'result': 'cancelled'})
                    continue
                last_id = cid
                result = self.core.call('live '+command)
                reply = json.loads(result['body'])
                await self.result(writer, cid, reply)
                if result['status'] == 202:
                    asyncio.create_task(self.deferred(writer, cid, reply['requestId']))
        except (ConnectionError, asyncio.IncompleteReadError, TimeoutError, ValueError, KeyError):
            pass
        finally:
            self.clients.discard(writer)
            self.sent_revision.pop(writer, None)
            self.sent_metadata.pop(writer, None)
            self.modes.pop(writer, None)
            writer.close()
            try:
                await writer.wait_closed()
            except ConnectionError:
                pass

    async def send(self, writer, message, opcode=1):
        data = message if isinstance(message, (bytes, bytearray)) else json.dumps(message, separators=(',', ':')).encode()
        if writer.transport.get_write_buffer_size() > 8192:
            writer.close()
            return
        header = bytes([0x80 | opcode, len(data)]) if len(data) < 126 else bytes([0x80 | opcode, 126])+struct.pack('!H', len(data))
        writer.write(header + data)
        await asyncio.wait_for(writer.drain(), .05)

    async def result(self, writer, cid, reply):
        state = json.loads(self.core.call('live_state')['body'])
        await self.send(writer, dict(v=1, type='result', boot=state['boot'], id=cid, result=reply))

    async def deferred(self, writer, cid, request):
        try:
            for _ in range(250):
                if writer not in self.clients:
                    return
                await asyncio.sleep(.005)
                reply = json.loads(self.core.call('http /api/hud/command id='+str(request))['body'])
                if reply['result'] != 'deferred':
                    await self.result(writer, cid, reply)
                    return
        except (ConnectionError, TimeoutError):
            writer.close()

    async def publish(self):
        if not self.clients:
            return
        state = json.loads(self.core.call('live_state')['body'])
        now = asyncio.get_running_loop().time()
        changed = not self.last or state['state'] != self.last['state']
        if not changed and now - self.last_sent < .25 and all(w in self.sent_revision for w in self.clients):
            return
        critical = not self.last or state['control'] != self.last['control']
        if not critical and now - self.last_sent < .2 and all(w in self.sent_revision for w in self.clients):
            return
        self.last, self.last_sent = state, now
        self.revision += 1
        message = dict(v=1, type='state', revision=self.revision, boot=state['boot'], state=state['state'])
        if self.metadata_hash != state['metadataHash']:
            self.metadata_hash = state['metadataHash']
            self.metadata_revision += 1
        metadata = dict(v=2, type='metadata', revision=self.metadata_revision, boot=state['boot'], state=state['metadata'])
        binary = bytearray.fromhex(state['binary'])
        struct.pack_into('<II', binary, 4, self.revision, self.metadata_revision)
        for writer in tuple(self.clients):
            try:
                mode = self.modes.get(writer, 'legacy')
                if mode == 'control':
                    await self.send(writer, dict(v=1, type='control', boot=state['boot'], selection=state['state'].get('hudSelectionEpoch',0), memoryLimited=False))
                elif mode == 'binary':
                    if self.sent_metadata.get(writer) != self.metadata_revision:
                        await self.send(writer, metadata)
                        self.sent_metadata[writer] = self.metadata_revision
                    await self.send(writer, binary, opcode=2)
                else:
                    await self.send(writer, message)
                self.sent_revision[writer] = self.revision
            except (ConnectionError, TimeoutError):
                writer.close()
