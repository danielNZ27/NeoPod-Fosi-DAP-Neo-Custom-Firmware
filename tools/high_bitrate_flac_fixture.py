"""Regenerate the incompressible 24-bit/44.1 kHz FLAC regression fixture.

Requires ffmpeg on PATH. PCM expectations come from the input, not the decoder.
"""
from pathlib import Path
import random
import math
import struct
import subprocess
import tempfile


def main():
    fixtures = Path(__file__).resolve().parents[1] / "tests/fixtures/flac"
    rng = random.Random(2062)
    samples = [rng.randrange(-8388608, 8388608) for _ in range(32768 * 2)]
    raw = b"".join((sample & 0xFFFFFF).to_bytes(3, "little") for sample in samples)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "noise.raw"
        source.write_bytes(raw)
        subprocess.run([
            "ffmpeg", "-v", "error", "-y", "-f", "s24le", "-ar", "44100",
            "-ac", "2", "-i", str(source), "-c:a", "flac", "-compression_level",
            "8", "-frame_size", "4096", str(fixtures / "high_bitrate_24_44k.flac")
        ], check=True)
    (fixtures / "high_bitrate_24_44k.pcm").write_bytes(
        b"".join(struct.pack("<i", sample) for sample in samples))
    # Model the 47-sample FLAC block size found in the reported track,
    # using a synthetic signal rather than distributing music samples.
    samples = [int(6500000 * math.sin(2 * math.pi * (440 + ch * 220) * i / 44100))
               + ((i * 7919) % 511) for i in range(12001) for ch in range(2)]
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "tiny.raw"
        source.write_bytes(b"".join((x & 0xFFFFFF).to_bytes(3, "little") for x in samples))
        subprocess.run([
            "ffmpeg", "-v", "error", "-y", "-f", "s24le", "-ar", "44100",
            "-ac", "2", "-i", str(source), "-c:a", "flac", "-frame_size", "47",
            str(fixtures / "tiny_blocks_24_44k.flac")
        ], check=True)
    (fixtures / "tiny_blocks_24_44k.pcm").write_bytes(
        b"".join(struct.pack("<i", sample) for sample in samples))


if __name__ == "__main__":
    main()
