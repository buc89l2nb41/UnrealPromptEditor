"""VFX Recipe storage and retrieval."""

from __future__ import annotations

import json
import re
import shutil
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional


@dataclass
class VFXRecipe:
    """VFX 레시피 데이터 구조."""
    
    # 기본 정보
    id: str                          # 고유 ID (폴더명)
    name: str                        # 표시 이름 (한글/영어)
    description: str                 # 효과 설명
    
    # 레시피 핵심
    template: str                    # 기반 템플릿 (Fountain, HangingParticulates 등)
    add_modules: list[str] = field(default_factory=list)  # 추가 모듈
    params: dict[str, Any] = field(default_factory=dict)  # 파라미터
    
    # 검색용 메타데이터
    tags: list[str] = field(default_factory=list)
    
    # 품질/버전 정보
    version: int = 1
    satisfaction: str = "high"       # low, medium, high
    iterations: int = 1              # 수정 횟수
    
    # 연관 정보
    niagara_path: str = ""           # /Game/FX/NS_xxx
    chat_id: str = ""                # 생성된 채팅 ID
    
    # 시간 정보
    created: str = ""
    modified: str = ""
    
    # 시각 참조
    thumbnail: str = ""              # thumbnail.png 경로
    
    def __post_init__(self):
        now = datetime.now(timezone.utc).isoformat()
        if not self.created:
            self.created = now
        if not self.modified:
            self.modified = now
    
    def to_dict(self) -> dict[str, Any]:
        return asdict(self)
    
    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "VFXRecipe":
        return cls(
            id=data.get("id", ""),
            name=data.get("name", ""),
            description=data.get("description", ""),
            template=data.get("template", ""),
            add_modules=data.get("add_modules", []),
            params=data.get("params", {}),
            tags=data.get("tags", []),
            version=data.get("version", 1),
            satisfaction=data.get("satisfaction", "high"),
            iterations=data.get("iterations", 1),
            niagara_path=data.get("niagara_path", ""),
            chat_id=data.get("chat_id", ""),
            created=data.get("created", ""),
            modified=data.get("modified", ""),
            thumbnail=data.get("thumbnail", ""),
        )


def _recipes_root(project_dir: str) -> Path:
    """레시피 저장 루트 디렉토리."""
    return Path(project_dir) / "Saved" / "PromptEditor" / "recipes"


def _sanitize_id(name: str) -> str:
    """이름을 안전한 폴더명으로 변환."""
    # 한글 → 로마자 변환 (간단한 방식)
    text = name.strip().lower()
    # 영숫자와 언더스코어만 유지
    cleaned = re.sub(r"[^a-z0-9_\uac00-\ud7af]", "_", text)
    # 연속 언더스코어 제거
    cleaned = re.sub(r"_+", "_", cleaned).strip("_")
    # 한글이 포함되어 있으면 해시 추가
    if re.search(r"[\uac00-\ud7af]", cleaned):
        import hashlib
        h = hashlib.md5(name.encode()).hexdigest()[:6]
        cleaned = re.sub(r"[\uac00-\ud7af]+", "", cleaned).strip("_")
        cleaned = f"{cleaned}_{h}" if cleaned else h
    return cleaned[:40] or "unnamed"


def save_recipe(
    project_dir: str,
    recipe: VFXRecipe,
    thumbnail_source: Optional[str] = None,
) -> str:
    """레시피 저장.
    
    Args:
        project_dir: 언리얼 프로젝트 루트
        recipe: 저장할 레시피
        thumbnail_source: 섬네일 소스 이미지 경로 (복사할 파일)
    
    Returns:
        저장된 레시피 경로
    """
    root = _recipes_root(project_dir)
    root.mkdir(parents=True, exist_ok=True)
    
    # ID가 없으면 이름에서 생성
    if not recipe.id:
        recipe.id = _sanitize_id(recipe.name)
    
    # 레시피 폴더
    recipe_dir = root / recipe.id
    
    # 버전 관리: 이미 존재하면 버전 증가
    if recipe_dir.exists():
        existing = load_recipe(project_dir, recipe.id)
        if existing:
            recipe.version = existing.version + 1
    
    recipe_dir.mkdir(parents=True, exist_ok=True)
    
    # 시간 업데이트
    recipe.modified = datetime.now(timezone.utc).isoformat()
    
    # recipe.json 저장
    recipe_path = recipe_dir / "recipe.json"
    recipe_path.write_text(
        json.dumps(recipe.to_dict(), ensure_ascii=False, indent=2),
        encoding="utf-8"
    )
    
    # 섬네일 복사
    if thumbnail_source:
        src = Path(thumbnail_source)
        if src.is_file():
            dst = recipe_dir / "thumbnail.png"
            shutil.copy2(src, dst)
            recipe.thumbnail = str(dst)
    
    # 인덱스 업데이트
    update_recipe_index(project_dir)
    
    return str(recipe_path)


