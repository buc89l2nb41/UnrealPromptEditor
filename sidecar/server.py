"""
Local HTTP sidecar: Unreal Editor Prompt panel <-> Cursor SDK.

Auth: CURSOR_API_KEY in sidecar/.env (never commit keys).
MCP: Unreal plugin at mcp_url (per-editor).

  copy .env.example .env
  uvicorn server:app --host 127.0.0.1 --port 18766
"""

from __future__ import annotations

import os
import json
import re
import tempfile
import threading
import traceback
import uuid
from collections.abc import Callable
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Mapping

import mesh_flow as mf
import tex_flow as tf
from gen_png import write_generated_png
from vfx_json import RECORD_SCHEMAS, save_vfx_record
from win_os_patch import patch_cursor_sdk_bridge, patch_os_blocking

patch_os_blocking()

from dotenv import load_dotenv
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

load_dotenv(Path(__file__).resolve().parent / ".env")

app = FastAPI(title="Editor Prompt Sidecar", version="0.1.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

FALLBACK_MODEL = os.environ.get("CURSOR_AI_MODEL", "composer-2.5").strip() or "composer-2.5"
DEFAULT_MCP = "http://127.0.0.1:18765/mcp"
WORKDIR = os.environ.get(
    "EDITOR_PROMPT_WORKDIR",
    os.path.join(tempfile.gettempdir(), "editor_prompt_sidecar"),
)
# Prompt budget only. Disk turns/ are never deleted.
# Chars are the real cap so short Q&A can keep more than a handful of turns.
# Turn count is a safety net for tiny messages.
HISTORY_MAX_TURNS = 80
HISTORY_MAX_CHARS = 24000
SUMMARY_MAX_LINES = 16
SEARCH_MAX_HITS = 4

def _get_template_docs_path() -> Path:
    """Return path to NiagaraTemplates docs folder."""
    # Try relative to sidecar (development)
    sidecar = Path(__file__).resolve().parent
    docs = sidecar.parent / "Docs" / "NiagaraTemplates"
    if docs.is_dir():
        return docs
    # Fallback: absolute path
    return Path("D:/Cursor/UnrealPrompt/PromptEditor/Docs/NiagaraTemplates")


def _read_template_doc(filename: str) -> str:
    """Read a template doc MD file."""
    docs = _get_template_docs_path()
    path = docs / filename
    if not path.is_file():
        return f"Template doc not found: {filename}"
    try:
        return path.read_text(encoding="utf-8")
    except Exception as exc:
        return f"Error reading {filename}: {exc}"


_PROMPT_DIR = Path(__file__).resolve().parent / "prompts"
_TEMPLATES_PATH = Path(__file__).resolve().parent / "vfx" / "templates.json"
_text_file_cache: dict[Path, tuple[float, str]] = {}


def _read_text_cached(path: Path) -> str:
    try:
        mtime = path.stat().st_mtime
    except OSError:
        return ""
    cached = _text_file_cache.get(path)
    if cached and cached[0] == mtime:
        return cached[1]
    text = path.read_text(encoding="utf-8")
    _text_file_cache[path] = (mtime, text)
    return text


def _read_prompt_file(name: str) -> str:
    return _read_text_cached(_PROMPT_DIR / name)


def _load_templates() -> dict[str, Any]:
    raw = _read_text_cached(_TEMPLATES_PATH)
    if not raw.strip():
        return {}
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return {}
    return data if isinstance(data, dict) else {}


def _template_line(t: Mapping[str, Any]) -> str:
    motion = "/".join(t.get("motion") or []) or "-"
    look = t.get("look") or "-"
    return (
        f"- {t.get('id')} [{t.get('kind')}, look={look}]: "
        f"form={t.get('form')}, emission={t.get('emission')}, motion={motion}, "
        f"orientation={t.get('orientation')}. {t.get('summary', '')}"
    )


def _vfx_template_guide() -> str:
    data = _load_templates()
    if not data:
        return ""
    vocab = data.get("vocabulary") or {}
    lines = ["VFX template catalog. Pick by visible form, not by effect name."]
    for axis in ("form", "emission", "motion", "orientation"):
        terms = vocab.get(axis) or {}
        if terms:
            lines.append(f"{axis}: " + "; ".join(f"{k} = {v}" for k, v in terms.items()))
    lines.append("Templates and approaches:")
    lines.extend(_template_line(t) for t in data.get("templates") or [])
    adjustments = data.get("param_adjustments") or []
    if adjustments:
        lines.append("Parameter adjustment words:")
        lines.extend(
            f"- {'/'.join(a.get('keywords') or [])} -> {a.get('change', '')}" for a in adjustments
        )
    notes = data.get("guide_notes") or []
    if notes:
        lines.append("Notes:")
        lines.extend(f"- {n}" for n in notes)
    return "\n".join(lines)


def _system_prompt(project_dir: str) -> str:
    raw = _read_prompt_file("system_prompt.md")
    if not raw.strip():
        return (
            "You drive Unreal Editor through MCP tools.\n"
            f"The Unreal project root (cwd) is:\n{project_dir}\n"
        )
    return (
        raw.replace("{project_dir}", project_dir)
        .replace("{vfx_template_guide}", _vfx_template_guide())
    )


@dataclass
class Session:
    agent: Any
    project_dir: str
    chat_id: str
    model: str


_sessions: dict[tuple[str, str], Session] = {}
_run_lock = threading.Lock()
_cancel_requested: set[tuple[str, str]] = set()
_active_runs: dict[tuple[str, str], Any] = {}
_jobs: dict[tuple[str, str], "JobState"] = {}
JOB_EVENT_CAP = 200


@dataclass
class JobState:
    running: bool = True
    events: list[str] = field(default_factory=list)
    images: list[str] = field(default_factory=list)
    reply: str = ""
    error: str = ""
    cancelled: bool = False


class ModelParamIn(BaseModel):
    id: str = ""
    value: str = ""


class PromptRequest(BaseModel):
    message: str
    mcp_url: str = DEFAULT_MCP
    project_dir: str = ""
    chat_id: str = "default"
    model: str = ""
    model_params: list[ModelParamIn] = []
    images: list[str] = []
    effect_id: str = ""
    new_effect: bool = False


class EffectBindRequest(BaseModel):
    project_dir: str = ""
    chat_id: str = "default"
    effect_id: str = ""


class EffectBindResponse(BaseModel):
    ok: bool = False
    effect_id: str = ""
    path: str = ""
    error: str = ""


class PromptResponse(BaseModel):
    reply: str = ""
    error: str = ""
    running: bool = False
    images: list[str] = []


class CancelRequest(BaseModel):
    project_dir: str = ""
    chat_id: str = "default"


class StatusRequest(BaseModel):
    project_dir: str = ""
    chat_id: str = "default"
    after: int = 0


class StatusResponse(BaseModel):
    running: bool = False
    events: list[str] = []
    after: int = 0
    reply: str = ""
    error: str = ""
    images: list[str] = []


def _ensure_workdir() -> str:
    os.makedirs(WORKDIR, exist_ok=True)
    return WORKDIR


def _api_key() -> str:
    key = os.environ.get("CURSOR_API_KEY", "").strip().strip('"').strip("'")
    if not key:
        return ""
    lowered = key.lower()
    if (
        "your_api_key" in lowered
        or "api_key_here" in lowered
        or key.endswith("_here")
        or key == "cursor_..."
        or "xxxxxxxx" in lowered
    ):
        return ""
    return key


def _chat_id(raw: str) -> str:
    s = (raw or "default").strip() or "default"
    cleaned = "".join(ch for ch in s if ch.isalnum() or ch in "-_")
    cleaned = cleaned[:80]
    return cleaned or "default"


def _model(raw: str) -> str:
    s = (raw or "").strip()
    if s:
        return s
    return FALLBACK_MODEL


def _model_fingerprint(model: str, params: list[ModelParamIn]) -> str:
    parts = [model]
    for param in params:
        pid = (param.id or "").strip()
        pval = (param.value or "").strip()
        if pid:
            parts.append(f"{pid}={pval}")
    return "|".join(parts)


def _clean_params(params: list[ModelParamIn]) -> list[ModelParamIn]:
    out: list[ModelParamIn] = []
    for param in params or []:
        pid = (param.id or "").strip()
        if pid:
            out.append(ModelParamIn(id=pid, value=(param.value or "").strip()))
    return out


def _resolve_project_dir(raw: str) -> str:
    text = (raw or "").strip()
    if not text:
        return _ensure_workdir()
    try:
        path = Path(text).expanduser().resolve()
    except OSError:
        return ""
    if not path.is_dir():
        return ""
    return str(path)


IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".tif", ".tiff"}
MAX_IMAGES = 8
MAX_IMAGE_BYTES = 12 * 1024 * 1024


def _resolve_images(project_dir: str, raw: list[str]) -> tuple[list[str], str]:
    root = Path(project_dir).resolve()
    out: list[str] = []
    for item in raw or []:
        text = str(item or "").strip()
        if not text:
            continue
        try:
            path = Path(text).expanduser().resolve()
        except OSError:
            return [], "invalid image path"
        if not path.is_file():
            return [], "image file not found: " + path.name
        try:
            path.relative_to(root)
        except ValueError:
            return [], "image must be under the Unreal project"
        if path.suffix.lower() not in IMAGE_EXTS:
            return [], "unsupported image type: " + path.suffix
        if path.stat().st_size > MAX_IMAGE_BYTES:
            return [], "image is larger than 12MB"
        resolved = str(path)
        if resolved not in out:
            out.append(resolved)
        if len(out) > MAX_IMAGES:
            return [], "at most 8 images"
    return out, ""


def _make_send_message(text: str, image_paths: list[str]) -> Any:
    if not image_paths:
        return text
    try:
        from cursor_sdk import SDKImage, UserMessage

        return UserMessage(
            text=text,
            images=[SDKImage.from_file(path) for path in image_paths],
        )
    except Exception:
        traceback.print_exc()
        return text + "\n\nAttached image files:\n" + "\n".join(image_paths)


def _close_agent(agent: Any) -> None:
    if agent is None:
        return
    try:
        if hasattr(agent, "close") and callable(agent.close):
            agent.close()
        elif hasattr(agent, "__exit__"):
            agent.__exit__(None, None, None)
    except Exception:
        pass


def _drop_session(key: tuple[str, str]) -> None:
    session = _sessions.pop(key, None)
    if session is not None:
        _close_agent(session.agent)


def _unwrap_agent(created: Any) -> Any:
    if hasattr(created, "__enter__"):
        return created.__enter__()
    return created


def _model_arg(model: str, params: list[ModelParamIn]) -> Any:
    try:
        from cursor_sdk import ModelParameterValue, ModelSelection

        return ModelSelection(
            id=model,
            params=tuple(
                ModelParameterValue(id=p.id, value=p.value) for p in params
            ),
        )
    except Exception:
        return model


_KO_PARTICLES = (
    "으로", "에서", "에게", "까지", "부터", "처럼", "같은", "같이",
    "을", "를", "이", "가", "은", "는", "의", "에", "로", "도", "와", "과",
)


def _word_tokens(text: str) -> set[str]:
    out: set[str] = set()
    for tok in re.findall(r"[0-9a-z가-힣]+", text.lower()):
        out.add(tok)
        if tok.isascii() and tok.endswith("s") and len(tok) > 3:
            out.add(tok[:-1])
        for suffix in _KO_PARTICLES:
            if not tok.isascii() and tok.endswith(suffix) and len(tok) > len(suffix):
                out.add(tok[: -len(suffix)])
    return out


def _hint_hits(text: str, hints: list[str]) -> list[str]:
    """Phrases match as substrings; single words must match a whole word."""
    low = text.lower()
    words = _word_tokens(text)
    hits: list[str] = []
    for hint in hints:
        h = hint.strip().lower()
        if not h:
            continue
        phrase = " " in h or (not h.isascii() and len(h) >= 2)
        if (phrase and h in low) or h in words:
            hits.append(hint)
    return hits


def _template_detail(t: Mapping[str, Any]) -> str:
    lines = [_template_line(t)]
    if t.get("kind") == "niagara_template":
        lines.append(
            f"  path: the list_niagara_templates entry whose name contains '{t.get('list_name')}'"
        )
    mods = t.get("add_modules") or []
    if mods:
        lines.append(
            "  add_modules: "
            + "; ".join(f"{m.get('module')} ({m.get('purpose')})" for m in mods)
        )
    params = t.get("params") or {}
    if params:
        lines.append("  start params: " + ", ".join(f"{k}={v}" for k, v in params.items()))
    if t.get("doc"):
        lines.append(f"  details: get_vfx_template_guide template={t.get('id')}")
    return "\n".join(lines)


