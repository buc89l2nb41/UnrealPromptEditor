"""Must be called before Bridge.launch / Agent.create on Windows."""

from __future__ import annotations

import os
import queue
import threading
import time
from typing import Any, Mapping


def patch_os_blocking() -> None:
    if hasattr(os, "get_blocking") and hasattr(os, "set_blocking"):
        return

    def _get_blocking(_fd: int) -> bool:
        return True

    def _set_blocking(_fd: int, _blocking: bool) -> None:
        return None

    os.get_blocking = _get_blocking  # type: ignore[attr-defined, assignment]
    os.set_blocking = _set_blocking  # type: ignore[attr-defined, assignment]


def patch_cursor_sdk_bridge() -> None:
    patch_os_blocking()
    if os.name != "nt":
        return

    import cursor_sdk._bridge as bridge

    if getattr(bridge, "_editor_prompt_win_patched", False):
        return

    CursorSDKError = bridge.CursorSDKError
    parse_discovery_line = bridge.parse_discovery_line

    def _read_discovery_windows(process: Any, timeout: float) -> Mapping[str, Any]:
        if process.stderr is None:
            raise CursorSDKError("Bridge process stderr is unavailable")

        result_q: queue.Queue = queue.Queue()
        stderr_lines: list[str] = []

        def worker() -> None:
            try:
                while True:
                    line = process.stderr.readline()
                    if line == "":
                        result_q.put(("eof", "".join(stderr_lines)))
                        return
                    stderr_lines.append(line)
                    discovery = parse_discovery_line(line if line.endswith("\n") else line + "\n")
                    if discovery is not None:
                        result_q.put(("ok", discovery))
                        return
            except Exception as exc:  # noqa: BLE001
                result_q.put(("err", exc))

        thread = threading.Thread(target=worker, name="cursor-bridge-discovery", daemon=True)
        thread.start()
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            remaining = max(0.05, deadline - time.monotonic())
            try:
                kind, payload = result_q.get(timeout=min(0.2, remaining))
            except queue.Empty:
                if process.poll() is not None and not thread.is_alive():
                    try:
                        kind, payload = result_q.get_nowait()
                    except queue.Empty:
                        raise CursorSDKError(
                            f"Bridge exited before discovery with status {process.poll()}: "
                            + "".join(stderr_lines)
                        ) from None
                else:
                    continue

            if kind == "ok":
                return payload  # type: ignore[return-value]
            if kind == "err":
                raise CursorSDKError(f"Bridge discovery failed: {payload}") from payload  # type: ignore[misc]
            if kind == "eof":
                raise CursorSDKError("Bridge exited before discovery: " + str(payload))
        raise CursorSDKError("Timed out waiting for bridge discovery")

    bridge._read_discovery = _read_discovery_windows  # type: ignore[assignment]
    bridge._editor_prompt_win_patched = True
