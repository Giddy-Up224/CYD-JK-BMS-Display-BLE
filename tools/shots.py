"""Turn screenshots from the serial log into PNGs.

The `shot [name]` console command prints the next frame as
    SHOT <name> <w> <h>
    AREA <x1> <y1> <x2> <y2>
    R<y> <base64 RGB565 pixels of row y of the area>
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
    filled = set()
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        parts = line.split()
        if line.startswith("SHOT ") and len(parts) == 4:
            name = parts[1]
            img = Image.new("RGB", (int(parts[2]), int(parts[3])))
            area = None
        elif img and line.startswith("AREA ") and len(parts) == 5:
            area = [int(p) for p in parts[1:]]
        elif img and line == "END":
            # rows lost on the serial link: repeat the row above
            px = img.load()
            for y in range(1, img.height):
                if y not in filled and y - 1 in filled:
                    for x in range(img.width):
                        px[x, y] = px[x, y - 1]
                    filled.add(y)
            filled = set()
            img.save(out / f"{name}.png")
            print(f"{name}.png")
            img = None
        elif img and area and line.startswith("R") and len(parts) == 2:
            try:
                y = int(parts[0][1:])
                raw = base64.b64decode(parts[1], validate=True)
            except ValueError:
                continue  # garbled on the way
            x1, _, x2, _ = area
            if len(raw) != 2 * (x2 - x1 + 1) or not 0 <= y < img.height:
                continue
            px = img.load()
            for i in range(x2 - x1 + 1):
                v = raw[2 * i] | raw[2 * i + 1] << 8
                px[x1 + i, y] = ((v >> 11) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3)
            filled.add(y)


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