def _execute_vfx_template_guide(args: Mapping[str, Any]) -> str:
    data = _load_templates()
    templates = data.get("templates") or []
    if not templates:
        return f"Template catalog missing or invalid: {_TEMPLATES_PATH}"

    template_name = str(args.get("template") or "").strip().lower()
    if template_name:
        for t in templates:
            names = {str(t.get("id") or "").lower(), str(t.get("list_name") or "").lower()}
            if template_name in names:
                detail = _template_detail(t)
                doc = str(t.get("doc") or "")
                if doc:
                    detail += f"\n\n=== {doc} ===\n\n{_read_template_doc(doc)}"
                return detail
        return "Unknown template. Available: " + ", ".join(str(t.get("id")) for t in templates)

    effect = str(args.get("effect") or "").strip()
    want_form = str(args.get("form") or "").strip().lower()
    want_emission = str(args.get("emission") or "").strip().lower()
    want_orientation = str(args.get("orientation") or "").strip().lower()
    want_motion = {
        m.strip().lower()
        for m in re.split(r"[,/ ]+", str(args.get("motion") or ""))
        if m.strip()
    }

    scored: list[tuple[int, list[str], Mapping[str, Any]]] = []
    for t in templates:
        score = 0
        why: list[str] = []
        if want_form and t.get("form") == want_form:
            score += 4
            why.append("form")
        if want_emission and t.get("emission") == want_emission:
            score += 2
            why.append("emission")
        if want_orientation and t.get("orientation") == want_orientation:
            score += 2
            why.append("orientation")
        shared = want_motion & set(t.get("motion") or [])
        if shared:
            score += 2 * len(shared)
            why.append("motion")
        hits = _hint_hits(effect, list(t.get("example_effects") or [])) if effect else []
        if hits:
            score += len(hits)
            why.append("hint:" + ",".join(hits))
        if score:
            scored.append((score, why, t))

    if not scored:
        return (
            "No candidate matched. Decide form/emission/motion/orientation from the request "
            "and call again with them, or pick from the catalog:\n"
            + "\n".join(_template_line(t) for t in templates)
        )
    scored.sort(key=lambda row: row[0], reverse=True)
    out = [
        "Candidates (effect-name hints are weak; confirm by the visible form the user wants):"
    ]
    for score, why, t in scored[:4]:
        out.append(f"score={score} matched={'+'.join(why)}\n{_template_detail(t)}")
    return "\n".join(out)


def _execute_save_recipe(project_dir: str, chat_id: str, args: Mapping[str, Any]) -> str:
    """Execute save_vfx_recipe custom tool."""
    from recipe_lib import VFXRecipe, save_recipe, extract_tags
    
    name = str(args.get("name") or "").strip()
    if not name:
        return "error: name is required"
    
    template = str(args.get("template") or "").strip()
    if not template:
        return "error: template is required"
    
    description = str(args.get("description") or name).strip()
    
    # 태그 자동 추출 또는 사용자 제공
    tags = args.get("tags")
    if not tags:
        tags = extract_tags(f"{name} {description}")
    elif isinstance(tags, str):
        tags = [t.strip() for t in tags.split(",") if t.strip()]
    
    # 모듈 리스트
    add_modules = args.get("add_modules") or []
    if isinstance(add_modules, str):
        add_modules = [m.strip() for m in add_modules.split(",") if m.strip()]
    
    # 파라미터
    params = args.get("params") or {}
    if isinstance(params, str):
        try:
            params = json.loads(params)
        except Exception:
            params = {}
    
    # 레시피 생성
    recipe = VFXRecipe(
        id="",  # 자동 생성
        name=name,
        description=description,
        template=template,
        add_modules=list(add_modules),
        params=dict(params),
        tags=list(tags),
        niagara_path=str(args.get("niagara_path") or ""),
        chat_id=chat_id,
    )
    
    # 섬네일
    thumbnail = str(args.get("thumbnail") or "").strip()
    
    try:
        saved_path = save_recipe(project_dir, recipe, thumbnail)
        return (
            f"saved=true\n"
            f"id={recipe.id}\n"
            f"name={recipe.name}\n"
            f"path={saved_path}\n"
            f"tags={','.join(recipe.tags)}\n"
            f"version={recipe.version}"
        )
    except Exception as exc:
        return f"error: {exc}"


def _execute_search_recipe(project_dir: str, args: Mapping[str, Any]) -> str:
    """Execute search_vfx_recipe custom tool."""
    from recipe_lib import search_recipes
    
    query = str(args.get("query") or "").strip()
    if not query:
        return "error: query is required"
    
    limit = int(args.get("limit") or 5)
    
    try:
        results = search_recipes(project_dir, query, limit=limit)
        
        if not results:
            return f"no_match=true\nquery={query}\nUse get_vfx_template_guide instead for new effects."
        
        lines = [f"found={len(results)}"]
        for i, r in enumerate(results, 1):
            lines.append(f"\n--- Recipe {i}: {r.name} ---")
            lines.append(f"id: {r.id}")
            lines.append(f"template: {r.template}")
            if r.add_modules:
                lines.append(f"add_modules: {', '.join(r.add_modules)}")
            if r.params:
                params_str = ", ".join(f"{k}={v}" for k, v in r.params.items())
                lines.append(f"params: {params_str}")
            lines.append(f"tags: {', '.join(r.tags)}")
            if r.niagara_path:
                lines.append(f"niagara_path: {r.niagara_path}")
            lines.append(f"version: {r.version}")
        
        return "\n".join(lines)
    except Exception as exc:
        return f"error: {exc}"


def _custom_tools(project_dir: str, chat_id: str, mcp_url: str = "") -> dict[str, Any]:
    from cursor_sdk import CustomTool
    from vfx_recipe import call_mcp, infer_look, infer_noise_mic, _png_path

    def execute_png(args: Mapping[str, Any], context: Any) -> str:
        del context
        result = write_generated_png(
            project_dir,
            description=str(args.get("description") or args.get("prompt") or "").strip(),
            kind=str(args.get("kind") or ""),
            name=str(args.get("name") or ""),
            frequency=args.get("frequency"),
            octaves=args.get("octaves"),
            size=args.get("size"),
            seed=args.get("seed"),
        )
        for path in _png_paths_in(result):
            _job_image((project_dir, chat_id), path)
        return result

    def execute_request_texture(args: Mapping[str, Any], context: Any) -> str:
        del context
        name = tf.sanitize_name(str(args.get("name") or ""))
        if not name:
            return "name required: an asset stem such as T_GrassTuft"
        desc = str(args.get("description") or "").strip()
        source = str(args.get("source") or "").strip()
        if not desc and not source:
            return "description required"
        use = str(args.get("use") or "cutout").strip().lower()
        if use not in tf.USES:
            return "use must be cutout, glow, or tile"
        meta = _read_meta(project_dir, chat_id)
        tex = dict(meta.get("tex") or {})
        active = str(tex.get("stage") or "") in (tf.STAGE_REQUESTED, tf.STAGE_IMAGE, tf.STAGE_ALPHA)
        items = [dict(it) for it in (tex.get("items") or [])] if active else []
        if not active:
            tex["request"] = ""

        def _int(key: str) -> int:
            try:
                return max(0, int(float(args.get(key) or 0)))
            except (TypeError, ValueError):
                return 0

        item = {
            "name": name,
            "use": use,
            "description": desc,
            "columns": _int("columns"),
            "rows": _int("rows"),
            "size": _int("size"),
            "anchor": "bottom" if str(args.get("anchor") or "").lower() == "bottom" else "center",
        }
        target = tf.raw_png(project_dir, name)
        for stale in target.parent.glob("*.png"):
            stale.unlink(missing_ok=True)
        if source:
            try:
                src_path = Path(source).expanduser().resolve()
                src_path.relative_to(Path(project_dir).resolve())
            except (OSError, ValueError):
                return "source must be an image under the Unreal project"
            if not mf.settle_ref_image(target, [src_path]):
                return "source is not a PNG or JPEG image"
            item["source"] = str(src_path)
        items = [it for it in items if it.get("name") != name] + [item]
        tex.update(stage=tf.STAGE_REQUESTED, items=items)
        _patch_chat_meta(project_dir, chat_id, tex=tex)
        if source:
            return (
                f"name={name} picture={target} (copied from the attached image). "
                "Do not call GenerateImage for this name. Stop after all request_texture calls "
                "and ask in one short sentence whether the pictures look right."
            )
        return (
            f"file_path={target}\n"
            f"Now call GenerateImage exactly once with filename={name}.png (or filePath={target} "
            f"if your tool takes a path), aspect_ratio=1:1, and description: {tf.image_prompt(item)}\n"
            "Request every texture the task needs in this Send, then stop and ask in one short "
            "sentence whether the pictures look right. Do not import_texture or use these "
            "textures in this Send; the sidecar hands you the final PNGs after the user approves."
        )

    def execute_vfx_json(args: Mapping[str, Any], context: Any) -> str:
        del context
        effect_id = _sanitize_effect_id(
            str(_read_meta(project_dir, chat_id).get("effect_id") or "")
        )
        if not effect_id:
            return "error=no effect bound"
        try:
            effect_dir = _effect_root(project_dir, effect_id)
        except ValueError as exc:
            return "error=" + str(exc)
        return save_vfx_record(effect_dir, str(args.get("file") or ""), args.get("json"))

    def execute_recipe(args: Mapping[str, Any], context: Any) -> str:
        del context
        desc = str(args.get("description") or args.get("prompt") or "").strip()
        if not desc:
            return "description required"
        if not mcp_url:
            return "mcp_url missing"
        look = infer_look(desc, str(args.get("look") or ""))
        meta = _read_meta(project_dir, chat_id)
        stored_path = str(meta.get("niagara_path") or "").strip()
        stored_name = str(meta.get("niagara_name") or "").strip()
        given_name = _sanitize_ns_stem(str(args.get("name") or ""))
        given_path = str(args.get("path") or "").strip()
        stem = given_name or _sanitize_ns_stem(given_path) or _sanitize_ns_stem(stored_name)
        if not stem:
            return (
                "ask_user=niagara_name\n"
                "Ask only: Niagara 시스템 이름을 알려주세요. Then wait."
            )
        if not given_path and stored_path and _sanitize_ns_stem(stored_path) == stem:
            given_path = stored_path
        ns_path = given_path or f"/Game/FX/NS_{stem}"
        png_stem = "".join(
            ch if ch.isalnum() or ch in "-_" else "_" for ch in stem
        )[:40] or look
        texture = str(args.get("texture") or "").strip()
        if not texture:
            return (
                "texture required: call request_texture first (use=glow for light, fire, or magic; "
                "use=cutout otherwise; columns and rows for look=card_flipbook). After the user "
                "approves, pass the png path the sidecar gives you as texture=."
            )
        try:
            tex_path = Path(texture).expanduser().resolve()
            tex_path.relative_to(Path(project_dir).resolve())
        except (OSError, ValueError):
            return "texture must be a PNG under the Unreal project"
        if not tex_path.is_file():
            return "texture not found: " + texture
        texture = str(tex_path)

        def _num(key: str, default: float) -> float:
            raw = args.get(key)
            if raw in (None, ""):
                return default
            try:
                return float(raw)
            except (TypeError, ValueError):
                return default

        payload: dict[str, Any] = {
            "look": look,
            "description": desc,
            "label": str(args.get("label") or f"FX_{stem}"),
        }
        payload["texture"] = texture
        bake = f"png={texture}"
        if look == "card_flipbook":
            payload["columns"] = _num("columns", 4)
            payload["rows"] = _num("rows", 4)
            payload["fps"] = _num("fps", 10)
        elif look == "card_noise":
            noise = write_generated_png(project_dir, desc, kind="noise", name=png_stem + "_noise")
            if "png=" not in noise:
                return noise
            payload["noise"] = _png_path(noise)
            payload.update(infer_noise_mic(desc))
            bake += "\n" + noise
        elif look in ("particles", "ribbon", "mesh"):
            if look == "mesh" and args.get("mesh"):
                payload["mesh"] = str(args.get("mesh"))
            payload["path"] = ns_path
        else:
            return "unknown look " + look
        applied = call_mcp(mcp_url, "apply_vfx_look", payload)
        try:
            fields: dict[str, Any] = {"niagara_name": stem}
            if payload.get("path"):
                fields["niagara_path"] = str(payload["path"])
            _patch_chat_meta(project_dir, chat_id, **fields)
        except Exception:
            pass
        return f"look={look}\n{bake}\n{applied}"

    def execute_request_mesh(args: Mapping[str, Any], context: Any) -> str:
        del context
        desc = str(args.get("description") or "").strip()
        if not desc:
            return "description required"
        meta = _read_meta(project_dir, chat_id)
        stage = str(meta.get("mesh_stage") or "")
        mesh_id = mf.sanitize_mesh_id(str(meta.get("mesh_id") or ""))
        if stage in (mf.STAGE_APPLY, mf.STAGE_NAME):
            return (
                "error=a mesh preview is waiting in the viewport. "
                "The user must apply or cancel it before a new picture."
            )
        if not mesh_id or stage not in (mf.STAGE_IMAGE, mf.STAGE_IMAGE_REQUESTED):
            mesh_id = mf.mint_mesh_id()
        try:
            height = float(args.get("height_cm") or 0)
        except (TypeError, ValueError):
            height = 0.0
        target = mf.ref_png(project_dir, mesh_id)
        if target.is_file():
            target.unlink()
        mf.write_mesh_meta(
            project_dir,
            mesh_id,
            chat_id=chat_id,
            description=desc,
            suggested_name=mf.sanitize_mesh_name(str(args.get("name") or "")),
            height_cm=max(0.0, height),
            stage=mf.STAGE_IMAGE_REQUESTED,
        )
        _patch_chat_meta(
            project_dir, chat_id, mesh_id=mesh_id, mesh_stage=mf.STAGE_IMAGE_REQUESTED
        )
        return (
            f"file_path={target}\n"
            f"Now call GenerateImage exactly once with filePath={target} and description: "
            f"{desc}. {mf.IMAGE_PROMPT_RULES}.\n"
            "Then reply in one short sentence asking whether the picture looks right. "
            "Do not call MCP, import, spawn, or other tools in this Send."
        )

    return {
        "request_mesh": CustomTool(
            execute=execute_request_mesh,
            description=(
                "Start a 3D model for an object the project does not have (tree, rock, prop). "
                "Returns the filePath for one reference picture. Call GenerateImage once with it, "
                "then ask the user to check the picture. The sidecar runs TRELLIS and imports "
                "into Unreal only after the user approves. Calling again replaces the picture."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "description": {
                        "type": "string",
                        "description": "English description of the single object, its look and colors",
                    },
                    "name": {
                        "type": "string",
                        "description": "Suggested asset stem, e.g. Tree. The user confirms the name later",
                    },
                    "height_cm": {
                        "type": "number",
                        "description": "Real-world height in cm if known (tree ~400, chair ~90)",
                    },
                },
                "required": ["description"],
            },
        ),
        "request_texture": CustomTool(
            execute=execute_request_texture,
            description=(
                "Start one texture picture drawn by GenerateImage. Returns the filePath and the full "
                "GenerateImage description. use=cutout for grass, flowers, leaves, props, and "
                "sprites (drawn on magenta, background removed later). use=glow for fire, light, "
                "sparks, and magic (drawn on black). use=tile for seamless ground and surface "
                "textures. columns and rows ask for a flipbook sheet. source= uses an attached "
                "image instead of GenerateImage. The user approves the picture and picks the "
                "background removal; then the sidecar gives you the final PNG paths."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Asset stem, e.g. T_GrassTuft"},
                    "use": {"type": "string", "description": "cutout (default), glow, or tile"},
                    "description": {
                        "type": "string",
                        "description": "English description of the picture: subject, style, colours, detail",
                    },
                    "columns": {"type": "number", "description": "Flipbook sheet columns"},
                    "rows": {"type": "number", "description": "Flipbook sheet rows"},
                    "size": {
                        "type": "number",
                        "description": "Final size in px: tile default 1024, cutout 512, sheet cell 256",
                    },
                    "anchor": {
                        "type": "string",
                        "description": "bottom keeps a cutout's base on the bottom edge (grass cards). Default center",
                    },
                    "source": {"type": "string", "description": "Attached image path to use as the picture"},
                },
                "required": ["name"],
            },
        ),
        "generate_png": CustomTool(
            execute=execute_png,
            description=(
                "Bake a data texture under Saved/PromptEditor/gen for import_texture. "
                "kind=noise is a tiling curl-noise flow field (RG) for UV warp; it is not a colour "
                "texture. kind=mask is a soft round grayscale mask. Pictures come from request_texture."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "kind": {"type": "string", "description": "noise or mask"},
                    "description": {
                        "type": "string",
                        "description": "What the field is for (smoke, fire, water); picks noise defaults",
                    },
                    "name": {"type": "string", "description": "Filename stem"},
                    "frequency": {
                        "type": "number",
                        "description": "Noise cells across the image, default inferred (fire 5, smoke 3)",
                    },
                    "octaves": {"type": "number", "description": "Noise octaves, default 3"},
                    "size": {"type": "number", "description": "Noise PNG size, default 256"},
                    "seed": {"type": "number", "description": "Noise seed. Omitted: hash of description"},
                },
                "required": ["kind"],
            },
        ),
        "apply_vfx_recipe": CustomTool(
            execute=execute_recipe,
            description=(
                "One look recipe: apply_vfx_look in Unreal with an approved texture. "
                "look=card_flipbook (looping card from a sheet), card_noise (one texture UV warp), "
                "particles, ribbon, or mesh. Omit look to infer from description. "
                "Requires the user-chosen name. Do not invent NS_Live or a hash name. "
                "Pass name=EmberRibbon (or path=/Game/FX/NS_EmberRibbon) after they answer. "
                "texture= is the final PNG from request_texture; card_flipbook needs the sheet's "
                "columns and rows."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "description": {
                        "type": "string",
                        "description": "What the effect should look like",
                    },
                    "look": {
                        "type": "string",
                        "description": "card_flipbook, card_noise, particles, ribbon, mesh. Inferred if omitted",
                    },
                    "name": {
                        "type": "string",
                        "description": "User-chosen asset stem, e.g. EmberRibbon → /Game/FX/NS_EmberRibbon",
                    },
                    "label": {"type": "string", "description": "Actor label"},
                    "path": {
                        "type": "string",
                        "description": "/Game Niagara path if the user named one",
                    },
                    "texture": {
                        "type": "string",
                        "description": "Final PNG path the sidecar returned after request_texture approval",
                    },
                    "columns": {"type": "number", "description": "Sheet columns for card_flipbook"},
                    "rows": {"type": "number", "description": "Sheet rows for card_flipbook"},
                    "fps": {"type": "number"},
                    "mesh": {"type": "string", "description": "Static mesh path when look=mesh"},
                },
                "required": ["description", "texture"],
            },
        ),
        "save_vfx_json": CustomTool(
            execute=execute_vfx_json,
            description=(
                "Validate JSON against sidecar/vfx/schema and write it into this "
                "chat's Saved/PromptEditor/effects/<id>/ folder. "
                "Does not write if schema validation fails. "
                "file is one of: " + ", ".join(RECORD_SCHEMAS) + ". "
                "iteration_history.json is append-only."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "file": {
                        "type": "string",
                        "description": "Record filename under the effect folder",
                        "enum": list(RECORD_SCHEMAS),
                    },
                    "json": {
                        "description": "Full record object, or a JSON string of that object",
                    },
                },
                "required": ["file", "json"],
            },
        ),
        "get_vfx_template_guide": CustomTool(
            execute=lambda args, ctx: _execute_vfx_template_guide(args),
            description=(
                "Rank Niagara templates and card approaches from sidecar/vfx/templates.json. "
                "Pass the visible form/emission/motion/orientation you decided from the request; "
                "effect text only adds weak name hints. Returns several candidates with modules "
                "and start params. template=<id> returns one entry plus its full doc."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "form": {
                        "type": "string",
                        "description": "many_instances, camera_card, warp_card, continuous_strip, "
                        "chunks_3d, flat_surface, field_2d, grid_pattern, audio_reactive",
                    },
                    "emission": {"type": "string", "description": "burst, continuous, static"},
                    "motion": {
                        "type": "string",
                        "description": "Comma list: rising, falling, drifting, radial, directional, "
                        "following, static, flowing",
                    },
                    "orientation": {
                        "type": "string",
                        "description": "camera_facing, velocity_aligned, flat_horizontal, strip, mesh",
                    },
                    "effect": {"type": "string", "description": "The user's effect wording"},
                    "template": {"type": "string", "description": "Catalog id for full details"},
                },
                "required": [],
            },
        ),
        "save_vfx_recipe": CustomTool(
            execute=lambda args, ctx: _execute_save_recipe(project_dir, chat_id, args),
            description=(
                "Save the current VFX effect as a reusable recipe to the library. "
                "Call this when the user is satisfied with the effect (says '좋아', '완벽', 'save this', etc). "
                "The recipe can be searched and reused later for similar effects."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Recipe name (e.g. '별똥별', 'SwordSlash')",
                    },
                    "description": {
                        "type": "string",
                        "description": "Effect description for search",
                    },
                    "template": {
                        "type": "string",
                        "description": "Base Niagara template used (e.g. 'HangingParticulates', 'Fountain')",
                    },
                    "add_modules": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "List of added modules (e.g. ['GravityForce', 'Collision'])",
                    },
                    "params": {
                        "type": "object",
                        "description": "Key parameters (e.g. {Gravity: -150, SpawnRate: 20})",
                    },
                    "tags": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "Search tags (auto-extracted if omitted)",
                    },
                    "niagara_path": {
                        "type": "string",
                        "description": "/Game/FX/NS_xxx path",
                    },
                    "thumbnail": {
                        "type": "string",
                        "description": "Path to thumbnail image (from capture_viewport)",
                    },
                },
                "required": ["name", "template"],
            },
        ),
        "search_vfx_recipe": CustomTool(
            execute=lambda args, ctx: _execute_search_recipe(project_dir, args),
            description=(
                "Search saved VFX recipes by name, tags, or description. "
                "Use this when the user requests an effect that might already exist in the library. "
                "Returns matching recipes with their templates and parameters."
            ),
            input_schema={
                "type": "object",
                "properties": {
                    "query": {
                        "type": "string",
                        "description": "Search query (effect name or description)",
                    },
                    "limit": {
                        "type": "number",
                        "description": "Maximum results (default 5)",
                    },
                },
                "required": ["query"],
            },
        ),
    }


