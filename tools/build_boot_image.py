"""Convert the boot artwork to the display's little-endian RGB565 format."""
import argparse
import struct
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    image = Image.open(args.source).convert("RGB")
    if image.width * 4 != image.height * 3:
        raise ValueError("Boot artwork must have a 3:4 aspect ratio")
    image = image.resize((240, 320), Image.Resampling.LANCZOS)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rgb = image.tobytes()
    pixels = [(rgb[i] >> 3) << 11 | (rgb[i + 1] >> 2) << 5 | rgb[i + 2] >> 3
              for i in range(0, len(rgb), 3)]
    args.output.write_bytes(struct.pack("<76800H", *pixels))
    print(f"Wrote {args.output} (240 x 320 RGB565)")


if __name__ == "__main__":
    main()
