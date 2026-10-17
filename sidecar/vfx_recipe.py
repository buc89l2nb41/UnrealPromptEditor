"""Look → recipe numbers. PNG bake stays in generate_png; editor apply is MCP."""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Mapping


def infer_look(description: str, look: str = "") -> str:
    token = (look or "").strip().lower().replace("-", "_").replace(" ", "_")
    aliases = {
        "flipbook": "card_flipbook",
        "atlas": "card_flipbook",
        "card": "card_flipbook",
        "noise": "card_noise",
        "warp": "card_noise",
        "distort": "card_noise",
        "particle": "particles",
        "sprites": "particles",
        "trail": "ribbon",
        "streak": "ribbon",
        "debris": "mesh",
        "chunk": "mesh",
    }
    if token in aliases:
        return aliases[token]
    if token in ("card_flipbook", "card_noise", "particles", "ribbon", "mesh"):
        return token
    low = (description or "").lower()
    if any(w in low for w in ("ribbon", "trail", "streak", "리본", "궤적")):
        return "ribbon"
    if any(w in low for w in ("debris", "chunk", "mesh particle", "메시 파티클", "파편")):
        return "mesh"
    if any(w in low for w in ("warp", "distort", "noise", "흐느적", "왜곡")):
        return "card_noise"
    if any(w in low for w in ("many", "particle", "ember", "spark", "불씨", "파티클", "instances")):
        return "particles"
    return "card_flipbook"


def infer_noise_mic(description: str) -> dict[str, float]:
    low = (description or "").lower()
    if any(w in low for w in ("smoke", "fog", "mist", "연기", "구름")):
        return {"strength": 0.12, "tiling": 1.0, "speed_u": 0.025, "speed_v": 0.018}
    if any(w in low for w in ("spark", "ember", "turbulen", "불씨", "난류")):
        return {"strength": 0.04, "tiling": 2.0, "speed_u": 0.1, "speed_v": 0.08}
    return {"strength": 0.07, "tiling": 1.0, "speed_u": 0.05, "speed_v": 0.035}


def _png_path(line: str) -> str:
    paths: list[str] = []
    for part in (line or "").split():
        if part.startswith("png="):
            paths.append(part[4:])
    picked = ""
    original = ""
    for path in paths:
        stem = Path(path).stem.lower()
        if stem.endswith("_outline"):
            continue
        if stem.endswith("_original"):
            original = path
            continue
        picked = path
        break
    return picked or original or (paths[0] if paths else "")


def call_mcp(mcp_url: str, name: str, arguments: Mapping[str, Any], req_id: int = 1) -> str:
    payload = {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": "tools/call",
        "params": {"name": name, "arguments": dict(arguments)},
    }
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        mcp_url,
        data=data,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            body = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return f"mcp http {exc.code} {name}"
    except Exception as exc:
        return f"mcp error {name}: {exc}"
    if body.get("error"):
        err = body["error"]
        if isinstance(err, dict):
            return f"mcp error {name}: {err.get('message') or err}"
        return f"mcp error {name}: {err}"
    result = body.get("result") or {}
    content = result.get("content") or []
    text = ""
    if content and isinstance(content[0], dict):
        text = str(content[0].get("text") or "")
    if result.get("isError"):
        return f"FAILED {name}: {text or 'error'}"
    return text or f"{name} ok"
