"""Regenerate the original Film Index vector mark and Windows icon (Pillow only)."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / 'assets'
root.mkdir(exist_ok=True)
rectangles = [(105, 75, 147, 105), (235, 75, 277, 105), (365, 75, 407, 105),
              (105, 407, 147, 437), (235, 407, 277, 437), (365, 407, 407, 437)]
f_points = [(155, 153), (361, 153), (361, 205), (212, 205),
            (212, 246), (329, 246), (329, 299), (212, 299), (212, 359), (155, 359)]
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">',
       '<rect x="24" y="24" width="464" height="464" rx="88" fill="#192a2d"/>',
       '<rect x="64" y="126" width="384" height="260" rx="20" fill="none" stroke="#80dfba" stroke-width="10"/>']
for x1,y1,x2,y2 in rectangles:
    svg.append(f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" rx="5" fill="#80dfba"/>')
svg.append('<polygon points="' + ' '.join(f'{x},{y}' for x,y in f_points) + '" fill="#f5f2e8"/>')
svg.append('</svg>')
(root / 'logo.svg').write_text('\n'.join(svg), encoding='utf-8')
scale=4
im=Image.new('RGBA',(512*scale,512*scale),(0,0,0,0)); d=ImageDraw.Draw(im)
def box(rect): return tuple(v*scale for v in rect)
d.rounded_rectangle(box((24,24,488,488)),88*scale,fill='#192a2d')
d.rounded_rectangle(box((59,121,453,391)),25*scale,outline='#80dfba',width=10*scale)
for rect in rectangles: d.rounded_rectangle(box(rect),5*scale,fill='#80dfba')
d.polygon([(x*scale,y*scale) for x,y in f_points],fill='#f5f2e8')
im=im.resize((512,512),Image.Resampling.LANCZOS)
im.save(root/'logo.png')
im.save(root/'app.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
