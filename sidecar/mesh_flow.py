"""
Image -> mesh flow: reference picture, local TRELLIS (StableProjectorz API), Unreal preview/apply.

TRELLIS location comes only from sidecar/.env (TRELLIS_URL, optional TRELLIS_HOME).
"""

from __future__ import annotations

import base64
import binascii
import json
import os
import re
import threading
import time
import urllib.error
import urllib.request
import uuid
from collections.abc import Callable
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Mapping

DEFAULT_TRELLIS_URL = "http://127.0.0.1:7960"
MIN_GLB_BYTES = 10_000
GENERATE_TIMEOUT_S = 30 * 60
PREVIEW_ROOT = "/Game/Meshes/Preview"
PREVIEW_LABEL = "MeshPreview"

TRELLIS_FORM = {
    "resolution": "512",
    "num_inference_steps": "12",
    "guidance_scale": "7.5",
    "mesh_simplify": "50",
    "texture_size": "1024",
    "apply_texture": "true",
    "output_format": "glb",
}

STAGE_IMAGE_REQUESTED = "image_requested"
STAGE_IMAGE = "pending_mesh_image"
STAGE_APPLY = "pending_mesh_apply"
STAGE_NAME = "pending_mesh_name"

IMAGE_PROMPT_RULES = (
    "a single isolated object, centered, full object visible, "
    "plain white background, no shadows, no text, no other objects"
)


class Cancelled(Exception):
    pass


# --- settings --------------------------------------------------------------


def trellis_url() -> str:
    raw = os.environ.get("TRELLIS_URL", "").strip().strip('"').strip("'")
    return (raw or DEFAULT_TRELLIS_URL).rstrip("/")


def trellis_start_hint() -> str:
    home = os.environ.get("TRELLIS_HOME", "").strip().strip('"').strip("'")
    launcher = Path(__file__).resolve().parent / "start-trellis.bat"
    if home and (Path(home) / "code" / "api_spz" / "main_api.py").is_file():
        return f"{launcher} 를 실행해 주세요 (첫 기동은 몇 분 걸립니다)."
    return (
        "TRELLIS(StableProjectorz API)를 켜고 sidecar/.env 의 TRELLIS_URL 을 확인해 주세요. "
        "TRELLIS_HOME 을 적어 두면 sidecar/start-trellis.bat 로 켤 수 있습니다."
    )


# --- storage ----------------------------------------------------------------


def meshes_root(project_dir: str) -> Path:
    return Path(project_dir) / "Saved" / "EditorPrompt" / "meshes"


def sanitize_mesh_id(raw: str) -> str:
    return "".join(ch for ch in (raw or "").strip() if ch.isalnum() or ch in "-_")[:80]


def mesh_dir(project_dir: str, mesh_id: str) -> Path:
    mid = sanitize_mesh_id(mesh_id)
    if not mid:
        raise ValueError("mesh_id is empty")
    root = meshes_root(project_dir)
    path = (root / mid).resolve()
    path.relative_to(root.resolve())
    path.mkdir(parents=True, exist_ok=True)
    return path


def mint_mesh_id() -> str:
    return str(uuid.uuid4())


def read_mesh_meta(project_dir: str, mesh_id: str) -> dict[str, Any]:
    try:
        path = mesh_dir(project_dir, mesh_id) / "meta.json"
    except (OSError, ValueError):
        return {}
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}
    return data if isinstance(data, dict) else {}


def write_mesh_meta(project_dir: str, mesh_id: str, **fields: Any) -> dict[str, Any]:
    meta = read_mesh_meta(project_dir, mesh_id)
    meta.update(fields)
    meta["id"] = sanitize_mesh_id(mesh_id)
    meta["modified"] = datetime.now(timezone.utc).isoformat()
    meta.setdefault("created", meta["modified"])
    meta.setdefault("backend", "trellis")
    path = mesh_dir(project_dir, mesh_id) / "meta.json"
    path.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
    return meta


def ref_png(project_dir: str, mesh_id: str) -> Path:
    return mesh_dir(project_dir, mesh_id) / "ref.png"


def out_glb(project_dir: str, mesh_id: str) -> Path:
    return mesh_dir(project_dir, mesh_id) / "out.glb"


