"""Bake a tiling 2D curl-noise vector field to an RGBA PNG.

RG is the signed flow encoded as 0.5-centered color (inspectable).
B is magnitude for preview only; the material samples RG.
Not drawn by an LLM. Numbers (frequency, octaves, seed) drive the field.
"""

from __future__ import annotations

import hashlib
import math
import random
from pathlib import Path

from PIL import Image


def infer_frequency(description: str) -> int:
    low = (description or "").lower()
    if any(w in low for w in ("smoke", "fog", "mist", "연기", "구름")):
        return 3
    if any(w in low for w in ("spark", "ember", "turbulen", "chaotic", "불씨", "난류")):
        return 8
    if any(w in low for w in ("fire", "flame", "campfire", "불", "화염", "모닥")):
        return 5
    return 4


def _seed_int(text: str, explicit: int | None) -> int:
    if explicit is not None:
        return int(explicit) & 0x7FFFFFFF
    return int(hashlib.sha256((text or "noise").encode("utf-8")).hexdigest()[:8], 16)


def _fade(t: float) -> float:
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def _lerp(a: float, b: float, t: float) -> float:
    return a + t * (b - a)


def _make_grid(period: int, seed: int, octave: int) -> list[list[float]]:
    rng = random.Random(seed + octave * 9973)
    period = max(2, period)
    return [[rng.random() * 2.0 - 1.0 for _ in range(period)] for _ in range(period)]


def _sample_grid(grid: list[list[float]], period: int, u: float, v: float) -> float:
    period = max(2, period)
    x = u % period
    y = v % period
    x0 = int(math.floor(x)) % period
    y0 = int(math.floor(y)) % period
    x1 = (x0 + 1) % period
    y1 = (y0 + 1) % period
    fx = x - math.floor(x)
    fy = y - math.floor(y)
    uu = _fade(fx)
    vv = _fade(fy)
    a = _lerp(grid[y0][x0], grid[y0][x1], uu)
    b = _lerp(grid[y1][x0], grid[y1][x1], uu)
    return _lerp(a, b, vv)


def _scalar(nx: float, ny: float, size: int, grids: list[list[list[float]]], periods: list[int]) -> float:
    total = 0.0
    amp = 1.0
    for grid, period in zip(grids, periods):
        total += amp * _sample_grid(grid, period, nx / size * period, ny / size * period)
        amp *= 0.5
    return total


def write_noise_png(
    project_dir: str,
    description: str,
    name: str = "",
    frequency: int | float | None = None,
    octaves: int | float | None = None,
    size: int | float | None = None,
    seed: int | float | None = None,
) -> dict[str, str | int | float]:
    freq = int(frequency) if frequency not in (None, "", 0, 0.0) else infer_frequency(description)
    freq = max(2, min(32, freq))
    oct_count = int(octaves) if octaves not in (None, "", 0, 0.0) else 3
    oct_count = max(1, min(6, oct_count))
    dim = int(size) if size not in (None, "", 0, 0.0) else 256
    dim = max(64, min(1024, dim))
    seed_i = _seed_int(description + "|noise", None if seed in (None, "") else int(seed))

    periods = [max(2, freq * (2**octave)) for octave in range(oct_count)]
    grids = [_make_grid(period, seed_i, octave) for octave, period in enumerate(periods)]

    field: list[tuple[float, float]] = []
    max_mag = 1e-6
    for y in range(dim):
        for x in range(dim):
            vx = _scalar(x, (y + 1) % dim, dim, grids, periods) - _scalar(x, (y - 1) % dim, dim, grids, periods)
            vy = -(
                _scalar((x + 1) % dim, y, dim, grids, periods)
                - _scalar((x - 1) % dim, y, dim, grids, periods)
            )
            mag = math.hypot(vx, vy)
            if mag > max_mag:
                max_mag = mag
            field.append((vx, vy))

    img = Image.new("RGBA", (dim, dim))
    pixels = []
    inv = 1.0 / max_mag
    for vx, vy in field:
        sx = max(-1.0, min(1.0, vx * inv))
        sy = max(-1.0, min(1.0, vy * inv))
        mag = math.hypot(sx, sy)
        r = int(round((sx * 0.5 + 0.5) * 255.0))
        g = int(round((sy * 0.5 + 0.5) * 255.0))
        b = int(round((mag * 0.5 + 0.5) * 255.0))
        pixels.append((r, g, b, 255))
    img.putdata(pixels)

    root = Path(project_dir).resolve()
    stem = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in (name or "noise").strip())[:40] or "noise"
    dest = root / "Saved" / "PromptEditor" / "gen"
    dest.mkdir(parents=True, exist_ok=True)
    path = dest / f"{stem}.png"
    img.save(path, optimize=True)
    return {
        "path": str(path),
        "kind": "noise",
        "frequency": freq,
        "octaves": oct_count,
        "size": dim,
        "seed": seed_i,
    }
