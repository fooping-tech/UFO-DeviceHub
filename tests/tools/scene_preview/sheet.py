"""Contact sheet of the frames written by scene_preview (one row per scene)."""
import glob
import os
import sys

from PIL import Image

d, out = sys.argv[1], sys.argv[2]
names = sys.argv[3:] or ["standby", "ready", "waiting", "gotball", "release", "done", "nohub"]
Z = 3  # as on the device: one scene pixel = 3 x 3
rows = [[Image.open(f).resize((80 * Z, 80 * Z), Image.NEAREST) for f in sorted(glob.glob(os.path.join(d, n + "_*.ppm")))]
        for n in names]
cell = 80 * Z + 4
sheet = Image.new("RGB", (max(len(r) for r in rows) * cell, len(rows) * cell), (60, 60, 60))
for y, r in enumerate(rows):
    for x, im in enumerate(r):
        sheet.paste(im, (x * cell, y * cell))
sheet.save(out)
