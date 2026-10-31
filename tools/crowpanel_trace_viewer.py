#!/usr/bin/env python3
"""Replay CrowPanel traces through the actual firmware LVGL renderer.

No GUI Python packages are needed. The browser only displays PNG frames produced
by the native renderer; every control and glyph on the display comes from the
firmware's CrowPanelUi and embedded fonts.
"""
from __future__ import annotations

import argparse
import contextlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import urllib.parse
import webbrowser
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from crowpanel_trace_core import load_recording

ROOT = Path(__file__).resolve().parent.parent
WIDTH = HEIGHT = 480
FRAME_BYTES = WIDTH * HEIGHT * 3
INPUT_SIZE = 600
REAL_FIELDS = frozenset(("current", "target", "minimum", "maximum", "secondaryCurrent", "secondaryTarget"))
NULLABLE_REAL_FIELDS = REAL_FIELDS - {"minimum", "maximum"}
BOOL_FIELDS = frozenset((
    "movingNow", "settingsAvailable", "enabled", "hud", "hudSelector", "dualAxis", "speedScreen", "speedAvailable[0]", "speedAvailable[1]",
    "elevation", "feedback", "targetValid", "uncalibrated", "secondaryUncalibrated", "pending",
    "moving", "autoMode", "autoHour", "secondaryFeedback", "secondaryTargetValid", "secondaryPending",
    "wifiConnected", "wifiAp", "mqttEnabled", "mqttConnected", "bluetoothEnabled", "bluetoothConnected", "statusFault"))
INT_FIELDS = frozenset(("direction", "azDirection", "elDirection", "wifiRssi"))
UINT_FIELDS = frozenset(("speedPercent[0]", "speedPercent[1]"))
TEXT_FIELDS = frozenset(("hudName", "hudAddress", "hudRows[0]", "hudRows[1]", "hudRows[2]", "ip", "heap", "status"))
ARRAY_LENGTHS = {"hudRows": 3, "speedAvailable": 2, "speedPercent": 2}


def renderer_path(explicit: str | None = None) -> Path:
    if explicit:
        path = Path(explicit).expanduser().resolve()
        if not path.is_file():
            raise ValueError(f"Renderer not found: {path}")
        return path
    build_dir = Path(os.environ.get("TRACE_BUILD_DIR", "build/crowpanel-trace")).expanduser()
    path = (ROOT / build_dir / "renderer").resolve()
    sources = [ROOT / "tools/crowpanel_trace_renderer.cpp", ROOT / "tools/crowpanel_trace_inputs.hpp",
               ROOT / "tools/build_crowpanel_trace_renderer.sh"]
    sources += list((ROOT / "G5500_Fork").glob("crowpanel_*.?pp"))
    sources += list((ROOT / "G5500_Fork").glob("crowpanel_*.h"))
    sources += list((ROOT / "G5500_Fork").glob("panel_*.?pp"))
    sources += [ROOT / "G5500_Fork/lv_conf.h", ROOT / "G5500_Fork/build_config.hpp"]
    if not path.is_file() or any(p.stat().st_mtime > path.stat().st_mtime for p in sources):
        result = subprocess.check_output(["sh", str(ROOT / "tools/build_crowpanel_trace_renderer.sh")], cwd=ROOT)
        path = Path(result.decode("utf-8").strip())
    return path


