"""Compare the production streaming decoder with every original RGB565 pixel."""
import importlib.util
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tft_image_generator', ROOT / 'tools/generate_tft_image.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class TftImageTests(unittest.TestCase):
    def test_lossless_streamed_original(self):
        text, original, size = generator.generate()
        self.assertEqual((ROOT / 'G5500_Fork/tft_image.hpp').read_text(), text)
        self.assertLess(size, 33000)
        expected = 2166136261
        for pixel in original:
            for byte in (pixel & 255, pixel >> 8):
                expected = ((expected ^ byte) * 16777619) & 0xffffffff
        binary = ROOT / 'build/tft_image_tests'
        binary.parent.mkdir(exist_ok=True)
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-I', str(ROOT / 'G5500_Fork'), str(ROOT / 'tests/tft_image_tests.cpp'), '-o', str(binary)], check=True)
        rows, pixels, digest = map(int, subprocess.check_output([str(binary)], text=True).split())
        self.assertEqual((rows, pixels, digest), (240, len(original), expected))
        decoded = subprocess.check_output([str(binary), '--pixels'])
        self.assertEqual(decoded, b''.join(pixel.to_bytes(2, 'little') for pixel in original))