def _agent_options(
    mcp_url: str,
    project_dir: str,
    model: str,
    params: list[ModelParamIn],
    chat_id: str,
) -> Any:
    from cursor_sdk import AgentOptions, LocalAgentOptions

    return AgentOptions(
        model=_model_arg(model, params),
        api_key=_api_key(),
        local=LocalAgentOptions(
            cwd=project_dir,
            custom_tools=_custom_tools(project_dir, chat_id, mcp_url),
        ),
        mcp_servers={"editor-prompt": {"type": "http", "url": mcp_url}},
    )


def _create_cursor_agent(
    mcp_url: str,
    project_dir: str,
    model: str,
    params: list[ModelParamIn],
    chat_id: str,
) -> Any:
    from cursor_sdk import Agent

    patch_cursor_sdk_bridge()
    opts = _agent_options(mcp_url, project_dir, model, params, chat_id)
    try:
        created = Agent.create(opts)
    except TypeError:
        created = Agent.create(
            model=_model_arg(model, params),
            api_key=_api_key(),
            local=opts.local,
            mcp_servers=opts.mcp_servers,
        )
    return _unwrap_agent(created)


def _resume_cursor_agent(
    agent_id: str,
    mcp_url: str,
    project_dir: str,
    model: str,
    params: list[ModelParamIn],
    chat_id: str,
) -> Any:
    from cursor_sdk import Agent

    patch_cursor_sdk_bridge()
    opts = _agent_options(mcp_url, project_dir, model, params, chat_id)
    created = Agent.resume(agent_id, opts)
    return _unwrap_agent(created)


def _read_meta(project_dir: str, chat_id: str) -> dict[str, Any]:
    chats = _chats_root(project_dir)
    if not chats.is_dir():
        return {}
    try:
        chats_res = chats.resolve()
        meta_path = (chats / chat_id / "meta.json").resolve()
        meta_path.relative_to(chats_res)
    except (OSError, ValueError):
        return {}
    if not meta_path.is_file():
        return {}
    try:
        data = json.loads(meta_path.read_text(encoding="utf-8"))
    except Exception:
        return {}
    return data if isinstance(data, dict) else {}


def _store_session(
    key: tuple[str, str],
    agent: Any,
    project_dir: str,
    chat_id: str,
    fingerprint: str,
) -> None:
    _sessions[key] = Session(
        agent=agent,
        project_dir=project_dir,
        chat_id=chat_id,
        model=fingerprint,
    )


def _get_agent(
    project_dir: str,
    chat_id: str,
    mcp_url: str,
    model: str,
    params: list[ModelParamIn],
) -> tuple[Any, bool]:
    key = (project_dir, chat_id)
    fingerprint = _model_fingerprint(model, params)
    session = _sessions.get(key)
    if session is not None and session.model == fingerprint:
        return session.agent, False
    had_live = session is not None
    _drop_session(key)
    # Sidecar restart: try Cursor resume with the MCP URL bound right now.
    # Model/variant change in this process: skip resume, new Agent + disk history.
    if not had_live:
        saved_id = str(_read_meta(project_dir, chat_id).get("agent_id") or "").strip()
        if saved_id:
            try:
                agent = _resume_cursor_agent(
                    saved_id, mcp_url, project_dir, model, params, chat_id
                )
                _store_session(key, agent, project_dir, chat_id, fingerprint)
                return agent, False
            except Exception:
                traceback.print_exc()
    agent = _create_cursor_agent(mcp_url, project_dir, model, params, chat_id)
    _store_session(key, agent, project_dir, chat_id, fingerprint)
    return agent, True


def _try_cancel_run(run: Any) -> None:
    if run is None:
        return
    try:
        if hasattr(run, "supports") and not run.supports("cancel"):
            return
        if hasattr(run, "cancel"):
            run.cancel()
    except Exception:
        traceback.print_exc()


def _is_stale_run_error(exc: BaseException) -> bool:
    text = str(exc).lower()
    return "already has active run" in text or "has active run" in text


def _send_options(mcp_url: str, *, force: bool) -> Any:
    from cursor_sdk import LocalSendOptions, SendOptions

    return SendOptions(
        mcp_servers={"editor-prompt": {"type": "http", "url": mcp_url}},
        local=LocalSendOptions(force=force),
    )


def _agent_send(agent: Any, prompt: Any, mcp_url: str, *, force: bool) -> Any:
    opts = _send_options(mcp_url, force=force)
    try:
        return agent.send(prompt, opts)
    except TypeError:
        try:
            return agent.send(prompt, options=opts)
        except TypeError:
            return agent.send(prompt)


def _cancel_stale_runs(agent: Any, project_dir: str) -> None:
    agent_id = str(getattr(agent, "agent_id", "") or getattr(agent, "agentId", "") or "")
    if not agent_id:
        return
    try:
        from cursor_sdk import Agent

        listed = Agent.list_runs(agent_id, cwd=project_dir)
    except Exception:
        traceback.print_exc()
        return
    terminal = {"finished", "error", "cancelled", "canceled", "expired"}
    for run in getattr(listed, "items", None) or []:
        status = str(getattr(run, "status", "") or "").lower()
        if status in terminal:
            continue
        _try_cancel_run(run)


