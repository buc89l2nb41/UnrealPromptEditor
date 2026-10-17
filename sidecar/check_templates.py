"""
Check vfx/templates.json list_name values against the editor's Niagara templates.

Unreal Editor with the EditorPrompt plugin must be running.

  .venv\\Scripts\\python check_templates.py [mcp_url]
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from vfx_recipe import call_mcp

DEFAULT_MCP = "http://127.0.0.1:18765/mcp"


def main() -> int:
    mcp_url = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_MCP
    listing = call_mcp(mcp_url, "list_niagara_templates", {"look": "all", "max": 40})
    if listing.startswith("mcp "):
        print(listing)
        print("Start Unreal Editor with the EditorPrompt plugin, then run again.")
        return 2
    paths = [line.split()[0] for line in listing.splitlines() if line.startswith("/")]
    catalog = json.loads(
        (Path(__file__).resolve().parent / "vfx" / "templates.json").read_text(encoding="utf-8")
    )
    failed = 0
    for entry in catalog.get("templates") or []:
        if entry.get("kind") != "niagara_template":
            continue
        key = str(entry.get("list_name") or "").lower()
        hits = [p for p in paths if key and key in p.rsplit("/", 1)[-1].lower()]
        status = "OK  " if len(hits) == 1 else ("MISS" if not hits else "MANY")
        failed += status != "OK  "
        print(f"{status} {entry.get('id'):<22} list_name={entry.get('list_name')}")
        for hit in hits:
            print(f"       {hit}")
    print(f"\n{len(paths)} templates in editor, {failed} entries need attention.")
    print("All editor templates:\n" + "\n".join(f"  {p}" for p in paths))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
