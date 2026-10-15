"""Verify the bytes actually embedded in flash, including gzip CRC and UI text."""
import gzip
from pathlib import Path
import re
import unittest

from tools.generate_web_assets import generate

ROOT = Path(__file__).resolve().parents[1]


class WebAssetsTests(unittest.TestCase):
    def test_generated_header_is_current(self):
        expected, sizes = generate()
        self.assertEqual((ROOT / "G5500_Fork/web_assets.hpp").read_text(), expected)
        self.assertEqual(len(sizes), 4)

    def test_embedded_assets_decompress_exactly(self):
        header = (ROOT / "G5500_Fork/web_assets.hpp").read_text()
        for filename in ("web_page.hpp", "diagnostic_pages.hpp"):
            source = (ROOT / "G5500_Fork" / filename).read_text()
            for name, html in re.findall(r'static constexpr char (\w+)\[\] PROGMEM = R"HTML\((.*?)\)HTML";', source, re.S):
                with self.subTest(asset=name):
                    array = re.search(rf'\b{name}_GZIP\[\] PROGMEM = \{{(.*?)\}};', header, re.S).group(1)
                    packed = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', array))
                    self.assertEqual(gzip.decompress(packed), html.encode())
                    self.assertLess(len(packed), len(html.encode()))
                    self.assertEqual(packed[4:8], b"\0\0\0\0")


if __name__ == "__main__":
    unittest.main()