def named_glb(project_dir: str, mesh_id: str, asset_name: str) -> Path:
    """Interchange names the mesh after the file, so import a copy named like the asset."""
    source = out_glb(project_dir, mesh_id)
    target = source.with_name(f"{asset_name}.glb")
    target.write_bytes(source.read_bytes())
    return target


def preview_folder(mesh_id: str) -> str:
    short = sanitize_mesh_id(mesh_id).replace("-", "")[:12] or "pending"
    return f"{PREVIEW_ROOT}/P_{short}"


# --- user words -------------------------------------------------------------

_NEGATIVE = re.compile(
    r"(?i)(안\s*좋|별로|아니|싫|그만|취소|안\s*할|안\s*해|하지\s*마|\bno\b|\bnope\b|\bcancel\b|\bstop\b)"
)
_APPROVE = re.compile(
    r"(?i)(좋아|좋네|좋습니다|좋다|괜찮|이걸로|그걸로|이거로|마음에\s*들|그래|응\b|네\b|"
    r"\bok\b|\bokay\b|\bgood\b|\byes\b|\bgo\b|\blgtm\b)"
)
_APPROVE_MAKE = re.compile(r"(?i)(진행|만들어|뽑아|생성해|\bmake\b|\bgenerate\b)")
_MESH_WORD = re.compile(r"(?i)(모델|메쉬|3d|트렐리스|trellis|\bmesh\b|\bmodel\b)")
_CHANGE = re.compile(r"(더\s|더$|바꿔|바꾸|수정|변경|말고|대신|빼고|넣고|크게|작게|색|\bmore\b|\bchange\b|\binstead\b)")
_APPLY = re.compile(r"(?i)(적용|확정|붙여|넣어|배치|이걸로|그걸로|좋아|괜찮|\bapply\b|\bok\b|\byes\b)")
_RETRY = re.compile(r"(?i)(다시\s*(만들|뽑|생성|해)|재생성|한\s*번\s*더|\bretry\b|\bregenerate\b|\bagain\b)")
_BACK_TO_IMAGE = re.compile(r"(?i)(그림부터|이미지부터|그림\s*다시|이미지\s*다시|그림을?\s*바꿔|이미지를?\s*바꿔)")
_CANCEL = re.compile(r"(?i)(취소|그만|안\s*할래|안\s*해|필요\s*없|\bcancel\b|\bstop\b)")
_HOW = re.compile(r"(?i)(\?|어떻게|왜|뭐야|뭔가요|설명|\bhow\b|\bwhy\b|\bwhat\b)")

_NAME_STOPWORDS = {
    "ok", "okay", "yes", "no", "good", "go", "lgtm", "apply", "retry", "again",
    "cancel", "stop", "regenerate", "glb", "mesh", "model", "preview", "live",
}


def is_question(message: str) -> bool:
    return bool(_HOW.search(message or ""))


def is_approve(message: str) -> bool:
    text = message or ""
    if is_question(text) or _NEGATIVE.search(text) or _CHANGE.search(text):
        return False
    if _APPROVE.search(text):
        return True
    if _APPROVE_MAKE.search(text):
        return bool(_MESH_WORD.search(text)) or len(text.strip()) <= 8
    return False


def is_apply(message: str) -> bool:
    text = message or ""
    if is_question(text) or _NEGATIVE.search(text):
        return False
    return bool(_APPLY.search(text))


def is_retry(message: str) -> bool:
    return bool(_RETRY.search(message or "")) and not is_question(message)


def is_back_to_image(message: str) -> bool:
    return bool(_BACK_TO_IMAGE.search(message or ""))


def is_cancel(message: str) -> bool:
    return bool(_CANCEL.search(message or "")) and not is_question(message)


def sanitize_mesh_name(raw: str) -> str:
    text = (raw or "").strip().strip("'\"")
    text = text.replace("\\", "/").rstrip("/").split("/")[-1]
    if text.lower().endswith(".uasset"):
        text = text[: -len(".uasset")]
    text = re.sub(r"[^A-Za-z0-9_]", "", text)
    if text.lower().startswith("sm_"):
        text = text[3:]
    if not text or text[0].isdigit() or text.lower() in _NAME_STOPWORDS:
        return ""
    return text[:40]


