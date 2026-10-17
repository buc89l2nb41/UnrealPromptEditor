"""Textures drawn by the agent's GenerateImage, then checked by the user.

1. request_texture registers a name and returns the filePath for GenerateImage.
2. The user approves the picture (or asks for changes).
3. Cutouts and glows get two alpha options: A (colour key or brightness) and B (rembg).
   The user picks one. Tiles are made seamless instead.
4. The final PNG goes to Saved/PromptEditor/gen/<name>.png for import_texture.
"""

from __future__ import annotations

import os
import re
import threading
from pathlib import Path
from typing import Any, Mapping

from PIL import Image, ImageDraw, ImageFont

STAGE_REQUESTED = "tex_image_requested"
STAGE_IMAGE = "tex_pending_image"
STAGE_ALPHA = "tex_pending_alpha"

USES = ("cutout", "glow", "tile")
MAGENTA = (255, 0, 255)

_RULES = {
    "cutout": (
        "a single isolated subject, centered, the whole subject visible with margin around it, "
        "on a perfectly flat solid pure magenta #FF00FF background that fills every pixel around "
        "the subject, no magenta or pink anywhere in the subject, no cast shadow, no ground, "
        "no text, no border, no frame"
    ),
    "glow": (
        "a single glowing subject centered on a pure black #000000 background, everything that "
        "is not part of the glow is pure black, no text, no border, no frame"
    ),
    "tile": (
        "a seamless tileable texture seen straight from above, flat even lighting, no cast "
        "shadows, no perspective, no horizon, nothing standing up out of the surface, the "
        "pattern fills the whole square frame edge to edge, no text, no border"
    ),
}

_SHEET_RULE = (
    "a {cols}x{rows} sprite sheet of one looping animation: {n} equal square cells, one frame "
    "per cell, each frame centered in its cell with empty margin, frames change gradually in "
    "reading order, no grid lines, no numbers"
)

REMBG_MODEL = os.environ.get("REMBG_MODEL", "isnet-general-use").strip() or "isnet-general-use"
_rembg_session: Any = None
_rembg_lock = threading.Lock()


# --- names and paths ----------------------------------------------------------


def sanitize_name(raw: str) -> str:
    text = re.sub(r"[^A-Za-z0-9_]", "_", (raw or "").strip())
    text = re.sub(r"_+", "_", text).strip("_")[:48]
    if not text or text[0].isdigit():
        return ""
    return text


def tex_dir(project_dir: str, name: str) -> Path:
    path = Path(project_dir).resolve() / "Saved" / "PromptEditor" / "textures" / name
    path.mkdir(parents=True, exist_ok=True)
    return path


def raw_png(project_dir: str, name: str) -> Path:
    return tex_dir(project_dir, name) / f"{name}_original.png"


def option_png(project_dir: str, name: str, option: str) -> Path:
    return tex_dir(project_dir, name) / f"{name}_{option.upper()}.png"


def compare_png(project_dir: str, name: str) -> Path:
    return tex_dir(project_dir, name) / f"{name}_compare.png"


def tiled_png(project_dir: str, name: str) -> Path:
    return tex_dir(project_dir, name) / f"{name}_tiled.png"


def final_png(project_dir: str, name: str) -> Path:
    path = Path(project_dir).resolve() / "Saved" / "PromptEditor" / "gen" / f"{name}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def image_prompt(item: Mapping[str, Any]) -> str:
    use = str(item.get("use") or "cutout")
    parts = [str(item.get("description") or "").strip()]
    cols = int(item.get("columns") or 0)
    rows = int(item.get("rows") or 0)
    if use != "tile" and cols > 1 and rows > 0:
        parts.append(_SHEET_RULE.format(cols=cols, rows=rows, n=cols * rows))
    parts.append(_RULES.get(use, _RULES["cutout"]))
    return ". ".join(p for p in parts if p)


def image_agent_message(item: Mapping[str, Any], target: Path) -> str:
    return (
        "You are an image-generation step for a game texture.\n"
        "Call the GenerateImage tool exactly once. Do not use any other tools. "
        "Do not write code or edit files.\n"
        f"description: {image_prompt(item)}\n"
        f"filename: {item.get('name')}.png (filePath {target} if the tool takes a path)\n"
        "aspect_ratio: 1:1. No commentary."
    )


