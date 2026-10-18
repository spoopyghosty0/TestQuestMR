#!/usr/bin/env python3
"""Renders the README screenshots with the PC renderer and the synthetic demo room: docs/screenshots/*.png.
Build tools/render_host first (see README).

    python3 tools/screenshots.py [shot ...]
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHOTS = {
    "room": "room",          # inside the room, with the panel
    "overview": "overview",  # the whole room, no panel
    "fb-meta": "both",       # FB results with the META results drawn over them
    "mesh": "wire",          # the room mesh as a wireframe
    "panel-runtime": "runtime",
    "panel-room": "roomtab",
    "panel-anchors": "anchors",
    "panel-log": "log",
}


def main():
    names = sys.argv[1:] or list(SHOTS)
    out = ROOT / "out"
    out.mkdir(exist_ok=True)
    (ROOT / "docs/screenshots").mkdir(parents=True, exist_ok=True)
    for name in names:
        ppm = out / f"{name}.ppm"
        subprocess.run([str(out / "render_host/render_host"), str(ppm), SHOTS[name]], check=True,
                       stdout=subprocess.DEVNULL)
        png = ROOT / f"docs/screenshots/{name}.png"
        Image.open(ppm).save(png, optimize=True)
        print(f"wrote {png.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