def extract_mesh_name(message: str) -> str:
    text = (message or "").strip()
    if not text:
        return ""
    for pattern in (
        r"/Game/Meshes/(?:[A-Za-z0-9_]+/)?(?:SM_)?([A-Za-z][A-Za-z0-9_]{1,39})",
        r"\bSM_([A-Za-z][A-Za-z0-9_]{1,39})\b",
        r"(?:이름은|이름|name(?:\s+it)?|call it|as)\s*[:：]?\s*[\"']?([A-Za-z][A-Za-z0-9_]{1,39})",
        r"([A-Za-z][A-Za-z0-9_]{1,39})\s*(?:으로|로|라고)(?:\s|$)",
    ):
        hit = re.search(pattern, text, re.I)
        if hit:
            name = sanitize_mesh_name(hit.group(1))
            if name:
                return name
    compact = re.sub(r"\s+", "", text)
    if re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{1,39}", compact):
        return sanitize_mesh_name(compact)
    return ""


# --- GenerateImage result ---------------------------------------------------


def is_generate_image_tool(name: str) -> bool:
    normalized = re.sub(r"[^a-z0-9]+", "", (name or "").lower())
    return "generateimage" in normalized


def _decode_image(raw: str) -> bytes | None:
    payload = raw.strip()
    if payload.startswith("data:"):
        payload = payload.partition(",")[2]
    try:
        data = base64.b64decode(payload, validate=True)
    except (binascii.Error, ValueError):
        return None
    return data if data[:8] == b"\x89PNG\r\n\x1a\n" or data[:3] == b"\xff\xd8\xff" else None


def _from_mapping(data: Mapping[str, Any]) -> bytes | Path | None:
    for holder in (data.get("value"), data):
        if not isinstance(holder, Mapping):
            continue
        file_path = holder.get("filePath") or holder.get("file_path") or holder.get("path")
        if isinstance(file_path, str) and file_path.strip():
            return Path(file_path.strip())
        image_data = holder.get("imageData") or holder.get("image_data")
        if isinstance(image_data, str) and image_data.strip():
            return _decode_image(image_data)
    return None


_IMAGE_PATH = re.compile(r"([A-Za-z]:[\\/][^\r\n\"'<>|?*]+?\.(?:png|jpe?g|webp))", re.I)


def extract_image_result(result: Any) -> bytes | Path | None:
    if result is None:
        return None
    if isinstance(result, Path):
        return result
    if isinstance(result, (bytes, bytearray)):
        return bytes(result)
    if isinstance(result, Mapping):
        return _from_mapping(result)
    if isinstance(result, str):
        text = result.strip()
        if text.startswith(("{", "[")):
            try:
                parsed = json.loads(text)
            except json.JSONDecodeError:
                parsed = None
            if isinstance(parsed, Mapping):
                return _from_mapping(parsed)
        hit = _IMAGE_PATH.search(text)
        if hit and Path(hit.group(1)).is_file():
            return Path(hit.group(1))
        return _decode_image(text)
    for attr in ("value", "result"):
        inner = getattr(result, attr, None)
        if inner is not None and inner is not result:
            got = extract_image_result(inner)
            if got is not None:
                return got
    return None


def _is_image_file(path: Path) -> bool:
    try:
        head = path.read_bytes()[:8]
    except OSError:
        return False
    return head[:8] == b"\x89PNG\r\n\x1a\n" or head[:3] == b"\xff\xd8\xff"


def settle_ref_image(target: Path, results: list[Any]) -> bool:
    """Put the generated picture at target. True when target is a real image."""
    for result in reversed(results):
        got = extract_image_result(result)
        if isinstance(got, bytes):
            target.write_bytes(got)
            break
        if isinstance(got, Path) and got.is_file():
            try:
                if got.resolve() != target.resolve():
                    target.write_bytes(got.read_bytes())
            except OSError:
                continue
            break
    if target.is_file() and _is_image_file(target):
        _to_png(target)
        return True
    return False


def _to_png(path: Path) -> None:
    try:
        from PIL import Image

        with Image.open(path) as img:
            if img.format == "PNG":
                return
            img.convert("RGBA").save(path, format="PNG")
    except Exception:
        pass


def image_agent_message(description: str, target: Path) -> str:
    return (
        "You are an image-generation step for a 3D mesh pipeline.\n"
        "Call the GenerateImage tool exactly once. Do not use any other tools. "
        "Do not write code or edit files.\n"
        f"description: {description}. {IMAGE_PROMPT_RULES}\n"
        f"filePath: {target}\n"
        "Do not change the path. No commentary."
    )


