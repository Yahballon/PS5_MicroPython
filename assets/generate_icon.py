#!/usr/bin/env python3
"""Generate the original 512x512 procedural icon used by the PS5 launcher."""
import sys
from pathlib import Path
from PIL import Image, ImageDraw

dest = Path(sys.argv[1])
dest.parent.mkdir(parents=True, exist_ok=True)
im = Image.new("RGB", (512, 512), "#050b1a")
d = ImageDraw.Draw(im)
d.rounded_rectangle((8, 8, 503, 503), radius=90, fill="#0d2040")
d.rounded_rectangle((42, 42, 469, 469), radius=70, outline="#476eaa", width=6)
d.rounded_rectangle((89, 92, 423, 420), radius=35, fill="#081425", outline="#274f7c", width=5)
d.rounded_rectangle((137, 134, 350, 278), radius=49, fill="#49cde7")
d.rectangle((137, 217, 185, 278), fill="#49cde7")
d.rectangle((185, 216, 350, 255), fill="#081425")
d.ellipse((288, 160, 307, 179), fill="#061728")
d.rounded_rectangle((163, 226, 376, 367), radius=49, fill="#f4bf51")
d.rectangle((328, 226, 376, 286), fill="#f4bf51")
d.rectangle((163, 286, 330, 325), fill="#081425")
d.ellipse((205, 321, 224, 340), fill="#483317")
d.line([(223, 252), (248, 272), (223, 292)], fill="#e9f8ff", width=11, joint="curve")
d.line([(266, 295), (296, 295)], fill="#e9f8ff", width=10)
im.save(dest, optimize=True)
print(dest)
