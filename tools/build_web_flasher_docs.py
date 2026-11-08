#!/usr/bin/env python3
"""Bundle the selected firmware's documentation with the static installer."""
import argparse
import hashlib
from html import escape
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import shutil
import zipfile
from urllib.parse import unquote, urlsplit, urlunsplit
import markdown
from project_version import generate

ROOT = Path(__file__).resolve().parents[1]


def slug(text, separator):
    return re.sub(r'[^\w -]', '', text.lower().strip()).replace(' ', separator)


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.targets, self.ids = [], set()
    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if values.get('id'):
            self.ids.add(values['id'])
        for key in ('href', 'src'):
            if values.get(key):
                self.targets.append(values[key])


def build(output, website_only=False):
    output = Path(output).resolve()
    docs = output / 'documentation'
    docs.mkdir(exist_ok=True)
    shutil.copytree(ROOT / 'docs', docs / 'docs', dirs_exist_ok=True)
    shutil.copy2(ROOT / 'README.md', docs / 'README.md')
    for source in (ROOT / 'hardware').rglob('*.md'):
        target = docs / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    rendered = []
    for source in [docs / 'README.md', *sorted((docs / 'docs').rglob('*.md')),
                   *sorted((docs / 'hardware').rglob('*.md'))]:
        text = markdown.markdown(source.read_text(), extensions=['toc', 'tables', 'fenced_code'],
                                 extension_configs={'toc': {'slugify': slug}})
        text = re.sub(r'(href=["\'])([^"\']+)(["\'])',
                      lambda m: m[1] + rewrite(m[2]) + m[3], text)
        target = source.with_suffix('.html')
        depth = len(target.relative_to(output).parts) - 1
        back = '../' * depth + 'index.html'
        style = '../' * depth + 'style.css'
        page = ('<!doctype html><html lang="ru"><meta charset="utf-8">'
                '<meta name="viewport" content="width=device-width,initial-scale=1">'
                '<title>G5500 · Подключение и настройка</title>'
                '<link rel="stylesheet" href="' + style + '">'
                '<style>article{max-width:900px;padding:30px 0}article h1{font-size:30px}'
                'article h2{margin-top:30px}article h3{margin-top:24px}'
                'article img,article svg{max-width:100%;height:auto}'
                'pre{overflow:auto;padding:15px;background:#141b23}'
                'table{display:block;overflow:auto;border-collapse:collapse}'
                'th,td{border:1px solid #28323d;padding:8px;text-align:left}'
                'a{color:#9be6b4}p,li{overflow-wrap:anywhere}</style>'
                '<main class="wrap"><article><nav><a href="' + back + '">'
                '← Конфигуратор прошивки</a></nav>' + text + '</article></main></html>')
        target.write_text(page)
        rendered.append(target)
    broken = []
    parsed = {}
    for page in rendered:
        parser = Links(); parser.feed(page.read_text()); parsed[page] = parser
    for page, parser in parsed.items():
        for url in parser.targets:
            parts = urlsplit(url)
            if parts.scheme or parts.netloc or url.startswith('/'):
                continue
            target = (page.parent / unquote(parts.path)).resolve() if parts.path else page
            if not target.is_relative_to(output):
                broken.append((str(page.relative_to(output)), url, 'outside site')); continue
            if not target.exists() and target.is_relative_to(docs):
                original = ROOT / target.relative_to(docs)
                if original.is_file() and not any(p in {'local-notes', '.git', '.pio', 'build', 'artifacts'} for p in original.relative_to(ROOT).parts):
                    target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(original, target)
            if not target.exists():
                broken.append((str(page.relative_to(output)), url, 'missing file'))
            elif parts.fragment and target in parsed and unquote(parts.fragment) not in parsed[target].ids:
                broken.append((str(page.relative_to(output)), url, 'missing anchor'))
    catalog_path = output / 'catalog.json'
    catalog = json.loads(catalog_path.read_text())
    for profile in catalog['profiles']:
        assert (docs / profile['docs']).with_suffix('.html').is_file(), profile['id']
    if not website_only:
        bundle_sources(output, catalog)
    report = {'pages': len(rendered), 'broken_links': broken}
    (output / 'documentation-check.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    if broken:
        raise ValueError('Documentation links are invalid: ' + json.dumps(broken, ensure_ascii=False))
    print('Documentation: ' + str(len(rendered)) + ' pages; all local files and anchors resolve')


def bundle_sources(output, catalog):
    # Firmware releases bind their source archive and docs to the build. A UI
    # overlay updates the user guide but keeps that released archive/catalog.
    if generate(ROOT) != catalog['identity']:
        raise ValueError('Documentation sources and firmware catalog identities differ')
    archive = output / 'source-code.zip'
    entries = []
    for name in ('G5500_Fork', 'tools', 'platformio', 'web-flasher', 'docs', 'tests', 'hardware', '.github/workflows'):
        folder = ROOT / name
        entries += [p for p in folder.rglob('*') if p.is_file()
                    and not any(part in {'__pycache__', 'build', 'local-notes', '.pio'} for part in p.relative_to(ROOT).parts)
                    and not p.name.endswith(('.pyc', '.ino.cpp'))]
    entries += [ROOT / name for name in ('.gitattributes', 'README.md', 'LICENSE', 'platformio.ini', 'package.json', 'package-lock.json', 'requirements-platformio.txt')
                if (ROOT / name).is_file()]
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(entries):
            z.write(path, path.relative_to(ROOT).as_posix())
    catalog['source_archive'] = './source-code.zip'
    catalog['source_archive_sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
    catalog['documentation_base'] = './documentation/'
    (output / 'catalog.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n')


def rewrite(url):
    parts = urlsplit(url)
    if parts.scheme or parts.netloc:
        return url
    if parts.path.endswith('.md'):
        return urlunsplit(parts._replace(path=parts.path[:-3] + '.html'))
    return url


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    parser.add_argument('--website-only', action='store_true', help='Update documentation while preserving released firmware source archive and catalog')
    args=parser.parse_args()
    build(args.output, website_only=args.website_only)