def load_recipe(project_dir: str, recipe_id: str) -> Optional[VFXRecipe]:
    """레시피 로드."""
    root = _recipes_root(project_dir)
    recipe_path = root / recipe_id / "recipe.json"
    
    if not recipe_path.is_file():
        return None
    
    try:
        data = json.loads(recipe_path.read_text(encoding="utf-8"))
        return VFXRecipe.from_dict(data)
    except Exception:
        return None


def list_recipes(project_dir: str) -> list[VFXRecipe]:
    """모든 레시피 목록."""
    root = _recipes_root(project_dir)
    if not root.is_dir():
        return []
    
    recipes = []
    for recipe_dir in root.iterdir():
        if not recipe_dir.is_dir():
            continue
        recipe = load_recipe(project_dir, recipe_dir.name)
        if recipe:
            recipes.append(recipe)
    
    return sorted(recipes, key=lambda r: r.modified, reverse=True)


def search_recipes(
    project_dir: str,
    query: str,
    limit: int = 5,
) -> list[VFXRecipe]:
    """레시피 검색 (이름, 태그, 설명 기반).
    
    Args:
        project_dir: 프로젝트 루트
        query: 검색어
        limit: 최대 결과 수
    
    Returns:
        매칭된 레시피 목록 (점수순)
    """
    recipes = list_recipes(project_dir)
    if not recipes:
        return []
    
    query_lower = query.lower()
    query_tokens = set(re.findall(r"[a-z0-9]+|[\uac00-\ud7af]+", query_lower))
    
    scored: list[tuple[int, VFXRecipe]] = []
    
    for recipe in recipes:
        score = 0
        
        # 정확한 이름 매칭 (높은 점수)
        if query_lower == recipe.name.lower():
            score += 100
        elif query_lower in recipe.name.lower():
            score += 50
        
        # 태그 매칭
        for tag in recipe.tags:
            if query_lower in tag.lower():
                score += 30
            for token in query_tokens:
                if token in tag.lower():
                    score += 10
        
        # 설명 매칭
        desc_lower = recipe.description.lower()
        for token in query_tokens:
            if token in desc_lower:
                score += 5
        
        # ID 매칭
        if query_lower in recipe.id.lower():
            score += 20
        
        if score > 0:
            scored.append((score, recipe))
    
    # 점수순 정렬
    scored.sort(key=lambda x: -x[0])
    
    return [recipe for _, recipe in scored[:limit]]


def get_recipe_index(project_dir: str) -> dict[str, Any]:
    """레시피 인덱스 반환 (빠른 검색용)."""
    root = _recipes_root(project_dir)
    index_path = root / "index.json"
    
    if not index_path.is_file():
        return {"recipes": [], "updated": ""}
    
    try:
        return json.loads(index_path.read_text(encoding="utf-8"))
    except Exception:
        return {"recipes": [], "updated": ""}


def update_recipe_index(project_dir: str) -> str:
    """레시피 인덱스 업데이트."""
    root = _recipes_root(project_dir)
    root.mkdir(parents=True, exist_ok=True)
    
    recipes = list_recipes(project_dir)
    
    index = {
        "updated": datetime.now(timezone.utc).isoformat(),
        "count": len(recipes),
        "recipes": [
            {
                "id": r.id,
                "name": r.name,
                "tags": r.tags,
                "template": r.template,
                "version": r.version,
            }
            for r in recipes
        ],
    }
    
    index_path = root / "index.json"
    index_path.write_text(
        json.dumps(index, ensure_ascii=False, indent=2),
        encoding="utf-8"
    )
    
    return str(index_path)
