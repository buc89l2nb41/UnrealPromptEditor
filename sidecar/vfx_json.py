"""Schema-gated writes into Saved/PromptEditor/effects/<id>/."""

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any, Mapping

from jsonschema import Draft202012Validator

SCHEMA_DIR = Path(__file__).resolve().parent / "vfx" / "schema"

RECORD_SCHEMAS = {
    "current_vfx.json": "vfx_schema.json",
    "implementation_calibration.json": "implementation_calibration_schema.json",
    "implementation_map.json": "implementation_map_schema.json",
    "render_setup.json": "render_setup_schema.json",
    "validation_result.json": "validation_schema.json",
    "iteration_history.json": "iteration_history_schema.json",
}

_SCHEMA_CACHE: dict[str, dict[str, Any]] = {}


def normalize_record_file(raw: str) -> str:
    name = (raw or "").strip()
    if name in RECORD_SCHEMAS:
        return name
    if not name.endswith(".json"):
        with_ext = name + ".json"
        if with_ext in RECORD_SCHEMAS:
            return with_ext
    return ""


def _load_schema(schema_name: str) -> dict[str, Any]:
    cached = _SCHEMA_CACHE.get(schema_name)
    if cached is not None:
        return cached
    path = SCHEMA_DIR / schema_name
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("schema is not an object")
    _SCHEMA_CACHE[schema_name] = data
    return data


def _parse_payload(raw: Any) -> dict[str, Any] | str:
    if isinstance(raw, Mapping):
        return dict(raw)
    if isinstance(raw, str):
        text = raw.strip()
        if not text:
            return "error=json is empty"
        try:
            loaded = json.loads(text)
        except json.JSONDecodeError as exc:
            return "error=json is not valid JSON: " + exc.msg
        if not isinstance(loaded, dict):
            return "error=json must be an object"
        return loaded
    if raw is None:
        return "error=json required"
    return "error=json must be an object"


def _format_schema_errors(errors: list[Any], file: str) -> str:
    lines = ["error=schema", "file=" + file]
    for err in errors[:12]:
        loc = ".".join(str(part) for part in err.absolute_path) or "$"
        lines.append("- " + loc + ": " + err.message)
    if len(errors) > 12:
        lines.append("- … " + str(len(errors) - 12) + " more")
    return "\n".join(lines)


def validate_record(file: str, payload: dict[str, Any]) -> str:
    schema_name = RECORD_SCHEMAS[file]
    schema = _load_schema(schema_name)
    validator = Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(payload), key=lambda e: list(e.absolute_path))
    if errors:
        return _format_schema_errors(errors, file)
    return ""


def _read_existing(path: Path) -> dict[str, Any] | str | None:
    if not path.is_file():
        return None
    try:
        loaded = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return "error=existing " + path.name + " is not valid JSON"
    if not isinstance(loaded, dict):
        return "error=existing " + path.name + " is not an object"
    return loaded


def _merge_iteration_history(
    existing: dict[str, Any] | None, incoming: dict[str, Any]
) -> dict[str, Any] | str:
    if existing is None:
        return incoming
    old = existing.get("iterations")
    new = incoming.get("iterations")
    if not isinstance(old, list) or not isinstance(new, list):
        return "error=iteration_history iterations must be arrays"
    merged = copy.deepcopy(existing)
    merged.update(copy.deepcopy(incoming))
    if len(new) == 0:
        merged["iterations"] = old
        return merged
    if len(new) == 1:
        if old and new[0] == old[-1]:
            merged["iterations"] = old
            return merged
        merged["iterations"] = old + new
        return merged
    if new[: len(old)] == old:
        merged["iterations"] = new
        return merged
    return (
        "error=iteration_history is append-only; "
        "send one new iteration or the existing list plus new items"
    )


def _atomic_write(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(payload, ensure_ascii=False, indent=2)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(text, encoding="utf-8")
    tmp.replace(path)


def save_vfx_record(effect_dir: Path, file_name: str, raw_json: Any) -> str:
    file = normalize_record_file(file_name)
    if not file:
        names = ", ".join(RECORD_SCHEMAS)
        return "error=unknown file; use one of: " + names
    payload = _parse_payload(raw_json)
    if isinstance(payload, str):
        return payload
    dest = effect_dir / file
    try:
        dest.relative_to(effect_dir.resolve())
    except ValueError:
        return "error=file is outside the effect folder"
    if file == "iteration_history.json":
        existing = _read_existing(dest)
        if isinstance(existing, str):
            return existing
        merged = _merge_iteration_history(existing, payload)
        if isinstance(merged, str):
            return merged
        payload = merged
    failed = validate_record(file, payload)
    if failed:
        return failed
    _atomic_write(dest, payload)
    return "ok=true\nfile=" + file + "\npath=" + str(dest.resolve())
