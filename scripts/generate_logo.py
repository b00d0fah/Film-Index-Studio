"""Convert the project owner's assets/logo.png to a multi-resolution Windows icon.
The source PNG is preserved unchanged, including its transparency.
"""
from pathlib import Path
from PIL import Image, ImageOps

assets = Path(__file__).resolve().parents[1] / 'assets'
with Image.open(assets / 'logo.png') as source:
    icon = ImageOps.pad(source.convert('RGBA'), (256, 256),
                        method=Image.Resampling.LANCZOS, color=(0, 0, 0, 0))
    icon.save(assets / 'app.ico', sizes=[(16,16), (24,24), (32,32), (48,48), (64,64), (128,128), (256,256)])
