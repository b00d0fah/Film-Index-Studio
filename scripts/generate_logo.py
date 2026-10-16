"""Build the editable film-cartridge vector mark and matching PNG/Windows ICO."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / 'assets'
scale = 4
image = Image.new('RGBA', (512 * scale, 512 * scale), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
svg = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">',
       '<title>Film Index — 35 mm film cartridge and negative strip</title>']

def rect(x, y, width, height, radius, fill):
    svg.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="{radius}" fill="{fill}"/>')
    draw.rounded_rectangle(tuple(int(v * scale) for v in (x, y, x + width, y + height)), radius * scale, fill=fill)

def ellipse(x, y, width, height, fill):
    svg.append(f'<ellipse cx="{x + width / 2}" cy="{y + height / 2}" rx="{width / 2}" ry="{height / 2}" fill="{fill}"/>')
    draw.ellipse(tuple(int(v * scale) for v in (x, y, x + width, y + height)), fill=fill)

# Bold, optically separated shapes stay legible at taskbar icon sizes.
rect(16, 16, 480, 480, 104, '#17272b')
rect(38, 38, 436, 436, 84, '#22383b')
# Negative tail: pale film stock, two exposed frames and sprocket holes.
rect(204, 172, 244, 234, 22, '#f1dcaa')
rect(252, 212, 75, 151, 5, '#30494b')
rect(341, 212, 75, 151, 5, '#30494b')
for x in (251, 292, 333, 374, 415):
    rect(x, 185, 18, 15, 3, '#22383b')
    rect(x, 377, 18, 15, 3, '#22383b')
# Cartridge spindle, rounded cap and golden body.
rect(145, 76, 48, 42, 9, '#f1dcaa')
rect(91, 116, 155, 274, 28, '#dba54d')
ellipse(91, 97, 155, 64, '#f6c96b')
ellipse(110, 110, 117, 33, '#b67e34')
rect(91, 179, 155, 134, 0, '#16292e')
rect(111, 197, 8, 97, 4, '#f6c96b')
# Simple negative-window symbol on the cartridge label (no tiny lettering).
rect(139, 211, 81, 65, 6, '#f1dcaa')
rect(150, 221, 59, 45, 2, '#30494b')
rect(110, 337, 116, 12, 5, '#f8d994')
ellipse(91, 364, 155, 38, '#b67e34')
svg.append('</svg>')
(root / 'logo.svg').write_text('\n'.join(svg), encoding='utf-8')
image = image.resize((512, 512), Image.Resampling.LANCZOS)
image.save(root / 'logo.png')
image.save(root / 'app.ico', sizes=[(16,16), (24,24), (32,32), (48,48), (64,64), (128,128), (256,256)])