# --- user words ---------------------------------------------------------------

_CHOICE_A = re.compile(r"(?i)(크로마|키잉|chroma|\bkey\b|밝기|첫\s*번째|에이)")
_CHOICE_B = re.compile(r"(?i)(rembg|렘비지|ai\s*배경|두\s*번째|비\b)")
_LETTER = re.compile(r"(?i)(?<![a-z])([ab])(?![a-z])")
_NUMBERED = re.compile(r"(?i)(\d+)\s*(?:번)?\s*[:=\-]?\s*([ab])(?![a-z])")


def parse_choices(message: str, count: int) -> dict[int, str]:
    """Index (0-based) -> 'a' or 'b'. Numbers in the message are 1-based."""
    text = message or ""
    picked: dict[int, str] = {}
    for num, letter in _NUMBERED.findall(text):
        idx = int(num) - 1
        if 0 <= idx < count:
            picked[idx] = letter.lower()
    rest = _NUMBERED.sub(" ", text)
    overall = ""
    letters = {m.lower() for m in _LETTER.findall(rest)}
    if len(letters) == 1:
        overall = letters.pop()
    elif _CHOICE_B.search(rest) and not _CHOICE_A.search(rest):
        overall = "b"
    elif _CHOICE_A.search(rest) and not _CHOICE_B.search(rest):
        overall = "a"
    if overall:
        for idx in range(count):
            picked.setdefault(idx, overall)
    return picked


# --- image helpers ------------------------------------------------------------


def _np():
    import numpy as np

    return np


