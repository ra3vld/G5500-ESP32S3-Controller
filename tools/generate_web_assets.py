#!/usr/bin/env python3
"""Generate staged, immutable gzip assets; --check needs only Python."""
import argparse
import gzip
import hashlib
import html as html_module
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('web_page.hpp', 'web_page_spp.hpp', 'diagnostic_pages.hpp', 'hardware_page.hpp')
PROFILE_GUARDS = {'web_page.hpp': '!ESP32_SPP_PROFILE', 'web_page_spp.hpp': 'ESP32_SPP_PROFILE'}
ASSET_RE = r'(?:static|inline) constexpr char (\w+)\[\](?: PROGMEM)? = R"(\w+)\((.*?)\)\2";'
OUTPUT = ROOT / 'G5500_Fork/web_assets.hpp'
POLL_HELPER = ROOT / 'tools/web_poll.js'
ACTIVITY_HELPER = ROOT / 'tools/web_activity.js'
RELEASE_HELPER = ROOT / 'tools/web_release.js'
RELEASE_BLOCK = r'/\* BEGIN RELEASE CHECK \*/.*?/\* END RELEASE CHECK \*/'
POLL_BLOCK = r'/\* BEGIN BOUNDED POLLING \*/.*?/\* END BOUNDED POLLING \*/'
ACTIVITY_BLOCK = r'/\* BEGIN STARTUP ACTIVITY \*/.*?/\* END STARTUP ACTIVITY \*/'
CACHE = ROOT / 'tools/web_compiled.json'


def pack_html(raw):
    packed = bytearray(gzip.compress(raw, compresslevel=9, mtime=0))
    packed[9] = 255
    return bytes(packed)


def sha(raw):
    return hashlib.sha256(raw if isinstance(raw, bytes) else raw.encode()).hexdigest()


class Compiler:
    def __init__(self, check):
        self.check, self.changed = check, False
        self.cache = json.loads(CACHE.read_text()) if CACHE.exists() else {}
        self.recipe = sha((ROOT / 'tools/web_compile.cjs').read_bytes())

    def compile(self, name, code):
        old = self.cache.get(name)
        if old and old['source_sha256'] == sha(code) and old.get('recipe_sha256') == self.recipe:
            result = old['result']
            if sha(json.dumps(result, sort_keys=True, ensure_ascii=False)) != old['result_sha256']:
                raise ValueError(f'Invalid compiled JavaScript cache: {name}')
            return result
        if self.check:
            raise ValueError(f'Stale compiled JavaScript: {name}; run python3 tools/generate_web_assets.py')
        try:
            process = subprocess.run(['node', str(ROOT / 'tools/web_compile.cjs')], input=code,
                                     text=True, capture_output=True, check=True)
        except (OSError, subprocess.CalledProcessError) as error:
            detail = getattr(error, 'stderr', '') or str(error)
            raise ValueError('JavaScript regeneration needs Node.js with acorn and terser; '
                             'unchanged firmware assets/checks need only Python. ' + detail) from error
        result = json.loads(process.stdout)
        self.cache[name] = {'source_sha256': sha(code), 'recipe_sha256': self.recipe, 'result': result,
                           'result_sha256': sha(json.dumps(result, sort_keys=True, ensure_ascii=False))}
        self.changed = True
        return result

    def finish(self):
        if self.changed:
            temporary = CACHE.with_suffix('.json.tmp')
            temporary.write_text(json.dumps(self.cache, ensure_ascii=False, sort_keys=True, indent=2) + '\n')
            temporary.replace(CACHE)


class Markup(HTMLParser):
    """Collapse HTML whitespace, preserving script/CSS/preformatted text."""
    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.parts, self.preserve = [], []

    def handle_starttag(self, tag, attrs):
        self.parts.append(self.get_starttag_text())
        if tag in ('script', 'style', 'pre', 'textarea'):
            self.preserve.append(tag)

    def handle_startendtag(self, tag, attrs):
        self.parts.append(self.get_starttag_text())

    def handle_endtag(self, tag):
        self.parts.append('</' + tag + '>')
        if self.preserve and self.preserve[-1] == tag:
            self.preserve.pop()

    def handle_data(self, data):
        self.parts.append(data if self.preserve else re.sub(r'\s+', ' ', data))

    def handle_entityref(self, name):
        self.parts.append('&' + name + ';')

    def handle_charref(self, name):
        self.parts.append('&#' + name + ';')

    def handle_decl(self, decl):
        self.parts.append('<!' + decl + '>')


def minify_markup(text):
    parser = Markup()
    parser.feed(text)
    parser.close()
    return ''.join(parser.parts).strip()


