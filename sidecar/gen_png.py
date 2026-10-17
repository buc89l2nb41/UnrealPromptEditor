"""Data textures that shaders read: curl-noise flow fields and soft masks (stdlib only).

Pictures (sprites, cards, ground) come from GenerateImage via tex_flow.
"""

from __future__ import annotations

import hashlib
import math
import struct
import zlib
from pathlib import Path

from gen_noise import write_noise_png


def _chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_rgba_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * stride : (y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + _chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def _seed(text: str) -> int:
    return int(hashlib.sha256(text.encode("utf-8")).hexdigest()[:8], 16)


def _noise(x: float, y: float, seed: int) -> float:
    n = math.sin(x * 12.9898 + y * 78.233 + seed * 0.001) * 43758.5453
    return n - math.floor(n)


def _pixel_mask(x: int, y: int, size: int, seed: int, description: str) -> tuple[int, int, int, int]:
    cx = cy = (size - 1) * 0.5
    dx = (x - cx) / (size * 0.42)
    dy = (y - cy) / (size * 0.42)
    d = math.sqrt(dx * dx + dy * dy)
    edge = 0.55 if "soft" in description.lower() or "부드" in description else 0.35
    a = max(0.0, min(1.0, 1.0 - (d - (1.0 - edge)) / edge))
    a *= 0.85 + 0.15 * _noise(x * 0.08, y * 0.08, seed)
    v = int(255 * a)
    return v, v, v, v


def write_generated_png(
    project_dir: str,
    description: str,
    kind: str = "",
    name: str = "",
    frequency: object = None,
    octaves: object = None,
    size: object = None,
    seed: object = None,
) -> str:
    root = Path(project_dir).resolve()
    kind_l = (kind or "").strip().lower()
    if kind_l in ("noise", "flow", "vector"):
        info = write_noise_png(
            project_dir,
            description,
            name=name,
            frequency=frequency,
            octaves=octaves,
            size=size,
            seed=seed,
        )
        return (
            f"png={info['path']} kind=noise frequency={info['frequency']} "
            f"octaves={info['octaves']} size={info['size']} seed={info['seed']}"
        )
    if kind_l == "mask":
        stem = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in (name or kind_l).strip())[:40] or kind_l
        digest = hashlib.sha1(description.encode("utf-8")).hexdigest()[:8]
        path = root / "Saved" / "EditorPrompt" / "gen" / f"{stem}_{digest}.png"
        px = 256
        mask_seed = _seed(description + kind_l)
        buf = bytearray(px * px * 4)
        i = 0
        for y in range(px):
            for x in range(px):
                buf[i : i + 4] = bytes(_pixel_mask(x, y, px, mask_seed, description))
                i += 4
        write_rgba_png(path, px, px, bytes(buf))
        return f"png={path} kind=mask"
    return (
        "kind must be noise or mask. Pictures (sprites, cards, ground textures) come from "
        "request_texture and GenerateImage."
    )