def _border_color(arr: Any) -> tuple[float, float, float]:
    np = _np()
    h, w = arr.shape[:2]
    band = max(2, min(h, w) // 64)
    border = np.concatenate(
        [
            arr[:band].reshape(-1, 3),
            arr[-band:].reshape(-1, 3),
            arr[:, :band].reshape(-1, 3),
            arr[:, -band:].reshape(-1, 3),
        ]
    )
    med = np.median(border, axis=0)
    return float(med[0]), float(med[1]), float(med[2])


def _is_magenta(key: tuple[float, float, float]) -> bool:
    r, g, b = key
    return r > 150 and b > 150 and g < 110


def _unmix(arr: Any, alpha: Any, key: tuple[float, float, float]) -> Any:
    """Remove the background colour blended into edge pixels."""
    np = _np()
    k = np.array(key, dtype=np.float32)
    a = np.clip(alpha, 1e-3, 1.0)[..., None]
    rgb = (arr - (1.0 - a) * k) / a
    rgb = np.where(alpha[..., None] < 0.02, 0.0, rgb)
    return np.clip(rgb, 0, 255)


def _erode(alpha: Any, px: int) -> Any:
    """Shrink the matte edge where key colour mixed with the subject."""
    from PIL import ImageFilter

    np = _np()
    mask = Image.fromarray((np.clip(alpha, 0, 1) * 255).astype(np.uint8), "L")
    mask = mask.filter(ImageFilter.MinFilter(px * 2 + 1)).filter(ImageFilter.GaussianBlur(px * 0.6))
    return np.minimum(alpha, np.asarray(mask).astype(np.float32) / 255.0)


def _despill_magenta(rgb: Any) -> Any:
    """Pull red and blue down to green wherever magenta tint is left on the subject."""
    np = _np()
    spill = np.clip(np.minimum(rgb[..., 0], rgb[..., 2]) - rgb[..., 1], 0, None)
    out = rgb.copy()
    out[..., 0] -= spill
    out[..., 2] -= spill
    return np.clip(out, 0, 255)


def _to_rgba(rgb: Any, alpha: Any) -> Image.Image:
    np = _np()
    out = np.dstack([rgb, np.clip(alpha, 0, 1) * 255.0]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def key_alpha(img: Image.Image) -> tuple[Image.Image, str]:
    """Option A for cutouts: key out the flat background colour."""
    np = _np()
    arr = np.asarray(img.convert("RGB")).astype(np.float32)
    key = _border_color(arr)
    if _is_magenta(key):
        r, g, b = arr[..., 0], arr[..., 1], arr[..., 2]
        spill = np.clip(np.minimum(r, b) - g, 0, None)
        full = max(1.0, min(key[0], key[2]) - key[1])
        lo, hi = 0.18 * full, 0.85 * full
        alpha = 1.0 - np.clip((spill - lo) / (hi - lo), 0, 1)
        rgb = _despill_magenta(_unmix(arr, alpha, key))
        return _to_rgba(rgb, _erode(alpha, max(1, min(arr.shape[:2]) // 512))), "magenta key"
    dist = np.linalg.norm(arr - np.array(key, dtype=np.float32), axis=2)
    alpha = np.clip((dist - 28.0) / 70.0, 0, 1)
    return _to_rgba(_unmix(arr, alpha, key), alpha), "key #%02x%02x%02x" % tuple(int(c) for c in key)


def glow_alpha(img: Image.Image) -> tuple[Image.Image, str]:
    """Option A for glows: brightness over black becomes alpha."""
    np = _np()
    arr = np.asarray(img.convert("RGB")).astype(np.float32)
    floor = max(_border_color(arr)) + 6.0
    peak = arr.max(axis=2)
    alpha = np.clip((peak - floor) / max(1.0, 255.0 - floor), 0, 1)
    return _to_rgba(_unmix(arr, alpha, (0.0, 0.0, 0.0)), alpha), "brightness alpha"


def rembg_alpha(img: Image.Image, use: str) -> tuple[Image.Image | None, str]:
    """Option B: rembg mask, with the background colour removed from edges."""
    global _rembg_session
    try:
        from rembg import new_session, remove
    except Exception:
        return None, "rembg is not installed (pip install \"rembg[cpu]\")"
    np = _np()
    with _rembg_lock:
        if _rembg_session is None:
            _rembg_session = new_session(REMBG_MODEL)
        mask = remove(img.convert("RGB"), session=_rembg_session, only_mask=True)
    alpha = np.asarray(mask.convert("L")).astype(np.float32) / 255.0
    arr = np.asarray(img.convert("RGB")).astype(np.float32)
    key = (0.0, 0.0, 0.0) if use == "glow" else _border_color(arr)
    rgb = _unmix(arr, alpha, key)
    if _is_magenta(key):
        rgb = _despill_magenta(rgb)
    return _to_rgba(rgb, alpha), f"rembg {REMBG_MODEL}"


def _pot(n: int, cap: int) -> int:
    p = 1
    while p * 2 <= min(n, cap):
        p *= 2
    return max(64, p)


def fit_cutout(img: Image.Image, size: int, anchor: str = "center") -> Image.Image:
    rgba = img.convert("RGBA")
    box = rgba.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    if box:
        x0, y0, x1, y1 = box
        pad = int(max(x1 - x0, y1 - y0) * 0.04)
        rgba = rgba.crop((max(0, x0 - pad), max(0, y0 - pad), min(rgba.width, x1 + pad), min(rgba.height, y1 + pad)))
    side = max(rgba.width, rgba.height)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    top = side - rgba.height if anchor == "bottom" else (side - rgba.height) // 2
    canvas.paste(rgba, ((side - rgba.width) // 2, top))
    return canvas.resize((size, size), Image.LANCZOS)


def fit_sheet(img: Image.Image, cols: int, rows: int, cell: int) -> Image.Image:
    return img.convert("RGBA").resize((cols * cell, rows * cell), Image.LANCZOS)


def make_tileable(img: Image.Image, size: int) -> Image.Image:
    np = _np()
    rgb = img.convert("RGB")
    side = min(rgb.width, rgb.height)
    left = (rgb.width - side) // 2
    top = (rgb.height - side) // 2
    rgb = rgb.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS)
    arr = np.asarray(rgb).astype(np.float32)
    ramp = np.abs(np.linspace(-1.0, 1.0, size, dtype=np.float32))
    w = np.clip((ramp - 0.5) / 0.5, 0, 1)
    w = w * w * (3 - 2 * w)
    rolled = np.roll(arr, size // 2, axis=1)
    arr = arr * (1 - w[None, :, None]) + rolled * w[None, :, None]
    rolled = np.roll(arr, size // 2, axis=0)
    arr = arr * (1 - w[:, None, None]) + rolled * w[:, None, None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")


def tiled_preview(img: Image.Image) -> Image.Image:
    half = img.resize((img.width // 2, img.height // 2), Image.LANCZOS)
    out = Image.new("RGB", (half.width * 2, half.height * 2))
    for x in (0, half.width):
        for y in (0, half.height):
            out.paste(half, (x, y))
    return out


def _checker(size: tuple[int, int], cell: int = 24) -> Image.Image:
    board = Image.new("RGB", size, (200, 200, 200))
    draw = ImageDraw.Draw(board)
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            if (x // cell + y // cell) % 2:
                draw.rectangle([x, y, x + cell - 1, y + cell - 1], fill=(150, 150, 150))
    return board


def compare_image(a: Image.Image, b: Image.Image | None, side: int = 512) -> Image.Image:
    tiles = [a] + ([b] if b is not None else [])
    out = Image.new("RGB", (side * len(tiles), side + 40), (30, 30, 30))
    draw = ImageDraw.Draw(out)
    try:
        font = ImageFont.load_default(size=26)
    except TypeError:
        font = ImageFont.load_default()
    for i, tile in enumerate(tiles):
        thumb = tile.convert("RGBA")
        thumb.thumbnail((side, side), Image.LANCZOS)
        board = _checker((side, side))
        board.paste(thumb, ((side - thumb.width) // 2, (side - thumb.height) // 2), thumb)
        out.paste(board, (i * side, 40))
        draw.text((i * side + 12, 6), "A" if i == 0 else "B (rembg)", fill=(255, 255, 255), font=font)
    return out


# --- stage work ---------------------------------------------------------------


def build_options(project_dir: str, item: dict[str, Any]) -> dict[str, Any]:
    """Write A/B (or the tiled result) for one approved picture. Returns updated item."""
    name = str(item["name"])
    use = str(item.get("use") or "cutout")
    src = Image.open(raw_png(project_dir, name))
    src.load()
    if use == "tile":
        size = _pot(int(item.get("size") or 1024), 2048)
        tile = make_tileable(src, size)
        tile.save(final_png(project_dir, name))
        tiled_preview(tile).save(tiled_png(project_dir, name))
        item.update(final=str(final_png(project_dir, name)), choice="tile", options=[])
        return item
    a_img, a_how = glow_alpha(src) if use == "glow" else key_alpha(src)
    b_img, b_how = rembg_alpha(src, use)
    cols = int(item.get("columns") or 0)
    rows = int(item.get("rows") or 0)

    def fit(img: Image.Image) -> Image.Image:
        if cols > 1 and rows > 0:
            return fit_sheet(img, cols, rows, _pot(int(item.get("size") or 256), 512))
        return fit_cutout(img, _pot(int(item.get("size") or 512), 2048), str(item.get("anchor") or "center"))

    a_img = fit(a_img)
    a_img.save(option_png(project_dir, name, "a"))
    item.update(a=str(option_png(project_dir, name, "a")), a_how=a_how, b="", b_how=b_how)
    if b_img is not None:
        b_img = fit(b_img)
        b_img.save(option_png(project_dir, name, "b"))
        item["b"] = str(option_png(project_dir, name, "b"))
    compare_image(a_img, b_img).save(compare_png(project_dir, name))
    item["compare"] = str(compare_png(project_dir, name))
    return item


def finalize(project_dir: str, item: dict[str, Any], choice: str) -> dict[str, Any]:
    src = Path(str(item.get("b" if choice == "b" else "a") or ""))
    if not src.is_file():
        raise FileNotFoundError(f"option {choice.upper()} is missing for {item.get('name')}")
    dest = final_png(project_dir, str(item["name"]))
    dest.write_bytes(src.read_bytes())
    item.update(final=str(dest), choice=choice)
    return item


def import_kind(item: Mapping[str, Any]) -> str:
    if int(item.get("columns") or 0) > 1 and int(item.get("rows") or 0) > 0:
        return "atlas"
    return "albedo"


def ready_line(item: Mapping[str, Any]) -> str:
    extra = ""
    if import_kind(item) == "atlas":
        extra = f" columns={item.get('columns')} rows={item.get('rows')}"
    alpha = {"a": item.get("a_how"), "b": item.get("b_how"), "tile": "seamless, no alpha"}.get(
        str(item.get("choice") or ""), ""
    )
    return (
        f"- {item.get('name')} use={item.get('use')} png={item.get('final')} "
        f"import_texture kind={import_kind(item)}{extra} ({alpha})"
    )