def view_command(time_ms: int, record: dict) -> bytes:
    """Encode the private native protocol, with exact uint64 and UTF-8 strings."""
    fields = ["VIEW", str(time_ms), str(record.get("pressed", 0))]

    def append(key, value):
        if key in ARRAY_LENGTHS:
            if not isinstance(value, list) or len(value) != ARRAY_LENGTHS[key]:
                raise ValueError(f"Invalid view array: {key}")
            for i, item in enumerate(value):
                append(f"{key}[{i}]", item)
        elif key in TEXT_FIELDS and isinstance(value, str):
            fields.append(key + "=" + value.encode("utf-8").hex())
        elif key in BOOL_FIELDS and type(value) is bool:
            fields.append(key + "=" + str(int(value)))
        elif value is None and key in NULLABLE_REAL_FIELDS:
            fields.append(key + "=null")
        elif key in REAL_FIELDS and type(value) in (int, float):
            if not -3.4028234663852886e38 <= value <= 3.4028234663852886e38:
                raise ValueError(f"View field exceeds the firmware float range: {key}")
            fields.append(key + "=" + str(value))
        elif key in INT_FIELDS and type(value) is int and -(1 << 31) <= value < (1 << 31):
            fields.append(key + "=" + str(value))
        elif key in UINT_FIELDS and type(value) is int and 0 <= value < (1 << 32):
            fields.append(key + "=" + str(value))
        elif key == "sourceId":
            if isinstance(value, str) and value.isascii() and value.isdecimal():
                value = int(value)
            if type(value) is not int or not 0 <= value < (1 << 64):
                raise ValueError("Invalid uint64 sourceId")
            fields.append(key + "=" + str(value))
        else:
            raise ValueError(f"Invalid or unsupported view field: {key}")

    for key, value in record["view"].items():
        append(key, value)
    return ("\t".join(fields) + "\n").encode("ascii")


def input_command(time_ms: int, record: dict) -> bytes | None:
    """Only recorded, well-formed touch/knob samples drive presentation."""
    value = record.get("input", {})
    kind = value.get("kind")
    if kind not in ("touch", "knob"):
        return None
    healthy, down = value.get("healthy"), value.get("down")
    if type(healthy) is not bool or type(down) is not bool:
        return f"CLEAR_INPUTS\t{time_ms}\n".encode("ascii")
    prefix = f"{time_ms}\t{int(healthy)}\t{int(down)}"
    if kind == "touch":
        x, y = value.get("x"), value.get("y")
        if type(x) is not int or type(y) is not int or not 0 <= x < WIDTH or not 0 <= y < HEIGHT:
            return f"CLEAR_INPUTS\t{time_ms}\n".encode("ascii")
        return f"TOUCH\t{prefix}\t{x}\t{y}\n".encode("ascii")
    delta = value.get("delta")
    if type(delta) is not int or not -(1 << 31) <= delta < (1 << 31):
        return f"CLEAR_INPUTS\t{time_ms}\n".encode("ascii")
    return f"KNOB\t{prefix}\t{delta}\n".encode("ascii")


