"""Saves the staff remote's (M5StopWatch) screen as a PNG, over its USB serial.

    uv run --no-project --with pyserial --with pillow python tests/tools/remote_screenshot.py /dev/cu.usbmodem101 out.png

The firmware answers "S" with "SHOT w h" and the screen as raw RGB565 (big endian).
"""

import sys
import time

import serial
from PIL import Image


def main() -> None:
    port, out = sys.argv[1], sys.argv[2]
    s = serial.Serial(port, 115200, timeout=3)
    s.reset_input_buffer()
    s.write(b"S")
    deadline = time.time() + 5
    while time.time() < deadline:
        line = s.readline().decode(errors="replace").strip()
        if line.startswith("SHOT "):
            break
    else:
        raise SystemExit("no SHOT answer (is the staff remote firmware running?)")
    w, h = map(int, line.split()[1:3])
    raw = b""
    while len(raw) < w * h * 2 and time.time() < deadline + 10:
        raw += s.read(w * h * 2 - len(raw))
    img = Image.new("RGB", (w, h))
    px = img.load()
    for i in range(w * h):
        v = (raw[2 * i] << 8) | raw[2 * i + 1]
        px[i % w, i // w] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    img.save(out)
    print(f"saved {out} ({w}x{h})")


if __name__ == "__main__":
    main()
