"""Lossless font storage: compare every glyph against DejaVu's original 4 bpp pixels."""
import importlib.util
from pathlib import Path
import unittest
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fontgen',ROOT/'tools/generate_panel_fonts.py')
gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)

def decode(data,count):
 bits=[(b>>shift)&1 for b in data for shift in range(7,-1,-1)];pos=0
 def get(n):
  nonlocal pos
  v=0
  for bit in bits[pos:pos+n]:v=v*2+bit
  pos+=n;return v
 previous=None;state=0;run=0;pixels=[]
 for _ in range(count):
  if state==0:
   value=get(4)
   if previous is not None and value==previous:state=1;run=0
   previous=value
  elif state==1:
   bit=get(1);run+=1
   if bit:
    value=previous
    if run==11:
     run=get(6)
     if run:state=2
     else:value=get(4);previous=value;state=0
   else:value=get(4);previous=value;state=0
  else:
   run-=1;value=previous
   if run==0:value=get(4);previous=value;state=0
  pixels.append(value)
 return pixels

class FontRleTests(unittest.TestCase):
 def test_runs_and_transitions(self):
  for size in range(300):
   for pixels in ([0]*size,[15]*size,[i%16 for i in range(size)],([0]*80+[1,1,2,3]*10)*size):
    self.assertEqual(decode(gen.compress_pixels(pixels),len(pixels)),list(pixels))
 def test_all_glyphs(self):
  for size in (14,18,24,28,40,48,56,64,72,80,96,128,144):
   font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',size)
   for point in gen.FULL if size<=18 or size==28 else gen.NUMBERS:
    l,t,r,b=font.getbbox(chr(point),anchor='ls');image=Image.new('L',(r-l,b-t));ImageDraw.Draw(image).text((-l,-t),chr(point),font=font,fill=255,anchor='ls')
    pixels=[(v+8)//17 for v in image.getdata()]
    self.assertEqual(decode(gen.compress_pixels(pixels),len(pixels)),pixels,(size,point))
if __name__=='__main__':unittest.main()