class NativeRenderer:
    def __init__(self, executable: Path):
        self.errors = tempfile.TemporaryFile()
        self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.errors)

    def error(self):
        self.errors.seek(0)
        return self.errors.read().decode("utf-8", "replace").strip() or "Native renderer stopped"

    def command(self, command: bytes):
        try:
            self.process.stdin.write(command)
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as error:
            self.process.wait(timeout=5)
            raise RuntimeError(self.error()) from error

    def reset(self):
        self.command(b"RESET\n")

    def update(self, time_ms: int, record: dict):
        self.command(view_command(time_ms, record))

    def pressed(self, time_ms: int, action: int):
        self.command(f"PRESSED\t{time_ms}\t{action}\n".encode("ascii"))

    def input(self, time_ms: int, record: dict):
        command = input_command(time_ms, record)
        if command:
            self.command(command)

    def clear_inputs(self, time_ms: int):
        self.command(f"CLEAR_INPUTS\t{time_ms}\n".encode("ascii"))

    def frame(self, time_ms: int, show_inputs: bool = False) -> bytes:
        self.command(f"{'FRAME_INPUTS' if show_inputs else 'FRAME'}\t{time_ms}\n".encode("ascii"))
        expected = INPUT_SIZE * INPUT_SIZE * 3 if show_inputs else FRAME_BYTES
        output = bytearray()
        while len(output) < expected:
            chunk = self.process.stdout.read(expected - len(output))
            if not chunk:
                raise RuntimeError(self.error())
            output.extend(chunk)
        return bytes(output)

    def close(self):
        with contextlib.suppress(BrokenPipeError, OSError):
            self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdout.close()
        self.errors.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Replay:
    """Apply every state at its recorded time, including states between frames."""
    def __init__(self, recording, renderer: NativeRenderer):
        self.recording = recording
        self.renderer = renderer
        self.records = recording.records
        self.index = 0
        self.position = -1
        self.session = None
        self.session_start = 0
        self.sequence = None
        self.dropped = None

    def frame(self, requested_ms: float, show_inputs: bool = False) -> bytes:
        requested = max(0, int(round(requested_ms)))
        if requested < self.position:
            self.renderer.reset()
            self.index = 0
            self.session = None
            self.session_start = 0
            self.sequence = None
            self.dropped = None
        while self.index < len(self.records) and self.records[self.index].time_ms <= requested:
            event = self.records[self.index]
            at = max(0, int(round(event.time_ms)))
            if event.session != self.session:
                self.renderer.reset()
                self.session = event.session
                self.session_start = at
                self.sequence = None
                self.dropped = None
            sequence = event.record["seq"]
            delta = 1 if self.sequence is None else (sequence - self.sequence) % (1 << 64)
            # Retain decoder diagnostics, but duplicate/stale packets must not
            # make the displayed state jump back to an older source state.
            if 0 < delta < (1 << 63):
                dropped = event.record.get("dropped", self.dropped)
                if (self.sequence is not None and delta > 1) or (self.dropped is not None and dropped != self.dropped):
                    self.renderer.clear_inputs(at - self.session_start)
                if event.record["type"] == "view":
                    self.renderer.update(at - self.session_start, event.record)
                elif event.record["type"] == "input":
                    if "pressed" in event.record:
                        self.renderer.pressed(at - self.session_start, event.record["pressed"])
                    if event.record["input"].get("kind") in ("touch", "knob"):
                        self.renderer.input(at - self.session_start, event.record)
                self.sequence = sequence
                self.dropped = dropped
            self.index += 1
        self.position = requested
        ms = max(0, requested - self.session_start)
        return self.renderer.frame(ms, show_inputs=True) if show_inputs else self.renderer.frame(ms)


def png_frame(rgb: bytes, width: int = WIDTH, height: int = HEIGHT) -> bytes:
    """Encode RGB without optional Pillow or a system image converter."""
    if width < 1 or height < 1 or len(rgb) != width * height * 3:
        raise ValueError("Invalid RGB frame size")

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    stride = width * 3
    scan = b"".join(b"\0" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scan, 3)) + chunk(b"IEND", b""))


