"""
Start the local TRELLIS StableProjectorz API using TRELLIS_HOME / TRELLIS_URL from sidecar/.env.

  .venv\\Scripts\\python start_trellis.py
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlparse

from dotenv import load_dotenv

import mesh_flow as mf


def _python_for(home: Path) -> Path | None:
    candidates = [
        (home / "code" / "venv" / "Scripts" / "python.exe", home / "code" / "venv" / "Lib" / "site-packages" / "torch"),
        (home / "tools" / "python" / "python.exe", home / "tools" / "python" / "Lib" / "site-packages" / "torch"),
    ]
    for exe, torch_dir in candidates:
        if exe.is_file() and torch_dir.is_dir():
            return exe
    return None


def main() -> int:
    load_dotenv(Path(__file__).resolve().parent / ".env")
    raw_home = os.environ.get("TRELLIS_HOME", "").strip().strip('"').strip("'")
    if not raw_home:
        print("sidecar/.env 에 TRELLIS_HOME (TRELLIS 설치 폴더)을 적어 주세요.")
        return 1
    home = Path(raw_home)
    main_api = home / "code" / "api_spz" / "main_api.py"
    if not main_api.is_file():
        print(f"TRELLIS_HOME 에 code/api_spz/main_api.py 가 없습니다: {home}")
        return 1
    python = _python_for(home)
    if python is None:
        print("torch 가 설치된 TRELLIS Python을 찾지 못했습니다. TRELLIS 설치를 먼저 끝내 주세요.")
        return 1
    url = urlparse(mf.trellis_url())
    host = url.hostname or "127.0.0.1"
    port = url.port or 7960
    tools = home / "tools"
    env = dict(os.environ)
    env.pop("CURSOR_API_KEY", None)
    env.update(
        {
            "HF_HOME": str(home / "code" / "models"),
            "HF_HUB_OFFLINE": "1",
            "TRANSFORMERS_OFFLINE": "1",
            "OPENCV_IO_ENABLE_OPENEXR": "1",
            "ATTN_BACKEND": "xformers",
            "PYTORCH_CUDA_ALLOC_CONF": "max_split_size_mb:128,expandable_segments:True",
            "PYTHONNOUSERSITE": "1",
            "PYTHONPATH": "",
            "SETUPTOOLS_USE_DISTUTILS": "",
            "PATH": os.pathsep.join(
                [
                    str(tools / "python"),
                    str(tools / "python" / "Scripts"),
                    str(tools / "git" / "mingw64" / "bin"),
                    str(tools / "git" / "cmd"),
                    env.get("PATH", ""),
                ]
            ),
        }
    )
    print(f"TRELLIS API: {python} on {host}:{port}. 첫 기동은 모델 로드로 몇 분 걸립니다.", flush=True)
    return subprocess.call(
        [str(python), str(main_api), "--host", host, "--port", str(port)],
        cwd=str(home / "code"),
        env=env,
    )


if __name__ == "__main__":
    sys.exit(main())