def _is_cancelled_status(status: str) -> bool:
    return str(status or "").lower() in {"cancelled", "canceled"}


def _png_paths_in(text: str) -> list[str]:
    out: list[str] = []
    for part in (text or "").split():
        if part.startswith("png="):
            path = part[4:].strip()
            if path and path not in out:
                out.append(path)
    return out


def _job_image(key: tuple[str, str], path: str) -> None:
    text = (path or "").strip()
    if not text:
        return
    with _run_lock:
        job = _jobs.get(key)
        if job is None:
            return
        if text not in job.images:
            job.images.append(text)


def _job_event(key: tuple[str, str], line: str) -> None:
    text = (line or "").strip()
    if not text:
        return
    with _run_lock:
        job = _jobs.get(key)
        if job is None:
            return
        job.events.append(text[:400])
        if len(job.events) > JOB_EVENT_CAP:
            job.events = job.events[-JOB_EVENT_CAP:]


def _assistant_text(msg: Any) -> str:
    message = getattr(msg, "message", None)
    content = getattr(message, "content", None) if message is not None else None
    texts: list[str] = []
    for block in content or ():
        text = str(getattr(block, "text", "") or "")
        if not text and isinstance(block, dict):
            text = str(block.get("text") or "")
        if text:
            texts.append(text)
    return "".join(texts)


def _as_map(value: Any) -> dict[str, Any]:
    if isinstance(value, Mapping):
        return dict(value)
    if isinstance(value, str):
        try:
            loaded = json.loads(value)
        except Exception:
            return {}
        if isinstance(loaded, Mapping):
            return dict(loaded)
        return {}
    return {}


def _map_str(data: Mapping[str, Any], *keys: str) -> str:
    for key in keys:
        raw = data.get(key)
        if raw is None:
            continue
        text = str(raw).strip()
        if text:
            return text
    return ""


def _tool_display_name(name: str, args: Any = None) -> str:
    outer = (name or "").strip()
    data = _as_map(args)
    inner = _map_str(
        data,
        "toolName",
        "tool_name",
        "tool",
        "mcpTool",
        "mcp_tool",
    )
    if not inner:
        nested = data.get("arguments") or data.get("args") or data.get("input")
        inner = _map_str(_as_map(nested), "name", "toolName", "tool_name", "tool")
    if not inner:
        inner = _map_str(data, "name")
    generic = outer.lower().replace("-", "_")
    if inner and (
        not outer
        or generic
        in {
            "mcp",
            "call_mcp_tool",
            "callmcptool",
            "mcp_tool",
            "editor_prompt",
            "editorprompt",
        }
    ):
        return inner
    if inner and inner != outer:
        return inner
    return inner or outer


def _tool_event_line(msg: Any) -> str:
    name = _tool_display_name(
        str(getattr(msg, "name", "") or ""),
        getattr(msg, "args", None),
    )
    status = str(getattr(msg, "status", "") or "").lower()
    if not name:
        return ""
    if status in ("", "running"):
        return f"tool {name}"
    if status == "error":
        return f"tool {name} failed"
    return ""


def _finish_run_result(run: Any) -> tuple[str, bool]:
    reply = ""
    cancelled = False
    if hasattr(run, "wait"):
        waited = run.wait()
        status = str(getattr(waited, "status", "") or "")
        if _is_cancelled_status(status):
            cancelled = True
        elif hasattr(waited, "result") and waited.result:
            reply = str(waited.result)
        elif status and status != "finished":
            reply = f"(run status: {status})"
    if not reply and hasattr(run, "text") and callable(run.text):
        reply = str(run.text() or "")
    return reply, cancelled


def _run_text(
    agent: Any,
    prompt: Any,
    mcp_url: str,
    key: tuple[str, str],
    on_event: Callable[[str], None] | None = None,
    on_tool: Callable[[str, str, Any], None] | None = None,
) -> tuple[str, list[dict[str, str]], bool]:
    def emit(line: str) -> None:
        if on_event is not None:
            on_event(line)

    with _run_lock:
        if key in _cancel_requested:
            return "", [], True
        leftover = _active_runs.pop(key, None)
    if leftover is not None:
        _try_cancel_run(leftover)
    try:
        run = _agent_send(agent, prompt, mcp_url, force=True)
    except Exception as exc:
        if "cancel" in str(exc).lower():
            return "", [], True
        if not _is_stale_run_error(exc):
            raise
        _cancel_stale_runs(agent, key[0])
        try:
            run = _agent_send(agent, prompt, mcp_url, force=True)
        except Exception as retry_exc:
            if "cancel" in str(retry_exc).lower():
                return "", [], True
            raise
    with _run_lock:
        _active_runs[key] = run
        should_stop = key in _cancel_requested
    if should_stop:
        _try_cancel_run(run)
        with _run_lock:
            _active_runs.pop(key, None)
        return "", [], True
    reply = ""
    cancelled = False
    used: list[str] = []
    try:
        stream_fn = None
        if hasattr(run, "messages") and callable(run.messages):
            stream_fn = run.messages
        elif hasattr(run, "stream") and callable(run.stream):
            stream_fn = run.stream
        if stream_fn is not None:
            emit("running")
            chunks: list[str] = []
            for msg in stream_fn():
                if key in _cancel_requested:
                    _try_cancel_run(run)
                    cancelled = True
                    break
                typ = str(getattr(msg, "type", "") or "")
                if typ == "assistant":
                    text = _assistant_text(msg)
                    if text:
                        chunks.append(text)
                elif typ == "tool_call":
                    line = _tool_event_line(msg)
                    if line:
                        emit(line)
                    shown = _tool_display_name(
                        str(getattr(msg, "name", "") or ""),
                        getattr(msg, "args", None),
                    )
                    st = str(getattr(msg, "status", "") or "").lower()
                    if shown and st in ("", "running") and (not used or used[-1] != shown):
                        used.append(shown)
                    if on_tool is not None:
                        on_tool(
                            str(getattr(msg, "name", "") or shown),
                            st,
                            getattr(msg, "result", None),
                        )
                elif typ == "status":
                    note = str(getattr(msg, "message", "") or getattr(msg, "status", "") or "")
                    if note:
                        emit(note)
            if not cancelled:
                finish_reply, finish_cancelled = _finish_run_result(run)
                cancelled = finish_cancelled
                joined = "".join(chunks)
                reply = (finish_reply or joined).strip()
        elif hasattr(run, "text") and callable(run.text):
            emit("running")
            reply = str(run.text() or "")
            if hasattr(run, "wait"):
                try:
                    waited = run.wait()
                    if _is_cancelled_status(str(getattr(waited, "status", "") or "")):
                        cancelled = True
                except Exception:
                    pass
        else:
            emit("running")
            reply, cancelled = _finish_run_result(run)
    except Exception as exc:
        if "cancel" in str(exc).lower() or key in _cancel_requested:
            return "", [], True
        raise
    finally:
        with _run_lock:
            _active_runs.pop(key, None)
    if cancelled:
        return reply, [], True
    tools: list[dict[str, str]] = [{"name": item} for item in used]
    if not tools:
        try:
            if hasattr(run, "supports") and run.supports("conversation"):
                conv = run.conversation()
                steps = getattr(conv, "steps", None) or getattr(conv, "messages", None) or []
                for step in steps:
                    payload = (
                        getattr(step, "message", None)
                        or getattr(step, "tool_call", None)
                    )
                    raw_name = ""
                    args = None
                    if isinstance(payload, dict):
                        raw_name = str(
                            payload.get("name") or payload.get("toolName") or ""
                        )
                        args = payload.get("args") or payload.get("arguments")
                    elif payload is not None:
                        raw_name = str(getattr(payload, "name", "") or "")
                        args = getattr(payload, "args", None)
                    shown = _tool_display_name(raw_name, args)
                    if shown:
                        tools.append({"name": shown})
        except Exception:
            pass
    return reply, tools, False


def _chats_root(project_dir: str) -> Path:
    return Path(project_dir) / "Saved" / "PromptEditor" / "chats"


def _effects_root(project_dir: str) -> Path:
    return Path(project_dir) / "Saved" / "PromptEditor" / "effects"


def _sanitize_effect_id(raw: str) -> str:
    cleaned = "".join(ch for ch in (raw or "").strip() if ch.isalnum() or ch in "-_")
    cleaned = cleaned[:80]
    if not cleaned or cleaned.lower().startswith("ns_"):
        return ""
    return cleaned


def _mint_effect_id() -> str:
    return str(uuid.uuid4())


def _effect_root(project_dir: str, effect_id: str) -> Path:
    eid = _sanitize_effect_id(effect_id)
    if not eid:
        raise ValueError("effect_id is empty")
    root = _effects_root(project_dir)
    root.mkdir(parents=True, exist_ok=True)
    path = (root / eid).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as exc:
        raise ValueError("effect_id is outside effects root") from exc
    path.mkdir(parents=True, exist_ok=True)
    return path


def _patch_chat_meta(project_dir: str, chat_id: str, **fields: Any) -> None:
    root = _chat_root(project_dir, chat_id)
    meta_path = root / "meta.json"
    meta: dict[str, Any] = {}
    if meta_path.is_file():
        try:
            loaded = json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            loaded = {}
        if isinstance(loaded, dict):
            meta = loaded
    now = datetime.now(timezone.utc).isoformat()
    meta["id"] = chat_id
    if "created" not in meta:
        meta["created"] = now
    meta["modified"] = now
    for key, value in fields.items():
        if value is None:
            continue
        if value == "" and key in ("pending_name_ask", "niagara_path"):
            meta.pop(key, None)
            continue
        meta[key] = value
    meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")


def _write_chat_effect_id(project_dir: str, chat_id: str, effect_id: str) -> None:
    _patch_chat_meta(project_dir, chat_id, effect_id=effect_id)


def _bind_chat_effect(project_dir: str, chat_id: str, effect_id: str = "") -> str:
    eid = _sanitize_effect_id(effect_id) or _mint_effect_id()
    _effect_root(project_dir, eid)
    _write_chat_effect_id(project_dir, chat_id, eid)
    return eid


def _resolve_prompt_effect_id(
    project_dir: str, chat_id: str, requested: str, new_effect: bool
) -> str:
    if new_effect:
        return _bind_chat_effect(project_dir, chat_id, "")
    requested_id = _sanitize_effect_id(requested)
    if requested_id:
        return _bind_chat_effect(project_dir, chat_id, requested_id)
    existing = _sanitize_effect_id(str(_read_meta(project_dir, chat_id).get("effect_id") or ""))
    if existing:
        _effect_root(project_dir, existing)
        return existing
    return ""


def _chat_root(project_dir: str, chat_id: str) -> Path:
    chats = _chats_root(project_dir)
    chats.mkdir(parents=True, exist_ok=True)
    root = (chats / chat_id).resolve()
    try:
        root.relative_to(chats.resolve())
    except ValueError as exc:
        raise ValueError("chat_id is outside chats root") from exc
    (root / "turns").mkdir(parents=True, exist_ok=True)
    return root


def _load_chat_turns(project_dir: str, chat_id: str) -> list[dict[str, Any]]:
    """Turns for this chat_id only. Does not read sibling New Agent folders."""
    chats = _chats_root(project_dir)
    if not chats.is_dir():
        return []
    try:
        chats_res = chats.resolve()
        turns_dir = (chats / chat_id / "turns").resolve()
        turns_dir.relative_to(chats_res)
    except (OSError, ValueError):
        return []
    if not turns_dir.is_dir():
        return []
    out: list[dict[str, Any]] = []
    for path in sorted(p for p in turns_dir.glob("*.json") if p.stem.isdigit()):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            continue
        if isinstance(data, dict):
            out.append(data)
    return out


def _format_chat_history(turns: list[dict[str, Any]]) -> str:
    parts: list[str] = []
    for turn in turns:
        user = str(turn.get("user") or "").strip()
        reply = str(turn.get("reply") or "").strip()
        if user:
            parts.append(f"User:\n{user}")
        if reply:
            parts.append(f"Assistant:\n{reply}")
    return "\n\n".join(parts)


def _turn_chars(turn: dict[str, Any]) -> int:
    return len(str(turn.get("user") or "")) + len(str(turn.get("reply") or ""))