def minify_css(text):
    # Preserve selector whitespace, strings, calc(), and custom properties.
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return '\n'.join(line.strip() for line in text.splitlines() if line.strip())


def bundle(check=False):
    compiler = Compiler(check)
    resources, pages, sections, sizes = [], {}, [], []

    def resource(text, kind, guard):
        raw = text.encode()
        url = '/ui/' + sha(raw)[:20] + '.' + kind
        for asset in resources:
            if asset['url'] == url:
                assert asset['raw'] == raw
                asset['guards'].add(guard)
                return url
        resources.append({'name': f'WEB_STATIC_{len(resources)}', 'url': url, 'raw': raw,
                          'packed': pack_html(raw), 'guards': {guard},
                          'type': {'js': 'text/javascript', 'css': 'text/css', 'html': 'text/html'}[kind] + '; charset=utf-8'})
        return url

    helper = '/* BEGIN BOUNDED POLLING */\n' + POLL_HELPER.read_text().rstrip() + '\n/* END BOUNDED POLLING */'
    activity = '/* BEGIN STARTUP ACTIVITY */\n' + ACTIVITY_HELPER.read_text().rstrip() + '\n/* END STARTUP ACTIVITY */'
    releases = '/* BEGIN RELEASE CHECK */\n' + RELEASE_HELPER.read_text().rstrip() + '\n/* END RELEASE CHECK */'
    for filename in SOURCES:
        path = ROOT / 'G5500_Fork' / filename
        source = path.read_text()
        updated = re.sub(POLL_BLOCK, lambda _: helper, source, flags=re.S)
        updated = re.sub(RELEASE_BLOCK, lambda _: releases, updated, flags=re.S)
        updated = re.sub(r'\n' + ACTIVITY_BLOCK, '', updated, flags=re.S)
        updated = updated.replace('/* END BOUNDED POLLING */', '/* END BOUNDED POLLING */\n' + activity)
        if updated != source:
            if check:
                raise ValueError(f'Stale polling helper in {filename}; regenerate web assets')
            path.write_text(updated)
            source = updated
        guard = PROFILE_GUARDS.get(filename)
        section = []
        for name, _, original in re.findall(ASSET_RE, source, re.S):
            match = re.search(r'<script>(.*?)</script>', original, re.S)
            script = match[1]
            if filename in ('web_page.hpp', 'hardware_page.hpp'):
                common = '\n'.join(re.findall(POLL_BLOCK + '|' + ACTIVITY_BLOCK, script, re.S))
                script = re.sub(POLL_BLOCK + '|' + ACTIVITY_BLOCK, '', script, flags=re.S)
                shared = compiler.compile('shared', common)
                assert not shared['libraries']
                scripts = [resource(shared['init'], 'js', guard)]
                compiled = compiler.compile(filename + ':' + name, script)
                style = re.search(r'<style>(.*?)</style>', original, re.S)[1]
                body_match = re.search(r'<body[^>]*>(.*?)<script>', original, re.S)
                body = body_match[1] if body_match else original[re.search(r'</style>', original).end():match.start()]
                if filename == 'web_page.hpp':
                    history = re.search(r'(<section id="historyPanel"[^>]*>)(.*?)</section>', body, re.S)
                    assert history
                    history_url = resource(minify_markup(history[2]), 'html', guard)
                    body = body[:history.start()] + history[1][:-1] + ' data-content-url="' + history_url + '"></section>' + body[history.end():]
                manifest = {'style': resource(minify_css(style), 'css', guard),
                            'body': resource(minify_markup(body), 'html', guard)}
                scripts += [resource(code, 'js', guard) for code in compiled['libraries']]
                scripts.append(resource(compiled['init'] + ';window.__webUiReady=true;', 'js', guard))
                manifest['scripts'] = scripts
                boot = compiler.compile('bootstrap', (ROOT / 'tools/web_boot.js').read_text())
                assert not boot['libraries']
                code = boot['init'].replace('__WEB_MANIFEST__', json.dumps(manifest, separators=(',', ':')))
                title = html_module.escape(re.search(r'<title>(.*?)</title>', original, re.S)[1])
                head = ''.join(re.findall(r'<link\b[^>]*>', original[:match.start()], re.I))
                served = minify_markup((ROOT / 'tools/web_boot.html').read_text().replace('{{TITLE}}', title).replace('{{EXTRA_HEAD}}', head).replace('{{BOOT_SCRIPT}}', code))
            else:
                compiled = compiler.compile(filename + ':' + name, script)
                served = original[:match.start(1)] + '\n'.join([*compiled['libraries'], compiled['init']]) + original[match.end(1):]
                served = re.sub(r'<style>(.*?)</style>', lambda m: '<style>' + minify_css(m[1]) + '</style>', served, flags=re.S)
                served = minify_markup(served)
                manifest = None
            raw, packed = served.encode(), pack_html(served.encode())
            key = 'SPP_WEB_PAGE' if filename == 'web_page_spp.hpp' and name == 'WEB_PAGE' else name
            pages[key] = {'name': name, 'raw': raw, 'packed': packed, 'manifest': manifest, 'original': original.encode()}
            section.append((name, original.encode(), raw, packed))
            sizes.append((name, len(original.encode()), len(packed)))
        sections.append((filename, guard, section))
    compiler.finish()
    lines = ['// Generated by tools/generate_web_assets.py; do not edit.',
             '// G5500 Fork; original by UA1CFM. GPL-3.0.', '#pragma once', '#include <cstddef>', '#include <cstdint>',
             '#include "web_page.hpp"', '#include "diagnostic_pages.hpp"', '#include "hardware_page.hpp"',
             '#ifndef ARDUINO', '#ifndef PROGMEM', '#define PROGMEM', '#endif', '#endif',
             'template<size_t N> constexpr uint32_t webAssetHash(const char (&data)[N]) {',
             '  uint32_t hash = 2166136261u;',
             '  for (size_t block = 0; block + 1 < N; block += 1024)',
             '    for (size_t i = block; i + 1 < N && i < block + 1024; ++i) hash = (hash ^ uint8_t(data[i])) * 16777619u;',
             '  return hash;', '}']

    def emit(name, raw, packed):
        assert ')HTTP"' not in raw.decode()
        lines.extend([f'static constexpr char {name}_HTTP[] PROGMEM = R"HTTP({raw.decode()})HTTP";',
                      f'static const unsigned char {name}_GZIP[] PROGMEM = {{'])
        lines.extend('  ' + ','.join(f'0x{byte:02x}' for byte in packed[i:i+16]) + ',' for i in range(0, len(packed), 16))
        lines.append('};')

    for filename, guard, section in sections:
        lines.append(f'// BEGIN ASSETS: {filename}')
        if guard:
            lines.append('#if ' + guard)
        for name, original, raw, packed in section:
            lines.append('// Source sha256=' + sha(original))
            emit(name, raw, packed)
            fingerprint = 2166136261
            for byte in original:
                fingerprint = ((fingerprint ^ byte) * 16777619) & 0xffffffff
            symbol = 'forkrot::' + name if filename == 'hardware_page.hpp' else name
            lines.extend([f'static constexpr size_t {name}_RAW_SIZE = {len(original)};',
                          f'static_assert(webAssetHash({symbol}) == 0x{fingerprint:08x}u, "Regenerate web assets: python3 tools/generate_web_assets.py");'])
        if guard:
            lines.append('#endif')
        lines.append(f'// END ASSETS: {filename}')

    def guard_for(asset):
        return None if None in asset['guards'] else ' || '.join(sorted(asset['guards']))

    for asset in resources:
        guard = guard_for(asset)
        if guard:
            lines.append('#if ' + guard)
        emit(asset['name'], asset['raw'], asset['packed'])
        if guard:
            lines.append('#endif')
    lines.extend(['struct WebStaticAsset { const char *path; const char *type; const unsigned char *gzip; size_t gzipSize; const char *raw; size_t rawSize; };',
                  'static constexpr WebStaticAsset WEB_STATIC_ASSETS[] PROGMEM = {'])
    for asset in resources:
        guard, name = guard_for(asset), asset['name']
        if guard:
            lines.append('#if ' + guard)
        lines.append('  {' + json.dumps(asset['url']) + ',' + json.dumps(asset['type']) + f',{name}_GZIP,sizeof {name}_GZIP,WEB_PLAIN_HTML_SUPPORT?{name}_HTTP:nullptr,sizeof {name}_HTTP-1' + '},')
        if guard:
            lines.append('#endif')
    lines.append('};')
    return '\n'.join(lines) + '\n', sizes, resources, pages


def generate(check=False):
    content, sizes, _, _ = bundle(check)
    return content, sizes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Fail if generated assets are stale (no Node required)')
    args = parser.parse_args()
    try:
        content, sizes = generate(args.check)
    except ValueError as error:
        parser.error(str(error))
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != content:
            parser.error('web assets are stale; run python3 tools/generate_web_assets.py')
    else:
        temporary = OUTPUT.with_suffix('.hpp.tmp')
        temporary.write_text(content)
        temporary.replace(OUTPUT)
    for name, raw, packed in sizes:
        print(f'{name}: {raw} source -> {packed} HTTP gzip bytes')


if __name__ == '__main__':
    main()