HTML = r"""<!doctype html><html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CrowPanel · запись экрана</title>
<style>
:root{color-scheme:dark;font:16px system-ui,sans-serif;background:#111820;color:#e8f0fa}
body{max-width:820px;margin:24px auto;padding:0 18px}h1{font-size:22px;margin-bottom:6px}
.meta{color:#a5b8cd;overflow-wrap:anywhere;font-size:14px}.screen{display:block;width:min(100%,var(--frame-size,480px));height:auto;aspect-ratio:1;margin:22px auto;background:#0c121a;border-radius:50%}
.controls{display:flex;align-items:center;gap:12px;flex-wrap:wrap}button,select{font:inherit;padding:8px 12px;background:#24364b;color:#edf5ff;border:1px solid #526a85;border-radius:6px}
button{cursor:pointer}input[type=range]{width:100%;margin:18px 0;accent-color:#64cfff}#clock{font-variant-numeric:tabular-nums;margin-left:auto}
.hint{font-size:14px;color:#a5b8cd}#error{color:#ff8b8b;white-space:pre-wrap}
.options{display:block;margin-top:18px;cursor:pointer}.options input{accent-color:#64cfff;width:18px;height:18px;vertical-align:middle;margin:0 7px 0 0}
</style>
<h1>CrowPanel · запись экрана</h1><div class="meta" id="name"></div>
<img class="screen" id="screen" width="480" height="480" alt="Изображение LVGL с CrowPanel">
<div class="controls"><button id="play">▶ Воспроизвести</button><button id="home">В начало</button>
<label>Скорость <select id="speed"><option>0.25</option><option>0.5</option><option selected>1</option><option>2</option><option>4</option></select>×</label>
<button id="export">Сохранить MP4</button><span id="clock"></span></div>
<label class="options"><input type="checkbox" id="inputs">Тапы, свайпы и энкодер</label>
<input id="seek" type="range" min="0" step="1" value="0" aria-label="Позиция записи">
<div class="hint" id="stats"></div><p class="hint">Пробел — пауза. ← / → — на 1 секунду. Изображение отрисовано исходным LVGL-интерфейсом прошивки.</p>
<pre id="error"></pre>
<script>
const $=id=>document.getElementById(id);let meta,position=0,playing=false,last=0,busy=false,desired=0,shown='',blobUrl;
const format=ms=>{const s=Math.floor(ms/1000);return `${Math.floor(s/60)}:${String(s%60).padStart(2,'0')}.${String(Math.floor(ms%1000)).padStart(3,'0')}`};
function controls(){ $('seek').value=position;$('clock').textContent=`${format(position)} / ${format(meta.duration_ms)}`;$('play').textContent=playing?'Ⅱ Пауза':'▶ Воспроизвести';}
const frameKey=()=>`${desired}:${Number($('inputs').checked)}`;
async function draw(){if(busy||shown===frameKey()||!meta)return;busy=true;const at=desired,enabled=$('inputs').checked,key=frameKey();try{const r=await fetch(`/frame?ms=${at}&inputs=${Number(enabled)}`);if(!r.ok)throw Error(await r.text());const image=await r.blob();if(key!==frameKey())return;const u=URL.createObjectURL(image);await new Promise((resolve,reject)=>{$('screen').onload=resolve;$('screen').onerror=reject;$('screen').src=u;});$('screen').style.setProperty('--frame-size',(enabled?600:480)+'px');if(blobUrl)URL.revokeObjectURL(blobUrl);blobUrl=u;shown=key;}
catch(e){playing=false;shown=key;$('error').textContent=String(e);controls();}finally{busy=false;if(shown!==frameKey())draw();}}
function seek(ms){position=Math.max(0,Math.min(meta.duration_ms,ms));desired=Math.round(position);last=performance.now();controls();draw();}
$('seek').oninput=e=>seek(Number(e.target.value));$('home').onclick=()=>seek(0);
$('inputs').onchange=()=>{shown='';draw();};
$('play').onclick=()=>{if(position>=meta.duration_ms)seek(0);playing=!playing;last=performance.now();controls();};
document.onkeydown=e=>{if(e.target.tagName==='SELECT'||e.target.tagName==='INPUT')return;if(e.code==='Space'){e.preventDefault();$('play').click();}if(e.code==='ArrowLeft'){e.preventDefault();seek(position-1000);}if(e.code==='ArrowRight'){e.preventDefault();seek(position+1000);}};
$('export').onclick=async()=>{const button=$('export'),enabled=$('inputs').checked;button.disabled=true;button.textContent='Сохраняю…';try{const r=await fetch('/export?inputs='+Number(enabled),{method:'POST'});if(!r.ok)throw Error(await r.text());const u=URL.createObjectURL(await r.blob());const a=document.createElement('a');a.href=u;a.download=meta.name.replace(/\.[^.]*$/,'')+(enabled?'-inputs':'')+'.mp4';a.click();setTimeout(()=>URL.revokeObjectURL(u),60000);}catch(e){$('error').textContent=String(e);}finally{button.disabled=false;button.textContent='Сохранить MP4';}};
function tick(now){if(meta&&playing){position=Math.min(meta.duration_ms,position+(now-last)*Number($('speed').value));desired=Math.round(position);if(position>=meta.duration_ms)playing=false;controls();draw();}last=now;setTimeout(()=>requestAnimationFrame(tick),65);}
fetch('/meta').then(r=>r.json()).then(m=>{meta=m;$('name').textContent=m.name;$('seek').max=m.duration_ms;$('stats').textContent=m.summary;$('inputs').checked=m.show_inputs;seek(0);requestAnimationFrame(tick);}).catch(e=>$('error').textContent=String(e));
</script></html>"""


