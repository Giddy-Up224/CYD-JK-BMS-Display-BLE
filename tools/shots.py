"""Turn screenshots from the serial log into PNGs.

The `shot [name]` console command prints the next frame as
    SHOT <name> <w> <h>
    AREA <x1> <y1> <x2> <y2>
    <one base64 line of RGB565 pixels per row of the area>
    ...
    END

    <PlatformIO python> tools/shots.py <serial log> <output dir>
"""

import base64
import sys
from pathlib import Path

from PIL import Image


def main(log: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    img = name = area = None
    row = 0
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        parts = line.split()
        if line.startswith("SHOT ") and len(parts) == 4:
            name = parts[1]
            img = Image.new("RGB", (int(parts[2]), int(parts[3])))
            area = None
        elif img and line.startswith("AREA ") and len(parts) == 5:
            area = [int(p) for p in parts[1:]]
            row = 0
        elif img and line == "END":
            img.save(out / f"{name}.png")
            print(f"{name}.png")
            img = None
        elif img and area and line and " " not in line:
            try:
                raw = base64.b64decode(line, validate=True)
            except ValueError:
                continue  # a log line from another task in between
            x1, y1, x2, _ = area
            px = img.load()
            for i in range(min(len(raw) // 2, x2 - x1 + 1)):
                v = raw[2 * i] | raw[2 * i + 1] << 8
                px[x1 + i, y1 + row] = ((v >> 11) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3)
            row += 1


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
