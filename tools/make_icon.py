"""Disegna l'icona di Alleggerisci (resources/alleggerisci.png e .ico): una foto stilizzata con una freccia verso il
basso. Disegno originale, nessun elemento di terzi. uso: uv run --with pillow tools/make_icon.py"""
from pathlib import Path
from PIL import Image, ImageDraw

S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
# Fondo: quadrato arrotondato blu con sfumatura verticale.
grad = Image.new("RGBA", (S, S))
gd = ImageDraw.Draw(grad)
for y in range(S):
    t = y / S
    gd.line([(0, y), (S, y)], fill=(int(37 + 30 * t), int(99 - 40 * t), int(235 - 60 * t), 255))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=210, fill=255)
img.paste(grad, (0, 0), mask)
# Foto: cornice bianca con montagne e sole.
d.rounded_rectangle([200, 230, 824, 690], radius=60, fill=(255, 255, 255, 255))
d.polygon([(250, 640), (430, 420), (560, 560), (640, 470), (774, 640)], fill=(37, 99, 235, 255))
d.ellipse([640, 290, 730, 380], fill=(250, 204, 21, 255))
# Freccia verso il basso (più leggero).
d.rounded_rectangle([462, 610, 562, 790], radius=20, fill=(34, 197, 94, 255))
d.polygon([(372, 760), (652, 760), (512, 900)], fill=(34, 197, 94, 255))
out = Path(__file__).resolve().parent.parent / "resources"
img.resize((256, 256), Image.LANCZOS).save(out / "alleggerisci.png")
img.save(out / "alleggerisci.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("icona scritta in", out)