def _trim_turns(
    turns: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Keep newest turns until the char budget fills. Drop from the front. Files stay."""
    if not turns:
        return [], []
    kept_rev: list[dict[str, Any]] = []
    chars = 0
    for turn in reversed(turns):
        extra = _turn_chars(turn) + 16
        if kept_rev and chars + extra > HISTORY_MAX_CHARS:
            break
        if kept_rev and len(kept_rev) >= HISTORY_MAX_TURNS:
            break
        kept_rev.append(turn)
        chars += extra
    kept = list(reversed(kept_rev))
    dropped = turns[: len(turns) - len(kept)]
    return kept, dropped


def _summarize_dropped(dropped: list[dict[str, Any]]) -> str:
    if not dropped:
        return ""
    lines: list[str] = []
    for i, turn in enumerate(dropped, 1):
        user = str(turn.get("user") or "").strip().replace("\n", " ")
        if not user:
            continue
        if len(user) > 80:
            user = user[:77] + "..."
        lines.append(f"- {user}")
        if len(lines) >= SUMMARY_MAX_LINES:
            rest = len(dropped) - i
            if rest > 0:
                lines.append(f"- ({rest} older user turns omitted)")
            break
    if not lines:
        return f"{len(dropped)} older turns omitted; files remain on disk."
    return "Earlier user requests in this chat_id:\n" + "\n".join(lines)


_RECALL_MARKERS = (
    "그때",
    "저번",
    "아까",
    "예전",
    "했었",
    "해달",
    "뭐라고",
    "뭘 해",
    "무슨",
    "기억",
    "전에",
    "그 큐브",
    "그 파일",
    "부탁",
    "생성해달",
    "내가 뭘",
    "what did",
    "earlier",
    "before",
    "remember",
    "last time",
    "you create",
    "did i ask",
)

_SEARCH_SKIP = {
    "the",
    "and",
    "you",
    "for",
    "did",
    "what",
    "this",
    "that",
    "have",
    "please",
    "just",
    "from",
    "with",
    "내가",
    "나는",
    "우리",
    "이것",
    "저것",
    "해줘",
    "해서",
    "하는",
    "있나",
    "있어",
    "했어",
    "뭐라고",
    "달라고",
    "부탁",
}


def _looks_like_recall(message: str) -> bool:
    lowered = message.lower()
    return any(marker.lower() in lowered for marker in _RECALL_MARKERS)


def _query_tokens(message: str) -> list[str]:
    found = re.findall(r"[A-Za-z0-9_]{2,}|[가-힣]{2,}", message)
    out: list[str] = []
    for tok in found:
        key = tok.lower()
        if key in _SEARCH_SKIP or tok in _SEARCH_SKIP:
            continue
        if tok not in out:
            out.append(tok)
    return out


def _search_turns(
    turns: list[dict[str, Any]],
    message: str,
    skip: set[int],
) -> list[dict[str, Any]]:
    tokens = _query_tokens(message)
    scored: list[tuple[int, dict[str, Any]]] = []
    for idx, turn in enumerate(turns):
        if idx in skip:
            continue
        blob = (
            str(turn.get("user") or "") + "\n" + str(turn.get("reply") or "")
        ).lower()
        score = 0
        for tok in tokens:
            if tok.lower() in blob:
                score += 1
        if score == 0:
            continue
        scored.append((score, turn))
    scored.sort(key=lambda item: -item[0])
    return [turn for _, turn in scored[:SEARCH_MAX_HITS]]


_VFX_DO = re.compile(
    r"(?i)(\bmake\b|\bcreat|\bbuild\b|\bchange\b|\bfix\b|\bmatch\b|"
    r"\brecreat|\breproduc|\bimplement|\badjust\b|"
    r"만들|고쳐|맞춰|재현|구현|수정)"
)

_NAME_ASK_REPLY = "Niagara 시스템 이름을 알려주세요."

_RESERVED_NS_STEMS = {
    "particles",
    "ribbon",
    "mesh",
    "card_flipbook",
    "card_noise",
    "look",
    "live",
    "ns_live",
}


def _sanitize_ns_stem(raw: str) -> str:
    text = (raw or "").strip().strip("'\"")
    if not text:
        return ""
    text = text.replace("\\", "/").rstrip("/")
    if "/" in text:
        text = text.split("/")[-1]
    if text.lower().endswith(".uasset"):
        text = text[: -len(".uasset")]
    text = re.sub(r"[^A-Za-z0-9_]", "", text)
    if text.lower().startswith("ns_"):
        text = text[3:]
    if not text or text[0].isdigit():
        return ""
    if text.lower() in _RESERVED_NS_STEMS:
        return ""
    return text[:40]


def _extract_user_ns_name(message: str) -> str:
    text = (message or "").strip()
    if not text:
        return ""
    path = re.search(r"/Game/FX/(NS_)?([A-Za-z][A-Za-z0-9_]{1,39})", text)
    if path:
        return _sanitize_ns_stem(path.group(2))
    prefixed = re.search(r"\bNS_([A-Za-z][A-Za-z0-9_]{1,39})\b", text)
    if prefixed:
        return _sanitize_ns_stem(prefixed.group(1))
    labeled = re.search(
        r"(?:이름은|이름|name(?:\s+it)?|call it|as)\s*[:：]?\s*[\"']?"
        r"([A-Za-z][A-Za-z0-9_]{1,39})",
        text,
        re.I,
    )
    if labeled:
        return _sanitize_ns_stem(labeled.group(1))
    compact = re.sub(r"\s+", "", text)
    if re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{1,39}", compact):
        return _sanitize_ns_stem(compact)
    trailing = re.search(
        r"([A-Za-z][A-Za-z0-9_]{1,39})\s*(?:으로|로)\s",
        text,
    )
    if trailing:
        return _sanitize_ns_stem(trailing.group(1))
    return ""


def _is_how_or_plan(message: str) -> bool:
    text = (message or "").strip()
    if not text:
        return False
    if re.search(
        r"해줘|해 줘|만들어줘|만들어 줘|추가해|넣어줘|고쳐줘|please make|create this",
        text,
        re.I,
    ):
        return False
    low = text.lower()
    if re.match(r"(how |why |what is|what's |explain|plan\b)", low):
        return True
    if re.match(r"(어떻게|왜 |왜요|뭐야|뭔가요|무슨 뜻|설명해|계획)", text):
        return True
    if "어떻게" in text:
        return True
    return False


def _looks_like_effect_make(message: str) -> bool:
    if _is_how_or_plan(message):
        return False
    text = message or ""
    low = text.lower()
    markers = (
        "리본",
        "파티클",
        "불씨",
        "이펙트",
        "나이아가라",
        "모닥불",
        "궤적",
        "스파크",
        "연기",
        "불꽃",
        "ember",
        "ribbon",
        "particle",
        "niagara",
        "trail",
        "spark",
        "smoke",
        "flame",
        "fire",
        "vfx",
    )
    return any(mark in text or mark in low for mark in markers)


def _named_system_followup(original: str, stem: str) -> str:
    path = f"/Game/FX/NS_{stem}"
    return (
        f"{original}\n\n"
        f"The user named the new Niagara system NS_{stem} "
        f"(path {path}, actor FX_{stem}). "
        f"Call apply_vfx_recipe with name={stem} and path={path}. "
        "Do not ask for the name again. Do not use leftover systems from other chats."
    )


_RESTORE_MARKERS = (
    "원래대로",
    "되돌려",
    "되돌리",
    "롤백",
    "잘못 고",
    "잘못 수정",
    "put it back",
    "restore that",
    "revert that",
    "undo that",
    "roll back",
)

_RESTORE_NEW_RE = re.compile(
    r"새로\s*(만들|생성|해)|아예\s*새로|새\s*(시스템|이펙트|나이아가라|파티클)|"
    r"create a new|make a new|new system",
    re.I,
)
_RESTORE_EDIT_RE = re.compile(
    r"바꿔|고쳐줘|추가해|넣어줘|수정해|\bchange\b|\bfix\b|\bedit\b|\badd\b",
    re.I,
)


def _has_restore_intent(message: str) -> bool:
    if _is_how_or_plan(message):
        return False
    text = (message or "").strip()
    low = text.lower()
    return any(mark in text or mark in low for mark in _RESTORE_MARKERS)


def _extract_ns_stems(message: str) -> list[str]:
    stems: list[str] = []
    text = message or ""
    for pattern in (
        r"/Game/FX/(?:NS_)?([A-Za-z][A-Za-z0-9_]{1,39})",
        r"\bNS_([A-Za-z][A-Za-z0-9_]{1,39})\b",
    ):
        for match in re.finditer(pattern, text):
            stem = _sanitize_ns_stem(match.group(1))
            if stem and stem not in stems:
                stems.append(stem)
    return stems


def _split_restore_and_new_names(
    message: str, current_stem: str
) -> tuple[str, str]:
    """Return (restore /Game path or '', new NS stem or '')."""
    stems = _extract_ns_stems(message)
    restore_path = ""
    new_stem = ""
    current = (current_stem or "").lower()
    for stem in stems:
        if current and stem.lower() == current:
            restore_path = f"/Game/FX/NS_{stem}"
        elif not new_stem:
            new_stem = stem
    if not new_stem:
        trailing = re.search(
            r"([A-Za-z][A-Za-z0-9_]{1,39})\s*(?:으로|로)\s*(?:새로|만들)",
            message or "",
        )
        if trailing:
            cand = _sanitize_ns_stem(trailing.group(1))
            if cand and cand.lower() != current:
                new_stem = cand
    return restore_path, new_stem


def _looks_like_restore_followup(message: str, current_stem: str = "") -> bool:
    if not _has_restore_intent(message):
        return False
    text = message or ""
    if _RESTORE_NEW_RE.search(text):
        return True
    _, new_stem = _split_restore_and_new_names(text, current_stem)
    if new_stem:
        return True
    stripped = text
    for mark in _RESTORE_MARKERS:
        stripped = re.sub(re.escape(mark), " ", stripped, flags=re.I)
    stripped = re.sub(r"잘못\s*고\w*", " ", stripped)
    stripped = re.sub(r"(추가한|넣은|붙인|만든)\w*", " ", stripped)
    return _RESTORE_EDIT_RE.search(stripped) is not None


def _looks_like_restore(message: str) -> bool:
    """Pure restore: undo only, no new system / extra edits."""
    if not _has_restore_intent(message):
        return False
    return not _looks_like_restore_followup(message)


def _restore_ok(result: str) -> bool:
    text = result or ""
    if _mcp_failed(text):
        return False
    return text.startswith("restored ")


def _restore_then_new_system_prompt(
    original: str, restored: str, old_path: str, stem: str
) -> str:
    path = f"/Game/FX/NS_{stem}"
    old = old_path or "the previous Niagara system"
    return (
        f"{original}\n\n"
        f"The sidecar already called restore_asset for {old}. Result:\n{restored}\n"
        "Do not call restore_asset again. Do not re-author or edit the restored system. "
        f"The remaining request is a NEW Niagara system NS_{stem} "
        f"(path {path}, actor FX_{stem}). "
        f"Call apply_vfx_recipe with name={stem} and path={path}. "
        "Do not add emitters to the restored asset. This chat now tracks only the new system."
    )


def _restore_then_edit_prompt(original: str, restored: str, old_path: str) -> str:
    target = old_path or "this chat's Niagara system"
    return (
        f"{original}\n\n"
        f"The sidecar already called restore_asset for {target}. Result:\n{restored}\n"
        "Do not call restore_asset again. Do not rebuild the graph to undo. "
        "Apply only the remaining edits to that restored system."
    )


def _is_vfx_reference_turn(message: str, has_images: bool) -> bool:
    if not has_images:
        return False
    text = (message or "").strip()
    if not text or text == "(image)":
        return False
    return _VFX_DO.search(text) is not None


def _vfx_sidecar_dir() -> Path:
    return Path(__file__).resolve().parent / "vfx"


def _vfx_path_block(project_dir: str, effect_id: str) -> str:
    rules = _vfx_sidecar_dir()
    effect = _effect_root(project_dir, effect_id)
    return (
        "VFX records for this effect:\n"
        f"- Rules: {rules / 'AGENTS.md'}\n"
        f"- Instructions: {rules / 'instructions'}\n"
        f"- Schemas (do not paste; save_vfx_json reads them): {rules / 'schema'}\n"
        f"- Effect folder: {effect}\n"
        "Write current_vfx.json, implementation_calibration.json, "
        "implementation_map.json, render_setup.json, validation_result.json, "
        "and iteration_history.json only with save_vfx_json. "
        "Do not Write those files another way. A claim that the JSON is valid is not enough.\n"
        "Order: save a schema-valid current_vfx.json before Unreal MCP. "
        "implementation_calibration.json may stay uncalibrated (empty anchors, null scale). "
        "Do not invent cm, speed, lifetime, count, smoke, embers, or glow. "
        "Then the MCP hands above."
    )


def _mesh_stage_block(project_dir: str, meta: Mapping[str, Any]) -> str:
    stage = str(meta.get("mesh_stage") or "")
    mesh_id = mf.sanitize_mesh_id(str(meta.get("mesh_id") or ""))
    if not mesh_id:
        return ""
    if stage == mf.STAGE_IMAGE:
        return (
            f"This chat has a mesh reference picture waiting for the user's OK: "
            f"{mf.ref_png(project_dir, mesh_id)}. "
            "If they want the picture changed, call request_mesh with the revised description, "
            "then GenerateImage once with the returned filePath. "
            "Do not run TRELLIS, import, or spawn meshes; the sidecar does that after they approve."
        )
    if stage in (mf.STAGE_APPLY, mf.STAGE_NAME):
        label = str(mf.read_mesh_meta(project_dir, mesh_id).get("preview_label") or mf.PREVIEW_LABEL)
        return (
            f"A TRELLIS mesh preview actor '{label}' is in the viewport, waiting for the user to "
            "apply it (with an asset name) or reject it. Do not call request_mesh, import meshes, "
            "or destroy that actor. You may move, rotate, scale, or focus it if they ask."
        )
    return ""


def _tex_stage_block(meta: Mapping[str, Any]) -> str:
    tex = meta.get("tex") or {}
    stage = str(tex.get("stage") or "")
    names = ", ".join(str(it.get("name")) for it in tex.get("items") or [])
    if not names:
        return ""
    if stage == tf.STAGE_IMAGE:
        return (
            f"Texture pictures are waiting for the user's OK: {names}. If they want one changed, "
            "call request_texture again with the same name and a revised description, then "
            "GenerateImage once with the returned filePath. Do not import or use these textures yet."
        )
    if stage == tf.STAGE_ALPHA:
        return (
            f"The user is choosing background removal A or B for: {names}. Do not import or use "
            "these textures yet. If they want a different picture, call request_texture again "
            "with the same name."
        )
    return ""


def _prompt_with_this_chat_only(
    project_dir: str,
    chat_id: str,
    message: str,
    include_history: bool,
    effect_id: str = "",
) -> str:
    all_turns = _load_chat_turns(project_dir, chat_id)
    kept, dropped = _trim_turns(all_turns)
    blocks = [_system_prompt(project_dir)]
    meta = _read_meta(project_dir, chat_id)
    ns_path = str(meta.get("niagara_path") or "").strip()
    ns_name = str(meta.get("niagara_name") or "").strip()
    if ns_path:
        blocks.append(
            f"This chat's Niagara system is {ns_path}. Later turns edit that path. "
            "Do not load leftover systems from other chats, including /Game/FX/NS_Live, "
            "unless the user named that path."
        )
    elif ns_name:
        blocks.append(
            f"The user named the new system NS_{ns_name} "
            f"(/Game/FX/NS_{ns_name}). Use that path. Do not invent another name."
        )
    elif _looks_like_effect_make(message):
        blocks.append(
            "This chat has no Niagara name yet. Reply with only: "
            "Niagara 시스템 이름을 알려주세요. "
            "Do not call apply_vfx_recipe, create_niagara_system, or apply_vfx_look until they answer."
        )
    mesh_block = _mesh_stage_block(project_dir, meta)
    if mesh_block:
        blocks.append(mesh_block)
    tex_block = _tex_stage_block(meta)
    if tex_block:
        blocks.append(tex_block)
    if effect_id:
        blocks.append(_vfx_path_block(project_dir, effect_id))
    if include_history:
        summary = _summarize_dropped(dropped)
        if summary:
            blocks.append(
                "Older turns for this chat_id (compressed; files kept on disk):\n"
                + summary
            )
        history = _format_chat_history(kept)
        if history:
            blocks.append(
                "Recent turns for this chat_id only (other Editor Prompt threads "
                "are not included):\n"
                + history
            )
    if _looks_like_recall(message):
        skip: set[int] = set()
        if include_history:
            skip = set(range(len(dropped), len(all_turns)))
        retrieved = _search_turns(all_turns, message, skip)
        if retrieved:
            blocks.append(
                "Retrieved from this chat_id turns/ for this question only:\n"
                + _format_chat_history(retrieved)
            )
    blocks.append(f"User:\n{message}")
    return "\n\n".join(blocks)


def _snapshot_job_images(key: tuple[str, str]) -> list[str]:
    with _run_lock:
        job = _jobs.get(key)
        return list(job.images) if job is not None else []


def _save_turn(
    project_dir: str,
    chat_id: str,
    user: str,
    reply: str,
    model: str,
    params: list[ModelParamIn],
    mcp_url: str,
    tools: list[dict[str, str]],
    agent_id: str,
    effect_id: str = "",
    gen_images: list[str] | None = None,
) -> None:
    root = _chat_root(project_dir, chat_id)
    turns_dir = root / "turns"
    existing = sorted(p for p in turns_dir.glob("*.json") if p.stem.isdigit())
    next_n = int(existing[-1].stem) + 1 if existing else 1
    turn_path = turns_dir / f"{next_n:04d}.json"
    images: list[str] = []
    for path in gen_images or ():
        text = str(path or "").strip()
        if text and text not in images:
            images.append(text)
    for tool in tools or ():
        for path in _png_paths_in(str(tool.get("result") or "")):
            if path not in images:
                images.append(path)
    payload = {
        "user": user,
        "reply": reply,
        "model": model,
        "model_params": [{"id": p.id, "value": p.value} for p in params],
        "mcp_url": mcp_url,
        "tools": tools,
        "gen_images": images,
    }
    turn_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")

    meta_path = root / "meta.json"
    now = datetime.now(timezone.utc).isoformat()
    meta: dict[str, Any] = {}
    if meta_path.is_file():
        try:
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except Exception:
            meta = {}
    if not isinstance(meta, dict):
        meta = {}
    meta["id"] = chat_id
    if not str(meta.get("title") or "").strip() or meta.get("title") == "New Agent":
        meta["title"] = user[:60]
    if "created" not in meta:
        meta["created"] = now
    meta["modified"] = now
    meta["model"] = model
    if agent_id:
        meta["agent_id"] = agent_id
    bound = _sanitize_effect_id(effect_id) or _sanitize_effect_id(
        str(meta.get("effect_id") or "")
    )
    if bound:
        meta["effect_id"] = bound
    meta_path.write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")


def _stop_requested(key: tuple[str, str]) -> bool:
    with _run_lock:
        return key in _cancel_requested


def _mcp_failed(text: str) -> bool:
    return text.startswith(("FAILED", "mcp error", "mcp http"))


def _field(text: str, name: str) -> str:
    hit = re.search(rf"(?:^|\s){name}=(\S+)", text or "")
    return hit.group(1) if hit else ""


def _fallback_image(project_dir: str, key: tuple[str, str], instruction: str) -> list[Any]:
    from cursor_sdk import Agent, AgentOptions, LocalAgentOptions

    patch_cursor_sdk_bridge()
    opts = AgentOptions(
        model=_model_arg(FALLBACK_MODEL, []),
        api_key=_api_key(),
        local=LocalAgentOptions(cwd=project_dir),
    )
    agent = _unwrap_agent(Agent.create(opts))
    results: list[Any] = []
    try:
        run = agent.send(instruction)
        with _run_lock:
            _active_runs[key] = run
        for msg in run.messages():
            if _stop_requested(key):
                _try_cancel_run(run)
                break
            if str(getattr(msg, "type", "") or "") != "tool_call":
                continue
            if not mf.is_generate_image_tool(str(getattr(msg, "name", "") or "")):
                continue
            status = str(getattr(msg, "status", "") or "").lower()
            if status not in ("", "running", "error"):
                results.append(getattr(msg, "result", None))
        if hasattr(run, "wait"):
            run.wait()
    finally:
        with _run_lock:
            _active_runs.pop(key, None)
        _close_agent(agent)
    return results


def _finish_mesh_image(
    project_dir: str,
    chat_id: str,
    key: tuple[str, str],
    model: str,
    image_results: list[Any],
) -> str:
    meta = _read_meta(project_dir, chat_id)
    mesh_id = mf.sanitize_mesh_id(str(meta.get("mesh_id") or ""))
    if not mesh_id:
        return ""
    target = mf.ref_png(project_dir, mesh_id)
    mesh_meta = mf.read_mesh_meta(project_dir, mesh_id)
    note = ""
    ok = mf.settle_ref_image(target, image_results)
    if not ok and model != FALLBACK_MODEL and not _stop_requested(key):
        _job_event(key, f"{model} 이(가) 그림을 만들지 못해 {FALLBACK_MODEL} 로 다시 그립니다")
        try:
            fallback = _fallback_image(
                project_dir,
                key,
                mf.image_agent_message(str(mesh_meta.get("description") or "object"), target),
            )
            ok = mf.settle_ref_image(target, fallback)
        except Exception:
            traceback.print_exc()
        if ok:
            note = f"({model} 모델이 그림을 만들지 못해 {FALLBACK_MODEL} 로 그렸습니다.)\n"
    if not ok:
        mf.write_mesh_meta(project_dir, mesh_id, stage="")
        _patch_chat_meta(project_dir, chat_id, mesh_stage="")
        return "레퍼런스 그림을 만들지 못했습니다. 다시 요청해 주세요."
    mf.write_mesh_meta(
        project_dir, mesh_id, stage=mf.STAGE_IMAGE, image_model=model if not note else FALLBACK_MODEL
    )
    _patch_chat_meta(project_dir, chat_id, mesh_stage=mf.STAGE_IMAGE)
    _job_image(key, str(target))
    return (
        note
        + "그림이 괜찮으면 '좋아'라고 해 주세요. TRELLIS로 모델링해서 뷰포트에 미리보기로 띄웁니다. "
        "바꾸고 싶으면 어떻게 바꿀지 말해 주세요."
    )


def _mesh_generate(
    project_dir: str,
    chat_id: str,
    key: tuple[str, str],
    mesh_id: str,
    mcp_url: str,
    retry: bool,
) -> tuple[str, list[dict[str, str]], bool]:
    from vfx_recipe import call_mcp

    tools: list[dict[str, str]] = []
    image = mf.ref_png(project_dir, mesh_id)
    if not image.is_file():
        _patch_chat_meta(project_dir, chat_id, mesh_stage="")
        return "레퍼런스 그림 파일이 없습니다. 모델을 다시 요청해 주세요.", tools, False
    base = mf.trellis_url()
    reachable, busy, _ = mf.trellis_ping(base)
    if not reachable:
        return f"TRELLIS에 연결할 수 없습니다 ({base}). {mf.trellis_start_hint()}", tools, False
    if busy:
        return "TRELLIS가 다른 생성 중입니다. 끝난 뒤 다시 '좋아'라고 해 주세요.", tools, False
    mesh_meta = mf.read_mesh_meta(project_dir, mesh_id)
    seed = int(mesh_meta.get("seed") or 1234) + (1 if retry else 0)
    glb = mf.out_glb(project_dir, mesh_id)
    _job_event(key, f"TRELLIS 생성 시작 ({base}, seed {seed})")
    try:
        size = mf.trellis_generate(
            base,
            image,
            glb,
            seed,
            lambda line: _job_event(key, line),
            lambda: _stop_requested(key),
        )
    except mf.Cancelled:
        return "", tools, True
    except Exception as exc:
        return str(exc), tools, False
    tools.append({"name": "trellis", "result": f"glb={glb} bytes={size} seed={seed}"})
    mf.write_mesh_meta(
        project_dir, mesh_id, seed=seed, trellis_form=mf.TRELLIS_FORM, glb_bytes=size
    )
    _job_event(key, "언리얼 미리보기 임포트")
    folder = mf.preview_folder(mesh_id)
    imported = call_mcp(
        mcp_url,
        "import_static_mesh",
        {
            "file": str(mf.named_glb(project_dir, mesh_id, "SM_Preview")),
            "dest": folder,
            "name": "SM_Preview",
            "label": mf.PREVIEW_LABEL,
            "preview": True,
            "replace": True,
            "height": float(mesh_meta.get("height_cm") or 0),
        },
    )
    tools.append({"name": "import_static_mesh", "result": imported[:300]})
    if _mcp_failed(imported):
        return f"GLB는 만들었지만 언리얼 미리보기 임포트에 실패했습니다: {imported[:200]}", tools, False
    label = _field(imported, "actor") or mf.PREVIEW_LABEL
    mf.write_mesh_meta(
        project_dir, mesh_id, stage=mf.STAGE_APPLY, preview_label=label, preview_folder=folder
    )
    _patch_chat_meta(project_dir, chat_id, mesh_stage=mf.STAGE_APPLY)
    return (
        f"TRELLIS 모델을 뷰포트에 미리보기({label})로 띄웠습니다. 돌려 보고 괜찮으면 "
        "'적용해'와 함께 에셋 이름을 알려 주세요 (예: 적용해, 이름은 Tree). "
        "다시 뽑으려면 '다시 만들어', 그림부터 바꾸려면 '그림부터'.",
        tools,
        False,
    )


def _mesh_discard_preview(project_dir: str, mesh_id: str, mcp_url: str) -> str:
    from vfx_recipe import call_mcp

    mesh_meta = mf.read_mesh_meta(project_dir, mesh_id)
    label = str(mesh_meta.get("preview_label") or "")
    folder = str(mesh_meta.get("preview_folder") or "")
    if not label and not folder:
        return ""
    result = call_mcp(
        mcp_url, "discard_mesh_preview", {"label": label, "folder": folder or mf.preview_folder(mesh_id)}
    )
    mf.write_mesh_meta(project_dir, mesh_id, preview_label="", preview_folder="")
    return result


def _mesh_name_ask(project_dir: str, chat_id: str, mesh_id: str) -> str:
    _patch_chat_meta(project_dir, chat_id, mesh_stage=mf.STAGE_NAME)
    mf.write_mesh_meta(project_dir, mesh_id, stage=mf.STAGE_NAME)
    hint = str(mf.read_mesh_meta(project_dir, mesh_id).get("suggested_name") or "Tree")
    return (
        f"메쉬 에셋 이름을 알려주세요. (예: {hint} → /Game/Meshes/{hint}/SM_{hint})"
    )


def _mesh_apply(
    project_dir: str, chat_id: str, mesh_id: str, mcp_url: str, name: str
) -> tuple[str, list[dict[str, str]], bool]:
    from vfx_recipe import call_mcp

    tools: list[dict[str, str]] = []
    glb = mf.out_glb(project_dir, mesh_id)
    if not glb.is_file():
        return "TRELLIS 결과 GLB가 없습니다. '다시 만들어'로 다시 뽑아 주세요.", tools, False
    mesh_meta = mf.read_mesh_meta(project_dir, mesh_id)
    preview_label = str(mesh_meta.get("preview_label") or "")
    dest = f"/Game/Meshes/{name}"
    applied = call_mcp(
        mcp_url,
        "import_static_mesh",
        {
            "file": str(mf.named_glb(project_dir, mesh_id, f"SM_{name}")),
            "dest": dest,
            "name": f"SM_{name}",
            "label": name,
            "copy_from": preview_label,
            "replace": False,
            "height": float(mesh_meta.get("height_cm") or 0),
        },
    )
    tools.append({"name": "import_static_mesh", "result": applied[:300]})
    if "exists=" in applied:
        _patch_chat_meta(project_dir, chat_id, mesh_stage=mf.STAGE_NAME)
        mf.write_mesh_meta(project_dir, mesh_id, stage=mf.STAGE_NAME)
        return f"{dest}/SM_{name} 이(가) 이미 있습니다. 다른 이름을 알려주세요.", tools, False
    if _mcp_failed(applied):
        return f"적용에 실패했습니다: {applied[:200]}", tools, False
    discarded = _mesh_discard_preview(project_dir, mesh_id, mcp_url)
    if discarded:
        tools.append({"name": "discard_mesh_preview", "result": discarded[:300]})
    mesh_path = _field(applied, "mesh") or f"{dest}/SM_{name}"
    actor = _field(applied, "actor") or name
    mf.write_mesh_meta(project_dir, mesh_id, stage="applied", name=name, final_path=mesh_path)
    _patch_chat_meta(
        project_dir, chat_id, mesh_stage="", mesh_id="", last_mesh_path=mesh_path, last_mesh_actor=actor
    )
    return (
        f"적용했습니다: {mesh_path} (액터 {actor}). 미리보기는 지웠고 메쉬 에셋은 저장했습니다. "
        "레벨 저장은 직접 해 주세요.",
        tools,
        False,
    )


def _mesh_turn(
    project_dir: str, chat_id: str, key: tuple[str, str], message: str, mcp_url: str
) -> tuple[str, list[dict[str, str]], bool] | None:
    """Deterministic mesh gates. None means: let the agent answer this turn."""
    meta = _read_meta(project_dir, chat_id)
    stage = str(meta.get("mesh_stage") or "")
    mesh_id = mf.sanitize_mesh_id(str(meta.get("mesh_id") or ""))
    if not mesh_id or stage not in (mf.STAGE_IMAGE, mf.STAGE_APPLY, mf.STAGE_NAME):
        return None
    if stage == mf.STAGE_IMAGE:
        if mf.is_cancel(message):
            mf.write_mesh_meta(project_dir, mesh_id, stage="cancelled")
            _patch_chat_meta(project_dir, chat_id, mesh_stage="", mesh_id="")
            return "모델링을 취소했습니다.", [], False
        if mf.is_approve(message):
            return _mesh_generate(project_dir, chat_id, key, mesh_id, mcp_url, retry=False)
        return None
    if mf.is_back_to_image(message):
        discarded = _mesh_discard_preview(project_dir, mesh_id, mcp_url)
        mf.write_mesh_meta(project_dir, mesh_id, stage=mf.STAGE_IMAGE)
        _patch_chat_meta(project_dir, chat_id, mesh_stage=mf.STAGE_IMAGE)
        return (
            "미리보기를 지우고 그림 단계로 돌아갔습니다. 그림을 어떻게 바꿀지 말해 주세요. "
            "지금 그림으로 다시 모델링하려면 '좋아'.",
            [{"name": "discard_mesh_preview", "result": discarded[:300]}],
            False,
        )
    if mf.is_retry(message):
        discarded = _mesh_discard_preview(project_dir, mesh_id, mcp_url)
        mf.write_mesh_meta(project_dir, mesh_id, stage=mf.STAGE_IMAGE)
        _patch_chat_meta(project_dir, chat_id, mesh_stage=mf.STAGE_IMAGE)
        reply, tools, cancelled = _mesh_generate(
            project_dir, chat_id, key, mesh_id, mcp_url, retry=True
        )
        return reply, [{"name": "discard_mesh_preview", "result": discarded[:300]}] + tools, cancelled
    if mf.is_cancel(message):
        discarded = _mesh_discard_preview(project_dir, mesh_id, mcp_url)
        mf.write_mesh_meta(project_dir, mesh_id, stage="cancelled")
        _patch_chat_meta(project_dir, chat_id, mesh_stage="", mesh_id="")
        return (
            "미리보기를 지우고 모델링을 취소했습니다.",
            [{"name": "discard_mesh_preview", "result": discarded[:300]}],
            False,
        )
    name = mf.extract_mesh_name(message)
    if name:
        return _mesh_apply(project_dir, chat_id, mesh_id, mcp_url, name)
    if stage == mf.STAGE_NAME:
        if mf.is_question(message):
            return None
        return _mesh_name_ask(project_dir, chat_id, mesh_id), [], False
    if mf.is_apply(message):
        return _mesh_name_ask(project_dir, chat_id, mesh_id), [], False
    return None


def _finish_texture_images(
    project_dir: str,
    chat_id: str,
    key: tuple[str, str],
    model: str,
    image_results: list[Any],
    message: str,
) -> str:
    tex = dict(_read_meta(project_dir, chat_id).get("tex") or {})
    if str(tex.get("stage") or "") != tf.STAGE_REQUESTED:
        return ""
    items = [dict(it) for it in tex.get("items") or []]
    present = {str(tf.raw_png(project_dir, it["name"]).resolve()) for it in items}
    unused: list[Any] = []
    by_name: dict[str, Any] = {}
    for result in image_results:
        got = mf.extract_image_result(result)
        if isinstance(got, Path) and got.is_file():
            if str(got.resolve()) in present:
                continue
            stem = got.stem.lower()
            owner = next((it["name"] for it in items if it["name"].lower() in stem), "")
            if owner and owner not in by_name:
                by_name[owner] = got
                continue
        unused.append(result)
    ok: list[dict[str, Any]] = []
    failed: list[str] = []
    used_fallback = False
    for it in items:
        target = tf.raw_png(project_dir, it["name"])
        done = mf.settle_ref_image(target, []) if target.is_file() else False
        if not done and it["name"] in by_name:
            done = mf.settle_ref_image(target, [by_name[it["name"]]])
        while not done and unused:
            done = mf.settle_ref_image(target, [unused.pop(0)])
        if not done and model != FALLBACK_MODEL and not _stop_requested(key):
            _job_event(key, f"{model} 이(가) {it['name']} 그림을 만들지 못해 {FALLBACK_MODEL} 로 다시 그립니다")
            try:
                done = mf.settle_ref_image(
                    target, _fallback_image(project_dir, key, tf.image_agent_message(it, target))
                )
                used_fallback = used_fallback or done
            except Exception:
                traceback.print_exc()
        if done:
            ok.append(it)
            _job_image(key, str(target))
        else:
            failed.append(it["name"])
    if not ok:
        _patch_chat_meta(project_dir, chat_id, tex={"stage": ""})
        return "텍스처 그림을 만들지 못했습니다. 다시 요청해 주세요."
    tex.update(stage=tf.STAGE_IMAGE, items=ok)
    if not str(tex.get("request") or "").strip():
        tex["request"] = message
    _patch_chat_meta(project_dir, chat_id, tex=tex)
    lines = [f"{i + 1}) {it['name']} ({it['use']})" for i, it in enumerate(ok)]
    note = ""
    if used_fallback:
        note += f"({model} 모델이 그림을 만들지 못한 것은 {FALLBACK_MODEL} 로 그렸습니다.)\n"
    if failed:
        note += "만들지 못한 그림: " + ", ".join(failed) + "\n"
    return (
        note
        + "텍스처 그림: "
        + ", ".join(lines)
        + "\n그림이 괜찮으면 '좋아'라고 해 주세요. 배경 제거 후보를 보여 드립니다. "
        "바꾸고 싶으면 몇 번을 어떻게 바꿀지 말해 주세요."
    )


@dataclass
class TexTurn:
    reply: str
    tools: list[dict[str, str]]
    run_message: str = ""


def _tex_continue(project_dir: str, chat_id: str, tex: dict[str, Any]) -> TexTurn:
    items = [dict(it) for it in tex.get("items") or []]
    lines = [tf.ready_line(it) for it in items]
    request = str(tex.get("request") or "").strip()
    _patch_chat_meta(project_dir, chat_id, tex={"stage": "", "last": lines})
    run_message = (
        "The user approved these textures. The final PNGs are ready:\n"
        + "\n".join(lines)
        + "\n\nContinue the original request now. import_texture each PNG with the kind shown, "
        "then build what the request needs with them. Do not call request_texture or "
        "GenerateImage for these names again unless the user asks for a new picture."
        + (f"\n\nOriginal request:\n{request}" if request else "")
    )
    names = ", ".join(str(it.get("name")) for it in items)
    return TexTurn(
        reply=f"텍스처를 확정했습니다: {names}",
        tools=[{"name": "texture_final", "result": " ".join("png=" + str(it.get("final")) for it in items)}],
        run_message=run_message,
    )


def _texture_turn(
    project_dir: str, chat_id: str, key: tuple[str, str], message: str
) -> TexTurn | None:
    """Deterministic texture gates. None means: let the agent answer this turn."""
    tex = dict(_read_meta(project_dir, chat_id).get("tex") or {})
    stage = str(tex.get("stage") or "")
    items = [dict(it) for it in tex.get("items") or []]
    if stage not in (tf.STAGE_IMAGE, tf.STAGE_ALPHA) or not items:
        return None
    if mf.is_question(message):
        return None
    if mf.is_cancel(message):
        _patch_chat_meta(project_dir, chat_id, tex={"stage": ""})
        return TexTurn("텍스처 작업을 취소했습니다.", [])
    if stage == tf.STAGE_IMAGE:
        if not mf.is_approve(message):
            return None
        _job_event(key, "배경 제거 후보를 만드는 중")
        tools: list[dict[str, str]] = []
        for it in items:
            try:
                tf.build_options(project_dir, it)
            except Exception as exc:
                traceback.print_exc()
                return TexTurn(f"{it['name']} 처리에 실패했습니다: {exc}", tools)
            if it.get("compare"):
                _job_image(key, str(it["compare"]))
            elif it.get("use") == "tile":
                _job_image(key, str(tf.tiled_png(project_dir, it["name"])))
            tools.append({"name": "texture_options", "result": f"{it['name']} a={it.get('a', '')} b={it.get('b', '')}"})
        keyed = [it for it in items if it.get("use") != "tile"]
        tex["items"] = items
        if not keyed:
            turn = _tex_continue(project_dir, chat_id, tex)
            turn.tools = tools + turn.tools
            return turn
        tex["stage"] = tf.STAGE_ALPHA
        _patch_chat_meta(project_dir, chat_id, tex=tex)
        lines = []
        for i, it in enumerate(keyed):
            b_text = it.get("b_how") if it.get("b") else f"없음 ({it.get('b_how')})"
            lines.append(f"{i + 1}) {it['name']}: A={it.get('a_how')}, B={b_text}")
        many = len(keyed) > 1
        return TexTurn(
            "배경을 지운 후보입니다. 체크무늬가 투명한 부분입니다.\n"
            + "\n".join(lines)
            + ("\n'1A 2B'처럼 고르거나, 전부 같으면 'A' 또는 'B'라고 해 주세요." if many else "\nA 또는 B로 골라 주세요.")
            + " 그림을 바꾸려면 '그림 다시'.",
            tools,
        )
    if mf.is_back_to_image(message):
        tex["stage"] = tf.STAGE_IMAGE
        _patch_chat_meta(project_dir, chat_id, tex=tex)
        return TexTurn("그림 단계로 돌아갔습니다. 어떻게 바꿀지 말해 주세요. 지금 그림으로 다시 진행하려면 '좋아'.", [])
    keyed = [it for it in items if it.get("use") != "tile"]
    picks = tf.parse_choices(message, len(keyed))
    if not picks:
        return None
    missing = [str(i + 1) for i in range(len(keyed)) if i not in picks]
    if missing:
        return TexTurn("아직 고르지 않은 번호가 있습니다: " + ", ".join(missing) + ". 예: '1A 2B'", [])
    for i, it in enumerate(keyed):
        if picks[i] == "b" and not it.get("b"):
            return TexTurn(f"{i + 1}) {it['name']}는 B가 없습니다 ({it.get('b_how')}). A로 골라 주세요.", [])
    for i, it in enumerate(keyed):
        tf.finalize(project_dir, it, picks[i])
    tex["items"] = items
    return _tex_continue(project_dir, chat_id, tex)


@app.get("/health")
def health() -> dict[str, Any]:
    key = _api_key()
    raw = os.environ.get("CURSOR_API_KEY", "").strip()
    return {
        "ok": True,
        "fallback_model": FALLBACK_MODEL,
        "api_key_set": bool(key),
        "api_key_placeholder": bool(raw) and not bool(key),
        "sessions": len(_sessions),
    }


def _variant_payload(item: Any) -> list[dict[str, Any]]:
    variants: list[dict[str, Any]] = []
    for variant in getattr(item, "variants", ()) or ():
        params = []
        for param in getattr(variant, "params", ()) or ():
            pid = str(getattr(param, "id", "") or "").strip()
            if pid:
                params.append(
                    {"id": pid, "value": str(getattr(param, "value", "") or "")}
                )
        name = str(getattr(variant, "display_name", "") or "").strip() or "Default"
        variants.append(
            {
                "display_name": name,
                "is_default": bool(getattr(variant, "is_default", False)),
                "params": params,
            }
        )
    if variants:
        if not any(v["is_default"] for v in variants):
            variants[0]["is_default"] = True
        return variants

    for pdef in getattr(item, "parameters", ()) or ():
        pid = str(getattr(pdef, "id", "") or "").strip()
        if not pid:
            continue
        for val in getattr(pdef, "values", ()) or ():
            value = str(getattr(val, "value", "") or "").strip()
            label = str(getattr(val, "display_name", "") or "").strip() or value
            if not value:
                continue
            variants.append(
                {
                    "display_name": label,
                    "is_default": False,
                    "params": [{"id": pid, "value": value}],
                }
            )
        break
    if not variants:
        variants.append({"display_name": "Default", "is_default": True, "params": []})
    else:
        variants[0]["is_default"] = True
    return variants


@app.get("/models")
def models() -> dict[str, Any]:
    ids: list[str] = []
    catalog: list[dict[str, Any]] = []
    if _api_key():
        try:
            from cursor_sdk import Cursor

            patch_cursor_sdk_bridge()
            for item in Cursor.models.list(api_key=_api_key()):
                mid = str(getattr(item, "id", "") or "").strip()
                if not mid or mid in ids:
                    continue
                ids.append(mid)
                catalog.append(
                    {
                        "id": mid,
                        "display_name": str(
                            getattr(item, "display_name", "") or mid
                        ),
                        "variants": _variant_payload(item),
                    }
                )
        except Exception:
            traceback.print_exc()
    return {"ids": ids, "models": catalog}


@app.post("/prompt", response_model=PromptResponse)
def prompt(body: PromptRequest) -> PromptResponse:
    project_dir = _resolve_project_dir(body.project_dir)
    if not project_dir:
        return PromptResponse(error="project_dir is not a directory")
    images, image_error = _resolve_images(project_dir, body.images)
    message = (body.message or "").strip()
    if image_error:
        return PromptResponse(error=image_error)
    if not message and not images:
        return PromptResponse(error="empty message")
    if not message:
        message = "(image)"
    if not _api_key():
        return PromptResponse(
            error="CURSOR_API_KEY missing or placeholder. sidecar/.env (Dashboard → Integrations)."
        )
    mcp_url = (body.mcp_url or DEFAULT_MCP).strip()
    chat_id = _chat_id(body.chat_id)
    model = _model(body.model)
    params = _clean_params(body.model_params)
    key = (project_dir, chat_id)
    with _run_lock:
        existing = _jobs.get(key)
        if existing is not None and existing.running:
            return PromptResponse(error="already running")
        _cancel_requested.discard(key)
        _jobs[key] = JobState()
    try:
        bound_effect_id = _resolve_prompt_effect_id(
            project_dir, chat_id, body.effect_id, body.new_effect
        )
        if not bound_effect_id and _is_vfx_reference_turn(message, bool(images)):
            bound_effect_id = _bind_chat_effect(project_dir, chat_id, "")
    except ValueError as exc:
        with _run_lock:
            _jobs.pop(key, None)
        return PromptResponse(error=str(exc))

    def work() -> None:
        reply = ""
        tools: list[dict[str, str]] = []
        cancelled = False
        job_error = ""
        run_message = message
        try:
            meta = _read_meta(project_dir, chat_id)
            ns_path = str(meta.get("niagara_path") or "").strip()
            pending = str(meta.get("pending_name_ask") or "").strip()
            named = _extract_user_ns_name(message)
            ask_only = False
            restore_followup = False
            prior_tools: list[dict[str, str]] = []
            if _has_restore_intent(message):
                from vfx_recipe import call_mcp
                ns_name = str(meta.get("niagara_name") or "").strip()
                current_stem = _sanitize_ns_stem(ns_path or ns_name)
                restore_target, new_stem = _split_restore_and_new_names(
                    message, current_stem
                )
                mcp_args: dict[str, Any] = {}
                path_arg = restore_target or ns_path
                if path_arg:
                    mcp_args["path"] = path_arg
                restored = call_mcp(mcp_url, "restore_asset", mcp_args)
                if not _looks_like_restore_followup(message, current_stem):
                    reply = (
                        "원래 에셋으로 되돌렸습니다.\n"
                        if _restore_ok(restored)
                        else "되돌리지 못했습니다.\n"
                    ) + restored
                    disk_user = message
                    if images:
                        disk_user += "\n" + "\n".join("image=" + path for path in images)
                    try:
                        _save_turn(
                            project_dir,
                            chat_id,
                            disk_user,
                            reply,
                            model,
                            params,
                            mcp_url,
                            [{"name": "restore_asset", "result": restored[:300]}],
                            "",
                            bound_effect_id,
                        )
                    except Exception:
                        traceback.print_exc()
                    return
                restore_followup = True
                prior_tools.append({"name": "restore_asset", "result": restored[:300]})
                if new_stem:
                    _patch_chat_meta(
                        project_dir,
                        chat_id,
                        niagara_name=new_stem,
                        niagara_path="",
                    )
                    run_message = _restore_then_new_system_prompt(
                        message, restored, path_arg, new_stem
                    )
                else:
                    run_message = _restore_then_edit_prompt(
                        message, restored, path_arg
                    )
            mesh_result = _mesh_turn(project_dir, chat_id, key, message, mcp_url)
            if mesh_result is not None:
                reply, tools, cancelled = mesh_result
                if cancelled:
                    return
                disk_user = message
                if images:
                    disk_user += "\n" + "\n".join("image=" + path for path in images)
                try:
                    _save_turn(
                        project_dir,
                        chat_id,
                        disk_user,
                        reply,
                        model,
                        params,
                        mcp_url,
                        tools,
                        "",
                        bound_effect_id,
                        _snapshot_job_images(key),
                    )
                except Exception:
                    traceback.print_exc()
                return
            tex_turn = _texture_turn(project_dir, chat_id, key, message)
            tex_reply = ""
            if tex_turn is not None and not tex_turn.run_message:
                disk_user = message
                if images:
                    disk_user += "\n" + "\n".join("image=" + path for path in images)
                reply, tools = tex_turn.reply, tex_turn.tools
                try:
                    _save_turn(
                        project_dir,
                        chat_id,
                        disk_user,
                        reply,
                        model,
                        params,
                        mcp_url,
                        tools,
                        "",
                        bound_effect_id,
                        _snapshot_job_images(key),
                    )
                except Exception:
                    traceback.print_exc()
                return
            if tex_turn is not None:
                run_message = tex_turn.run_message
                tex_reply = tex_turn.reply
                prior_tools.extend(tex_turn.tools)
            if not ns_path and not restore_followup and not tex_reply:
                if pending:
                    if named:
                        _patch_chat_meta(
                            project_dir,
                            chat_id,
                            niagara_name=named,
                            pending_name_ask="",
                        )
                        run_message = _named_system_followup(pending, named)
                    elif _is_how_or_plan(message):
                        run_message = (
                            message
                            + "\n\nThis chat is still waiting for a Niagara asset name. "
                            "Answer in words only. Do not create assets."
                        )
                    else:
                        reply = _NAME_ASK_REPLY
                        ask_only = True
                elif _looks_like_effect_make(message):
                    if named:
                        _patch_chat_meta(project_dir, chat_id, niagara_name=named)
                        run_message = _named_system_followup(message, named)
                    else:
                        _patch_chat_meta(
                            project_dir, chat_id, pending_name_ask=message
                        )
                        reply = _NAME_ASK_REPLY
                        ask_only = True
            if ask_only:
                disk_user = message
                if images:
                    disk_user += "\n" + "\n".join("image=" + path for path in images)
                try:
                    _save_turn(
                        project_dir,
                        chat_id,
                        disk_user,
                        reply,
                        model,
                        params,
                        mcp_url,
                        [],
                        "",
                        bound_effect_id,
                    )
                except Exception:
                    traceback.print_exc()
                return
            agent, fresh = _get_agent(project_dir, chat_id, mcp_url, model, params)
            user_block = _prompt_with_this_chat_only(
                project_dir,
                chat_id,
                run_message,
                include_history=fresh,
                effect_id=bound_effect_id,
            )
            payload = _make_send_message(user_block, images)
            image_results: list[Any] = []

            def on_tool(name: str, status: str, result: Any) -> None:
                if mf.is_generate_image_tool(name) and status not in ("", "running", "error"):
                    image_results.append(result)

            try:
                reply, tools, cancelled = _run_text(
                    agent,
                    payload,
                    mcp_url,
                    key,
                    on_event=lambda line: _job_event(key, line),
                    on_tool=on_tool,
                )
            except Exception as send_exc:
                if not _is_stale_run_error(send_exc):
                    raise
                _drop_session(key)
                agent, _fresh = _get_agent(
                    project_dir, chat_id, mcp_url, model, params
                )
                user_block = _prompt_with_this_chat_only(
                    project_dir,
                    chat_id,
                    run_message,
                    include_history=True,
                    effect_id=bound_effect_id,
                )
                payload = _make_send_message(user_block, images)
                reply, tools, cancelled = _run_text(
                    agent,
                    payload,
                    mcp_url,
                    key,
                    on_event=lambda line: _job_event(key, line),
                    on_tool=on_tool,
                )
            if cancelled:
                return
            if prior_tools:
                tools = prior_tools + tools
            if str(_read_meta(project_dir, chat_id).get("mesh_stage") or "") == mf.STAGE_IMAGE_REQUESTED:
                mesh_note = _finish_mesh_image(project_dir, chat_id, key, model, image_results)
                if mesh_note:
                    reply = ((reply or "").strip() + "\n\n" + mesh_note).strip()
            tex_note = _finish_texture_images(
                project_dir, chat_id, key, model, image_results, message
            )
            if tex_note:
                reply = ((reply or "").strip() + "\n\n" + tex_note).strip()
            if tex_reply:
                reply = (tex_reply + "\n\n" + (reply or "").strip()).strip()
            agent_id = str(
                getattr(agent, "agent_id", "") or getattr(agent, "agentId", "") or ""
            )
            disk_user = message
            if images:
                disk_user += "\n" + "\n".join("image=" + path for path in images)
            try:
                _save_turn(
                    project_dir,
                    chat_id,
                    disk_user,
                    reply or "(empty reply)",
                    model,
                    params,
                    mcp_url,
                    tools,
                    agent_id,
                    bound_effect_id,
                    _snapshot_job_images(key),
                )
            except Exception:
                traceback.print_exc()
        except Exception as exc:
            with _run_lock:
                stopping = key in _cancel_requested
            if stopping or "cancel" in str(exc).lower():
                cancelled = True
                return
            traceback.print_exc()
            _drop_session(key)
            msg = str(exc)
            if _api_key() and _api_key() in msg:
                job_error = "Cursor Agent.create/send failed. Check CURSOR_API_KEY."
            else:
                job_error = "Cursor Agent failed: " + msg[:400]
        finally:
            with _run_lock:
                job = _jobs.get(key)
                if job is not None:
                    job.running = False
                    job.cancelled = cancelled
                    if cancelled:
                        job.error = "cancelled"
                    elif job_error:
                        job.error = job_error
                    else:
                        job.reply = reply or "(empty reply)"
                _active_runs.pop(key, None)
                _cancel_requested.discard(key)

    threading.Thread(target=work, daemon=True, name="editor-prompt-run").start()
    return PromptResponse(running=True)


@app.get("/effect", response_model=EffectBindResponse)
def get_effect(project_dir: str = "", chat_id: str = "default") -> EffectBindResponse:
    resolved = _resolve_project_dir(project_dir)
    if not resolved:
        return EffectBindResponse(error="project_dir is not a directory")
    cid = _chat_id(chat_id)
    eid = _sanitize_effect_id(str(_read_meta(resolved, cid).get("effect_id") or ""))
    if not eid:
        return EffectBindResponse(ok=True)
    try:
        path = _effect_root(resolved, eid)
    except ValueError as exc:
        return EffectBindResponse(error=str(exc))
    return EffectBindResponse(ok=True, effect_id=eid, path=str(path))


@app.post("/effect", response_model=EffectBindResponse)
def bind_effect(body: EffectBindRequest) -> EffectBindResponse:
    project_dir = _resolve_project_dir(body.project_dir)
    if not project_dir:
        return EffectBindResponse(error="project_dir is not a directory")
    chat_id = _chat_id(body.chat_id)
    requested = _sanitize_effect_id(body.effect_id)
    existing = _sanitize_effect_id(
        str(_read_meta(project_dir, chat_id).get("effect_id") or "")
    )
    try:
        if requested:
            eid = _bind_chat_effect(project_dir, chat_id, requested)
        elif existing:
            _effect_root(project_dir, existing)
            eid = existing
        else:
            eid = _bind_chat_effect(project_dir, chat_id, "")
        path = _effect_root(project_dir, eid)
    except ValueError as exc:
        return EffectBindResponse(error=str(exc))
    return EffectBindResponse(ok=True, effect_id=eid, path=str(path))


@app.post("/status", response_model=StatusResponse)
def status(body: StatusRequest) -> StatusResponse:
    project_dir = _resolve_project_dir(body.project_dir)
    chat_id = _chat_id(body.chat_id)
    if not project_dir:
        return StatusResponse(error="project_dir is not a directory")
    key = (project_dir, chat_id)
    after = max(0, int(body.after or 0))
    with _run_lock:
        job = _jobs.get(key)
        if job is None:
            return StatusResponse(running=False, after=after)
        events = list(job.events[after:])
        next_after = after + len(events)
        images = list(job.images)
        if job.running:
            return StatusResponse(
                running=True,
                events=events,
                after=next_after,
                images=images,
            )
        return StatusResponse(
            running=False,
            events=events,
            after=next_after,
            reply=job.reply,
            error=job.error,
            images=images,
        )


@app.post("/cancel")
def cancel(body: CancelRequest) -> dict[str, Any]:
    project_dir = _resolve_project_dir(body.project_dir)
    chat_id = _chat_id(body.chat_id)
    if not project_dir:
        return {"ok": False, "error": "project_dir is not a directory"}
    key = (project_dir, chat_id)
    with _run_lock:
        _cancel_requested.add(key)
        run = _active_runs.get(key)
    if run is not None:
        _try_cancel_run(run)
    return {"ok": True}