# --- TRELLIS ----------------------------------------------------------------


def _http_json(method: str, url: str, timeout: float, data: bytes | None = None,
               headers: Mapping[str, str] | None = None) -> tuple[int, Any]:
    req = urllib.request.Request(url, data=data, method=method, headers=dict(headers or {}))
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read()
            code = resp.status
    except urllib.error.HTTPError as exc:
        body = exc.read() if hasattr(exc, "read") else b""
        code = exc.code
    try:
        return code, json.loads(body.decode("utf-8") or "null")
    except Exception:
        return code, body.decode("utf-8", "replace")[:300]


def trellis_ping(base: str) -> tuple[bool, bool, str]:
    """(reachable, busy, error)."""
    try:
        code, body = _http_json("GET", base + "/ping", timeout=4)
    except Exception as exc:
        return False, False, str(exc)
    if code != 200 or not isinstance(body, Mapping):
        return False, False, f"http {code}"
    return True, bool(body.get("busy")), ""


def _multipart(fields: Mapping[str, str], file_field: str, file_path: Path) -> tuple[bytes, str]:
    boundary = "----EditorPrompt" + uuid.uuid4().hex
    parts: list[bytes] = []
    for key, value in fields.items():
        parts.append(
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"{key}\"\r\n\r\n{value}\r\n".encode()
        )
    parts.append(
        (
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"{file_field}\"; "
            f"filename=\"{file_path.name}\"\r\nContent-Type: image/png\r\n\r\n"
        ).encode()
        + file_path.read_bytes()
        + b"\r\n"
    )
    parts.append(f"--{boundary}--\r\n".encode())
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


def trellis_generate(
    base: str,
    image: Path,
    target: Path,
    seed: int,
    emit: Callable[[str], None],
    should_stop: Callable[[], bool],
) -> int:
    """Run one generation and write the GLB. Returns its byte size."""
    fields = dict(TRELLIS_FORM)
    fields["seed"] = str(seed)
    body, ctype = _multipart(fields, "file", image)
    outcome: dict[str, Any] = {}

    def post() -> None:
        try:
            outcome["result"] = _http_json(
                "POST",
                base + "/generate_no_preview",
                timeout=GENERATE_TIMEOUT_S,
                data=body,
                headers={"Content-Type": ctype},
            )
        except Exception as exc:
            outcome["error"] = str(exc)

    worker = threading.Thread(target=post, daemon=True, name="editor-prompt-trellis")
    worker.start()
    started = time.monotonic()
    last_note = 0.0
    interrupted = False
    while worker.is_alive():
        worker.join(timeout=1.0)
        if should_stop() and not interrupted:
            interrupted = True
            emit("TRELLIS 중지 요청")
            try:
                _http_json("POST", base + "/interrupt", timeout=5, data=b"")
            except Exception:
                pass
        elapsed = time.monotonic() - started
        if not interrupted and elapsed - last_note >= 15:
            last_note = elapsed
            note = ""
            try:
                code, status = _http_json("GET", base + "/status", timeout=3)
                if code == 200 and isinstance(status, Mapping):
                    note = str(status.get("message") or "")
            except Exception:
                pass
            emit(f"TRELLIS {int(elapsed)}s {note}".strip())
    if interrupted:
        raise Cancelled()
    if "error" in outcome:
        raise RuntimeError("TRELLIS 요청 실패: " + outcome["error"][:200])
    code, result = outcome.get("result", (0, None))
    if code == 499:
        raise Cancelled()
    if code == 503:
        raise RuntimeError("TRELLIS가 다른 생성 중입니다. 끝난 뒤 다시 말해 주세요.")
    if code != 200:
        detail = result.get("detail") if isinstance(result, Mapping) else result
        raise RuntimeError(f"TRELLIS 실패 (http {code}): {str(detail)[:200]}")
    req = urllib.request.Request(base + "/download/model", method="GET")
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = resp.read()
    if len(data) < MIN_GLB_BYTES or data[:4] != b"glTF":
        raise RuntimeError(f"TRELLIS 결과 GLB가 비정상입니다 ({len(data)} bytes).")
    target.write_bytes(data)
    return len(data)