def serve(recording, source: Path, executable: Path, port: int, open_browser: bool, show_inputs: bool = False):
    lock = threading.Lock()
    export_lock = threading.Lock()
    with NativeRenderer(executable) as renderer:
        replay = Replay(recording, renderer)

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def inputs_enabled(self, query: str) -> bool:
                value = urllib.parse.parse_qs(query).get("inputs", [str(int(show_inputs))])[0]
                if value not in ("0", "1"):
                    raise ValueError("Invalid input visualization option")
                return value == "1"

            def do_GET(self):
                parsed = urllib.parse.urlsplit(self.path)
                try:
                    if parsed.path == "/":
                        content, mime = HTML.encode("utf-8"), "text/html; charset=utf-8"
                    elif parsed.path == "/meta":
                        stats = recording.stats
                        gaps = stats.get("sequence_gaps", 0)
                        drops = stats.get("device_drops", 0)
                        damaged = sum(stats.get(key, 0) for key in ("malformed_lines", "invalid_records", "oversized_lines", "truncated_lines"))
                        summary = f"Состояний экрана: {stats.get('view_records', 0)} · Сеансов: {stats.get('sessions', 0)}"
                        if gaps or drops or damaged:
                            summary += f" · Пропуски: {gaps}, отброшено устройством: {drops}, повреждённых строк: {damaged}"
                        content = json.dumps({"name": source.name, "duration_ms": recording.duration_ms,
                                              "summary": summary, "show_inputs": show_inputs}, ensure_ascii=False).encode("utf-8")
                        mime = "application/json; charset=utf-8"
                    elif parsed.path == "/frame":
                        ms = float(urllib.parse.parse_qs(parsed.query).get("ms", ["0"])[0])
                        if not math.isfinite(ms):
                            raise ValueError("Invalid frame position")
                        enabled = self.inputs_enabled(parsed.query)
                        size = INPUT_SIZE if enabled else WIDTH
                        with lock:
                            content = png_frame(replay.frame(min(recording.duration_ms, max(0, ms)), show_inputs=enabled), size, size)
                        mime = "image/png"
                    elif parsed.path == "/favicon.ico":
                        self.send_response(204)
                        self.end_headers()
                        return
                    else:
                        self.send_error(404)
                        return
                    self.send_response(200)
                    self.send_header("Content-Type", mime)
                    self.send_header("Content-Length", str(len(content)))
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(content)
                except (BrokenPipeError, ConnectionResetError):
                    pass
                except Exception as error:
                    content = str(error).encode("utf-8")
                    self.send_response(400)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.send_header("Content-Length", str(len(content)))
                    self.end_headers()
                    self.wfile.write(content)

            def do_POST(self):
                parsed = urllib.parse.urlsplit(self.path)
                if parsed.path != "/export":
                    self.send_error(404)
                    return
                if not export_lock.acquire(blocking=False):
                    self.send_error(409, "Video export is already running")
                    return
                try:
                    with tempfile.TemporaryDirectory(prefix="crowpanel-video-") as directory:
                        video = Path(directory) / "crowpanel.mp4"
                        export(recording, executable, video, 30, show_inputs=self.inputs_enabled(parsed.query))
                        self.send_response(200)
                        self.send_header("Content-Type", "video/mp4")
                        self.send_header("Content-Length", str(video.stat().st_size))
                        self.send_header("Content-Disposition", 'attachment; filename="crowpanel.mp4"')
                        self.end_headers()
                        with video.open("rb") as stream:
                            shutil.copyfileobj(stream, self.wfile)
                except (BrokenPipeError, ConnectionResetError):
                    pass
                except Exception as error:
                    content = str(error).encode("utf-8")
                    self.send_response(500)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.send_header("Content-Length", str(len(content)))
                    self.end_headers()
                    self.wfile.write(content)
                finally:
                    export_lock.release()

        server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
        url = f"http://127.0.0.1:{server.server_port}/"
        print(f"CrowPanel viewer: {url}\nCtrl+C — остановить", flush=True)
        if open_browser:
            webbrowser.open(url)
        try:
            server.serve_forever(poll_interval=0.25)
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()


