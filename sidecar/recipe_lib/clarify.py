"""모호한 VFX 요청에 대한 세부 질문 시스템."""

from __future__ import annotations

from typing import Optional

# 효과 유형별 옵션
EFFECT_OPTIONS: dict[str, dict[str, list[dict[str, str]]]] = {
    "fire": {
        "question": "어떤 스타일의 불인가요?",
        "question_en": "What style of fire?",
        "options": [
            {"id": "candle", "label": "촛불 (작고 부드러운)", "template": "Fountain", "params": {"SpawnRate": 10, "Gravity": -50, "CurlNoise": 20}},
            {"id": "campfire", "label": "모닥불 (중간, 흔들림)", "template": "Fountain", "params": {"SpawnRate": 30, "Gravity": -100, "CurlNoise": 80}},
            {"id": "torch", "label": "횃불 (위로 솟는)", "template": "Fountain", "params": {"SpawnRate": 20, "Gravity": -150, "CurlNoise": 50}},
            {"id": "flamethrower", "label": "화염방사기 (방향성)", "template": "DirectionalBurst", "params": {"ConeAngle": 30, "Velocity": 500}},
            {"id": "magic_fire", "label": "마법 불꽃 (판타지)", "template": "Fountain", "params": {"SpawnRate": 40, "Gravity": 0, "CurlNoise": 100}},
        ],
    },
    "explosion": {
        "question": "어떤 종류의 폭발인가요?",
        "question_en": "What type of explosion?",
        "options": [
            {"id": "small", "label": "작은 폭발 (히트 이펙트)", "template": "SimpleSpritesBurst", "params": {"SpawnCount": 30, "Velocity": 300}},
            {"id": "medium", "label": "중간 폭발 (수류탄)", "template": "OmnidirectionalBurst", "params": {"SpawnCount": 80, "Velocity": 600}},
            {"id": "large", "label": "큰 폭발 (차량/건물)", "template": "OmnidirectionalBurst", "params": {"SpawnCount": 150, "Velocity": 1000}},
            {"id": "magic", "label": "마법 폭발 (에너지)", "template": "OmnidirectionalBurst", "params": {"SpawnCount": 60, "Velocity": 400, "Gravity": 0}},
            {"id": "smoke", "label": "연기 폭발", "template": "OmnidirectionalBurst", "params": {"SpawnCount": 40, "Drag": 5, "SizeScale": 3}},
        ],
    },
    "trail": {
        "question": "어떤 종류의 궤적/꼬리인가요?",
        "question_en": "What type of trail?",
        "options": [
            {"id": "sword", "label": "검기 (무기 휘두름)", "template": "SpriteTrail", "params": {"RibbonWidth": 20, "Lifetime": 0.3}},
            {"id": "missile", "label": "미사일/로켓 연기", "template": "SpriteTrail", "params": {"RibbonWidth": 10, "Lifetime": 2.0, "CurlNoise": 30}},
            {"id": "magic", "label": "마법 궤적", "template": "SpriteTrail", "params": {"RibbonWidth": 15, "Lifetime": 0.5, "Emissive": True}},
            {"id": "speed", "label": "속도선/잔상", "template": "SpriteTrail", "params": {"RibbonWidth": 5, "Lifetime": 0.2}},
            {"id": "lightning", "label": "번개/전기", "template": "SpriteTrail", "params": {"RibbonWidth": 3, "NoiseAmplitude": 20}},
        ],
    },
    "particles": {
        "question": "어떤 종류의 파티클인가요?",
        "question_en": "What type of particles?",
        "options": [
            {"id": "dust", "label": "먼지/꽃가루 (떠다니는)", "template": "HangingParticulates", "params": {"CurlNoise": 10, "Drag": 2}},
            {"id": "falling", "label": "낙엽/눈 (떨어지는)", "template": "HangingParticulates", "params": {"Gravity": -150, "CurlNoise": 30}},
            {"id": "magic", "label": "마법 입자 (오라)", "template": "HangingParticulates", "params": {"CurlNoise": 40, "Attractor": True}},
            {"id": "spark", "label": "스파크/불꽃", "template": "SimpleSpritesBurst", "params": {"SpawnCount": 50, "Velocity": 400}},
            {"id": "debris", "label": "파편/잔해", "template": "MeshParticle", "params": {"Gravity": -980, "Collision": True}},
        ],
    },
    "water": {
        "question": "어떤 종류의 물 효과인가요?",
        "question_en": "What type of water effect?",
        "options": [
            {"id": "fountain", "label": "분수", "template": "Fountain", "params": {"SpawnRate": 100, "Gravity": -980}},
            {"id": "splash", "label": "물방울 튀김", "template": "OmnidirectionalBurst", "params": {"SpawnCount": 50, "Gravity": -980}},
            {"id": "rain", "label": "비", "template": "HangingParticulates", "params": {"Gravity": -500, "SpawnRate": 200}},
            {"id": "waterfall", "label": "폭포", "template": "Fountain", "params": {"SpawnRate": 150, "Gravity": -980, "Drag": 0.5}},
            {"id": "ripple", "label": "물결/파문", "template": "Grid", "params": {"WaveAmplitude": 10}},
        ],
    },
}

