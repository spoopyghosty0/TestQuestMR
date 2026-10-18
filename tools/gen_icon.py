#!/usr/bin/env python3
"""Makes the app icon from the host renderer's view of the demo room's floor and furniture: docs/icon.png (512 px)
and the launcher mipmaps. Build tools/render_host first (see README).

    python3 tools/gen_icon.py
"""
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
MIPMAPS = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def read_pam(path):
    data = path.read_bytes()
    end = data.index(b"ENDHDR\n") + len(b"ENDHDR\n")
    head = dict(line.split(" ", 1) for line in data[:end].decode().splitlines()[1:-1])
    w, h = int(head["WIDTH"]), int(head["HEIGHT"])
    return Image.fromarray(np.frombuffer(data[end:], np.uint8).reshape(h, w, 4), "RGBA")


def main():
    shot = ROOT / "out/icon.pam"
    subprocess.run([str(ROOT / "out/render_host/render_host"), str(shot), "icon", "1024", "1024"], check=True,
                   capture_output=True)
    room = read_pam(shot)
    room = room.crop(room.getbbox())  # the room alone, so it can fill the icon
    fit = 430 / max(room.size)
    room = room.resize((round(room.width * fit), round(room.height * fit)), Image.LANCZOS)

    # dark blue -> deep violet: the coloured outlines and boxes read at 48 px
    size = 512
    t = np.clip((np.arange(size)[None, :] + np.arange(size)[:, None]) / (2 * size - 2), 0, 1)[..., None]
    top, bottom = np.array([24, 44, 96], float), np.array([58, 24, 82], float)
    grad = (top * (1 - t) + bottom * t).astype(np.uint8)
    bg = Image.fromarray(np.dstack([grad, np.full((size, size, 1), 255, np.uint8)]), "RGBA")
    at = ((size - room.width) // 2, (size - room.height) // 2 + 8)
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    glow.paste((120, 200, 255, 90), at, room.getchannel("A"))
    bg = Image.alpha_composite(bg, glow.filter(ImageFilter.GaussianBlur(10)))
    bg.alpha_composite(room, at)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=110, fill=255)
    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    icon.paste(bg, (0, 0), mask)

    icon.save(ROOT / "docs/icon.png")
    for name, px in MIPMAPS.items():
        folder = ROOT / f"app/src/main/res/mipmap-{name}"
        folder.mkdir(parents=True, exist_ok=True)
        icon.resize((px, px), Image.LANCZOS).save(folder / "ic_launcher.png")
    print(f"wrote docs/icon.png and {len(MIPMAPS)} mipmaps")


if __name__ == "__main__":
    main()
