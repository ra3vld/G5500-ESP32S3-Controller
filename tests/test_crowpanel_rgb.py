"""Run the actual pinned driver's functions against small host GDMA/cache fakes."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'^(?:static |IRAM_ATTR )*(?:esp_err_t|void|bool) ' + name + r'\([^;]+?\)\n\{', source, re.M)
    if not match:
        raise AssertionError(f'Function definition missing: {name}')
    start = match.start()
    cursor = match.end()
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor]


class CrowPanelRgbTests(unittest.TestCase):
    def test_real_driver_restart_paths(self):
        source = Path(os.environ.get('CROWPANEL_RGB_SOURCE', ROOT / 'G5500_Fork/crowpanel_rgb_driver.c')).read_text()
        configuration = re.search(r'^#define CONFIG_LCD_RGB_RESTART_IN_VSYNC [01]$', source, re.M).group()
        code = '#include "crowpanel_rgb_fakes.hpp"\n' + configuration + '\n'
        code += '#define CROWPANEL_HAS_FRAME_STATUS ' + str(int('crowpanel_rgb_frame_status(' in source)) + '\n'
        for name in ('lcd_rgb_panel_init_trans_link', 'lcd_rgb_panel_writeback', 'rgb_panel_draw_bitmap', 'lcd_rgb_panel_fill_bounce_buffer', 'lcd_rgb_panel_eof_handler', 'lcd_rgb_panel_try_restart_transmission'):
            # C's nested designator needs braces when compiled by the C++ host harness.
            code += function(source, name).replace(
                '.flags.bypass_buffer_align_check = true,',
                '.flags = {.bypass_buffer_align_check = true},').replace(
                '(uint32_t)&panel->fbs', '(uintptr_t)&panel->fbs') + '\n'
        code += '#include "crowpanel_rgb_tests.cpp"\n'
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'rgb-tests'
            subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-O1', '-g',
                            '-fsanitize=undefined', '-I', str(ROOT / 'tests'),
                            '-x', 'c++', '-', '-o', str(binary)], input=code, text=True, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