# 모호한 키워드 감지
AMBIGUOUS_KEYWORDS: dict[str, str] = {
    "불": "fire",
    "화염": "fire",
    "불꽃": "fire",
    "fire": "fire",
    "flame": "fire",
    "폭발": "explosion",
    "터지": "explosion",
    "explosion": "explosion",
    "explode": "explosion",
    "궤적": "trail",
    "트레일": "trail",
    "꼬리": "trail",
    "trail": "trail",
    "파티클": "particles",
    "입자": "particles",
    "particle": "particles",
    "물": "water",
    "분수": "water",
    "water": "water",
    "fountain": "water",
}


def detect_ambiguous_effect(message: str) -> Optional[str]:
    """모호한 효과 요청 감지.
    
    Args:
        message: 사용자 메시지
    
    Returns:
        감지된 효과 카테고리 또는 None
    """
    text = message.lower()
    
    # 이미 구체적인 경우 (옵션 키워드 포함)
    specific_keywords = [
        "촛불", "모닥불", "횃불", "화염방사",
        "검기", "미사일", "번개",
        "먼지", "낙엽", "눈", "스파크",
        "분수", "비", "폭포",
        "candle", "campfire", "torch", "sword", "missile",
    ]
    for kw in specific_keywords:
        if kw in text:
            return None  # 이미 구체적
    
    # 모호한 키워드 감지
    for keyword, category in AMBIGUOUS_KEYWORDS.items():
        if keyword in text:
            return category
    
    return None


def get_clarification_options(category: str, lang: str = "ko") -> Optional[dict]:
    """세부 질문 옵션 반환.
    
    Args:
        category: 효과 카테고리
        lang: 언어 ("ko" 또는 "en")
    
    Returns:
        질문과 옵션 딕셔너리
    """
    if category not in EFFECT_OPTIONS:
        return None
    
    data = EFFECT_OPTIONS[category]
    question_key = "question" if lang == "ko" else "question_en"
    
    return {
        "question": data.get(question_key, data["question"]),
        "options": data["options"],
    }


def format_clarification_prompt(category: str, lang: str = "ko") -> str:
    """세부 질문 프롬프트 포맷.
    
    Args:
        category: 효과 카테고리
        lang: 언어
    
    Returns:
        포맷된 질문 문자열
    """
    data = get_clarification_options(category, lang)
    if not data:
        return ""
    
    lines = [data["question"], ""]
    for i, opt in enumerate(data["options"], 1):
        lines.append(f"{i}. {opt['label']}")
    
    return "\n".join(lines)


def get_option_recipe(category: str, option_id: str) -> Optional[dict]:
    """선택된 옵션의 레시피 반환.
    
    Args:
        category: 효과 카테고리
        option_id: 선택된 옵션 ID
    
    Returns:
        템플릿과 파라미터 딕셔너리
    """
    if category not in EFFECT_OPTIONS:
        return None
    
    for opt in EFFECT_OPTIONS[category]["options"]:
        if opt["id"] == option_id:
            return {
                "template": opt["template"],
                "params": opt.get("params", {}),
            }
    
    return None