def export(recording, executable: Path, output: Path, fps: float, start: float = 0, end: float | None = None,
           show_inputs: bool = False):
    if shutil.which("ffmpeg") is None:
        raise ValueError("FFmpeg not found. Install ffmpeg to export video.")
    if not math.isfinite(fps) or not 1 <= fps <= 120:
        raise ValueError("FPS must be between 1 and 120")
    if not math.isfinite(start) or start < 0:
        raise ValueError("Start must be a nonnegative number of seconds")
    begin_ms = start * 1000
    end_ms = recording.duration_ms if end is None else end * 1000
    if not math.isfinite(end_ms) or begin_ms > end_ms or end_ms > recording.duration_ms:
        raise ValueError("Invalid export range")
    output = output.expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    size = INPUT_SIZE if show_inputs else WIDTH
    command = ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-y", "-f", "rawvideo",
               "-pixel_format", "rgb24", "-video_size", f"{size}x{size}", "-framerate", str(fps),
               "-i", "pipe:0", "-an", "-c:v", "libx264", "-crf", "18", "-pix_fmt", "yuv420p",
               "-movflags", "+faststart", str(output)]
    count = max(1, int(math.floor((end_ms - begin_ms) * fps / 1000)) + 1)
    with NativeRenderer(executable) as renderer:
        replay = Replay(recording, renderer)
        process = subprocess.Popen(command, stdin=subprocess.PIPE)
        try:
            for index in range(count):
                ms = min(end_ms, begin_ms + index * 1000 / fps)
                process.stdin.write(replay.frame(ms, show_inputs=show_inputs))
                if index % max(1, int(fps)) == 0:
                    print(f"\rВидео: {index + 1}/{count} кадров", end="", file=sys.stderr, flush=True)
            process.stdin.close()
            if process.wait():
                raise RuntimeError("FFmpeg could not encode video")
        except BaseException:
            with contextlib.suppress(BrokenPipeError, OSError):
                process.stdin.close()
            process.terminate()
            process.wait()
            raise
    print(f"\rВидео: {count} кадров → {output}", file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    view = sub.add_parser("view", help="Open an interactive local browser player")
    view.add_argument("file", type=Path)
    view.add_argument("--port", type=int, default=0)
    view.add_argument("--no-browser", action="store_true")
    view.add_argument("--renderer", help="Use a previously built native renderer")
    view.add_argument("--show-inputs", action="store_true", help="Show recorded touch gestures and the virtual encoder ring")
    video = sub.add_parser("export", help="Export an MP4 using FFmpeg")
    video.add_argument("file", type=Path)
    video.add_argument("--output", "-o", type=Path, required=True)
    video.add_argument("--fps", type=float, default=30)
    video.add_argument("--start", type=float, default=0, help="Start in seconds")
    video.add_argument("--end", type=float, help="End in seconds")
    video.add_argument("--renderer", help="Use a previously built native renderer")
    video.add_argument("--show-inputs", action="store_true", help="Include touch gestures and virtual encoder ring (600x600)")
    args = parser.parse_args(argv)
    try:
        recording = load_recording(args.file)
        if not any(event.record["type"] == "view" for event in recording.records):
            raise ValueError("Recording contains no CrowPanel view states")
        executable = renderer_path(args.renderer)
        if args.command == "view":
            serve(recording, args.file, executable, args.port, not args.no_browser, args.show_inputs)
        else:
            if args.file.resolve() == args.output.resolve():
                raise ValueError("Video output must differ from input recording")
            export(recording, executable, args.output, args.fps, args.start, args.end, args.show_inputs)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"CrowPanel viewer: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
