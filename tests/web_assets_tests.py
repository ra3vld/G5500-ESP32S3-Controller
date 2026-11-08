"""Verify the bytes actually embedded in flash, including gzip CRC and UI text."""
import gzip
from pathlib import Path
import re
import unittest

from tools.generate_web_assets import generate, bundle, minify_markup

ROOT = Path(__file__).resolve().parents[1]


class WebAssetsTests(unittest.TestCase):
    def test_generated_header_is_current(self):
        expected, sizes = generate(check=True)
        self.assertEqual((ROOT / "G5500_Fork/web_assets.hpp").read_text(), expected)
        self.assertEqual(len(sizes), 9)

    def test_embedded_assets_decompress_exactly(self):
        header = (ROOT / "G5500_Fork/web_assets.hpp").read_text()
        _, _, resources, pages = bundle(check=True)
        for asset in [*resources, *pages.values()]:
            name = asset['name']
            with self.subTest(asset=name):
                # WEB_PAGE has different profile-guarded definitions.
                arrays = re.findall(rf'\b{name}_GZIP\[\] PROGMEM = \{{(.*?)\}};', header, re.S)
                packed = asset['packed']
                self.assertTrue(any(bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', array)) == packed for array in arrays))
                self.assertEqual(gzip.decompress(packed), asset['raw'])
                self.assertEqual(packed[4:8], b"\0\0\0\0")

    def test_staged_budgets_and_content_addresses(self):
        import hashlib
        _, _, resources, pages = bundle(check=True)
        by_url = {r['url']: r for r in resources}
        for name in ['WEB_PAGE', 'HARDWARE_PAGE']:
            page = pages[name]
            self.assertLess(len(page['packed']), 5000)
            manifest = page['manifest']
            for url in [manifest['style'], manifest['body'], *manifest['scripts']]:
                self.assertIn(url, by_url)
            self.assertIn(b'webBootStop', page['raw'])
            self.assertNotIn(b'loadHistory()', by_url[manifest['body']]['raw'])
        for asset in resources:
            self.assertIn(hashlib.sha256(asset['raw']).hexdigest()[:20], asset['url'])
            self.assertLess(len(asset['packed']), 24000, asset['url'])

    def test_minification_preserves_whitespace_sensitive_content(self):
        text='<p>alpha  beta</p><pre>a  b\nc</pre><textarea>x  y</textarea><script>const x="a  b";</script>'
        result=minify_markup(text)
        self.assertIn('<p>alpha beta</p>',result)
        for part in ['<pre>a  b\nc</pre>','<textarea>x  y</textarea>','const x="a  b";']:
            self.assertIn(part,result)


if __name__ == "__main__":
    unittest.main()
