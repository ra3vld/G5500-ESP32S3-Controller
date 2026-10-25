#!/usr/bin/env python3
"""Convert production-renderer native test frames to PNG; update the user screenshot."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
PREVIEW = ROOT / 'build/unified-panel-preview'
NAMES = ['single', 'dual', 'confirm', 'moving', 'elevation', 'wide', 'menu', 'offline', 'speed']


def main():
    for name in NAMES:
        source = PREVIEW / ('240x240-' + name + '.ppm')
        if not source.exists():
            raise SystemExit('Run tools/check_panel_ui.sh with LVGL_DIR set first')
        Image.open(source).save(source.with_suffix('.png'))
    canvas = Image.new('RGB', (4*260, ((len(NAMES)+3)//4)*282), '#0a111c')
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
    for i, name in enumerate(NAMES):
        x, y = (i % 4)*260+10, (i // 4)*282
        canvas.paste(Image.open(PREVIEW / ('240x240-'+name+'.png')), (x, y+24))
        draw.text((x, y+4), name, font=font, fill='#8fabc3')
    canvas.save(PREVIEW / 'overview.png')
    public = Image.new('RGBA', (744, 240))
    mask = Image.new('L', (240, 240))
    ImageDraw.Draw(mask).ellipse((0, 0, 239, 239), fill=255)
    for i, name in enumerate(['dual', 'confirm', 'moving']):
        image = Image.open(PREVIEW / ('240x240-'+name+'.png')).convert('RGBA')
        image.putalpha(mask)
        public.paste(image, (i*252, 0))
    public.save(ROOT / 'docs/images/esp32-2424s012c.png')


if __name__ == '__main__':
    main()
