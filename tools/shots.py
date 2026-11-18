"""Turn screenshots from the serial log into PNGs.

The `shot [name]` console command prints the next frame (twice) as
    SHOT <name> <w> <h>
    AREA <x1> <y1> <x2> <y2>
    R<y> <base64 RGB565 pixels of row y of the area>
    ...
    END

Now and then a row loses characters on the serial link. Each row is taken
from a copy of the frame where it arrived whole; a row broken in every copy
is filled with the row above.

    <PlatformIO python> tools/shots.py <serial log> <output dir>
"""

import base64
import re
import sys
from pathlib import Path

from PIL import Image


def main(log: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    shots = {}  # name -> (image, set of (area, y) received whole)
    order = []
    img = good = area = None
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        # PlatformIO's `time` monitor filter puts "12:34:56.789 > " in front
        line = re.sub(r"^\d\d:\d\d:\d\d\.\d+ > ", "", line.strip())
        parts = line.split()
        if line.startswith("SHOT ") and len(parts) == 4:
            name = parts[1]
            if name not in shots:
                shots[name] = (Image.new("RGB", (int(parts[2]), int(parts[3]))), set())
                order.append(name)
            img, good = shots[name]
            area = None
        elif img and line.startswith("AREA ") and len(parts) == 5:
            area = tuple(int(p) for p in parts[1:])
        elif img and line == "END":
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
            if (x1, x2, y) in good:
                continue  # already have it from an earlier copy
            px = img.load()
            for i in range(x2 - x1 + 1):
                v = raw[2 * i] | raw[2 * i + 1] << 8
                px[x1 + i, y] = ((v >> 11) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3)
            good.add((x1, x2, y))

    for name in order:
        img, good = shots[name]
        rows = {y for _, _, y in good}
        missing = [y for y in range(img.height) if y not in rows]
        px = img.load()
        for y in missing:
            if y > 0:
                for x in range(img.width):
                    px[x, y] = px[x, y - 1]
        img.save(out / f"{name}.png")
        print(f"{name}.png" + (f"  ({len(missing)} rows lost in every copy, filled)" if missing else ""))


if __name__ == "__main__":
    main(Path(sys.argv[1]), Path(sys.argv[2]))
