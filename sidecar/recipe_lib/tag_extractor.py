"""VFX 효과 설명에서 태그 자동 추출."""

from __future__ import annotations

import re
from typing import Set

# 태그 키워드 매핑 (키워드 → 정규화된 태그)
TAG_KEYWORDS: dict[str, str] = {
    # 효과 유형 - 한글
    "검기": "sword",
    "검": "sword",
    "칼": "sword",
    "베기": "slash",
    "슬래시": "slash",
    "무기": "weapon",
    "휘두": "swing",
    "총": "gun",
    "총알": "bullet",
    "총구": "muzzle",
    "발사": "shoot",
    "폭발": "explosion",
    "터지": "explosion",
    "히트": "hit",
    "타격": "hit",
    "충돌": "impact",
    "스파크": "spark",
    "불": "fire",
    "화염": "flame",
    "모닥불": "campfire",
    "횃불": "torch",
    "분수": "fountain",
    "물": "water",
    "낙엽": "leaf",
    "나뭇잎": "leaf",
    "눈": "snow",
    "눈송이": "snowflake",
    "꽃잎": "petal",
    "벚꽃": "cherry_blossom",
    "꽃가루": "pollen",
    "먼지": "dust",
    "마법": "magic",
    "오라": "aura",
    "힐링": "healing",
    "치유": "healing",
    "버프": "buff",
    "연기": "smoke",
    "안개": "fog",
    "번개": "lightning",
    "전기": "electric",
    "파편": "debris",
    "잔해": "rubble",
    "부서": "break",
    "미사일": "missile",
    "로켓": "rocket",
    "트레일": "trail",
    "궤적": "trail",
    "꼬리": "tail",
    "반딧불": "firefly",
    "빛": "light",
    "별": "star",
    "별똥별": "shooting_star",
    "유성": "meteor",
    "홀로그램": "hologram",
    "디지털": "digital",
    "음악": "music",
    "비트": "beat",
    "오디오": "audio",
    "비": "rain",
    "바람": "wind",
    "소용돌이": "vortex",
    "회오리": "tornado",
    "기운": "energy",
    "에너지": "energy",
    "전투": "combat",
    "공격": "attack",
    "방어": "defense",
    "쉴드": "shield",
    "포탈": "portal",
    "텔레포트": "teleport",
    "소환": "summon",
    "사라지": "vanish",
    "등장": "appear",
    "죽음": "death",
    "부활": "respawn",
    "레벨업": "levelup",
    
    # 효과 유형 - 영어
    "sword": "sword",
    "slash": "slash",
    "weapon": "weapon",
    "gun": "gun",
    "bullet": "bullet",
    "muzzle": "muzzle",
    "shoot": "shoot",
    "explosion": "explosion",
    "explode": "explosion",
    "burst": "burst",
    "hit": "hit",
    "impact": "impact",
    "spark": "spark",
    "fire": "fire",
    "flame": "flame",
    "campfire": "campfire",
    "torch": "torch",
    "fountain": "fountain",
    "water": "water",
    "leaf": "leaf",
    "leaves": "leaf",
    "snow": "snow",
    "snowflake": "snowflake",
    "petal": "petal",
    "cherry": "cherry_blossom",
    "pollen": "pollen",
    "dust": "dust",
    "magic": "magic",
    "aura": "aura",
    "heal": "healing",
    "buff": "buff",
    "smoke": "smoke",
    "fog": "fog",
    "lightning": "lightning",
    "electric": "electric",
    "debris": "debris",
    "rubble": "rubble",
    "missile": "missile",
    "rocket": "rocket",
    "trail": "trail",
    "firefly": "firefly",
    "light": "light",
    "star": "star",
    "meteor": "meteor",
    "hologram": "hologram",
    "digital": "digital",
    "music": "music",
    "beat": "beat",
    "audio": "audio",
    "rain": "rain",
    "wind": "wind",
    "vortex": "vortex",
    "tornado": "tornado",
    "energy": "energy",
    "combat": "combat",
    "attack": "attack",
    "defense": "defense",
    "shield": "shield",
    "portal": "portal",
    "teleport": "teleport",
    "summon": "summon",
    "death": "death",
    
    # 특성/스타일
    "밝은": "bright",
    "어두운": "dark",
    "빠른": "fast",
    "느린": "slow",
    "크게": "large",
    "작게": "small",
    "많이": "many",
    "적게": "few",
    "강한": "strong",
    "약한": "weak",
    "부드러운": "soft",
    "격렬한": "intense",
    "화려한": "flashy",
    "은은한": "subtle",
    "반짝": "sparkle",
    "깜빡": "flicker",
    "회전": "rotate",
    "spiral": "spiral",
    "나선": "spiral",
    
    # 색상
    "빨간": "red",
    "파란": "blue",
    "초록": "green",
    "노란": "yellow",
    "주황": "orange",
    "보라": "purple",
    "흰": "white",
    "검은": "black",
    "금색": "gold",
    "은색": "silver",
    "무지개": "rainbow",
    "red": "red",
    "blue": "blue",
    "green": "green",
    "yellow": "yellow",
    "orange": "orange",
    "purple": "purple",
    "cyan": "cyan",
    "white": "white",
    "black": "black",
    "gold": "gold",
    "silver": "silver",
    "rainbow": "rainbow",
    
    # 환경
    "하늘": "sky",
    "땅": "ground",
    "공중": "air",
    "바닥": "floor",
    "실내": "indoor",
    "실외": "outdoor",
    "밤": "night",
    "낮": "day",
    "숲": "forest",
    "던전": "dungeon",
}


def extract_tags(text: str, include_original: bool = True) -> list[str]:
    """텍스트에서 태그 추출.
    
    Args:
        text: 효과 설명 텍스트
        include_original: 원본 키워드도 태그에 포함할지
    
    Returns:
        추출된 태그 목록 (중복 제거, 정렬됨)
    """
    if not text:
        return []
    
    text_lower = text.lower()
    tags: Set[str] = set()
    
    # 키워드 매칭
    for keyword, tag in TAG_KEYWORDS.items():
        if keyword.lower() in text_lower or keyword in text:
            tags.add(tag)
            if include_original and keyword != tag:
                # 한글 원본 키워드도 추가 (검색 편의)
                if re.search(r"[\uac00-\ud7af]", keyword):
                    tags.add(keyword)
    
    return sorted(tags)


def suggest_tags(text: str, existing_tags: list[str], max_suggestions: int = 5) -> list[str]:
    """추가 태그 제안.
    
    Args:
        text: 효과 설명
        existing_tags: 이미 있는 태그
        max_suggestions: 최대 제안 수
    
    Returns:
        제안 태그 목록
    """
    extracted = extract_tags(text)
    existing_set = set(existing_tags)
    
    suggestions = [tag for tag in extracted if tag not in existing_set]
    return suggestions[:max_suggestions]
