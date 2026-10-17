"""복잡한 VFX를 레이어로 분해하는 시스템."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

@dataclass
class VFXLayer:
    """VFX 레이어 정의."""
    name: str                        # 레이어 이름 (예: "Core Flash")
    template: str                    # 기반 템플릿
    add_modules: list[str] = field(default_factory=list)
    params: dict[str, Any] = field(default_factory=dict)
    order: int = 0                   # 실행 순서 (낮을수록 먼저)
    description: str = ""            # 설명


# 복잡한 이펙트 분해 템플릿
COMPLEX_EFFECT_LAYERS: dict[str, list[VFXLayer]] = {
    "explosion_full": [
        VFXLayer(
            name="Core Flash",
            template="SimpleSpritesBurst",
            params={"SpawnCount": 1, "Size": 200, "Lifetime": 0.1, "Color": "White", "Additive": True},
            order=0,
            description="중심 섬광 (밝은 흰색, 즉시 사라짐)"
        ),
        VFXLayer(
            name="Fire Ball",
            template="OmnidirectionalBurst",
            add_modules=["SubUVAnimation", "ScaleSize"],
            params={"SpawnCount": 30, "Velocity": 300, "Lifetime": 0.5, "Color": "Orange→Red"},
            order=1,
            description="화염구 확산"
        ),
        VFXLayer(
            name="Smoke Cloud",
            template="OmnidirectionalBurst",
            add_modules=["CurlNoiseForce", "Drag", "ScaleSize"],
            params={"SpawnCount": 20, "Velocity": 100, "Lifetime": 2.0, "Drag": 3, "SizeScale": 3, "Color": "Gray"},
            order=2,
            description="연기 구름 (느리게 퍼짐)"
        ),
        VFXLayer(
            name="Debris",
            template="MeshParticle",
            add_modules=["GravityForce", "Collision", "RandomRotation"],
            params={"SpawnCount": 15, "Velocity": 500, "Gravity": -980},
            order=3,
            description="파편 (중력 + 충돌)"
        ),
        VFXLayer(
            name="Sparks",
            template="SimpleSpritesBurst",
            add_modules=["GravityForce"],
            params={"SpawnCount": 50, "Velocity": 600, "Size": 2, "Lifetime": 1.0, "Color": "Yellow"},
            order=4,
            description="스파크 (작고 빠름)"
        ),
    ],
    
    "magic_cast": [
        VFXLayer(
            name="Charge Aura",
            template="HangingParticulates",
            add_modules=["PointAttractor", "ScaleSize"],
            params={"SpawnRate": 30, "CurlNoise": 40, "AttractorStrength": 50},
            order=0,
            description="충전 오라 (모이는 입자)"
        ),
        VFXLayer(
            name="Energy Ring",
            template="Grid",
            add_modules=["SineWave"],
            params={"GridSize": 32, "Radius": 50},
            order=1,
            description="에너지 링 (확장)"
        ),
        VFXLayer(
            name="Core Glow",
            template="SimpleSpritesBurst",
            params={"SpawnCount": 1, "Size": 30, "Lifetime": 0.5, "Color": "Cyan", "Emissive": 10},
            order=2,
            description="중심 빛"
        ),
        VFXLayer(
            name="Burst Particles",
            template="OmnidirectionalBurst",
            params={"SpawnCount": 40, "Velocity": 200, "Color": "Cyan→Purple"},
            order=3,
            description="방출 파티클"
        ),
    ],
    
    "hit_impact": [
        VFXLayer(
            name="Flash",
            template="SimpleSpritesBurst",
            params={"SpawnCount": 1, "Size": 50, "Lifetime": 0.05, "Color": "White", "Additive": True},
            order=0,
            description="순간 섬광"
        ),
        VFXLayer(
            name="Sparks",
            template="DirectionalBurst",
            add_modules=["GravityForce"],
            params={"SpawnCount": 20, "ConeAngle": 60, "Velocity": 400, "Gravity": -500},
            order=1,
            description="스파크 분사"
        ),
        VFXLayer(
            name="Dust",
            template="SimpleSpritesBurst",
            add_modules=["Drag"],
            params={"SpawnCount": 10, "Velocity": 100, "Drag": 3, "Color": "Brown", "Alpha": 0.5},
            order=2,
            description="먼지"
        ),
    ],
    
    "teleport": [
        VFXLayer(
            name="Disappear Particles",
            template="OmnidirectionalBurst",
            add_modules=["PointAttractor"],
            params={"SpawnCount": 50, "Velocity": -200, "AttractorStrength": 300},
            order=0,
            description="사라지는 입자 (안으로 빨림)"
        ),
        VFXLayer(
            name="Flash",
            template="SimpleSpritesBurst",
            params={"SpawnCount": 1, "Size": 100, "Lifetime": 0.1, "Additive": True},
            order=1,
            description="섬광"
        ),
        VFXLayer(
            name="Ring",
            template="Grid",
            add_modules=["ScaleSize"],
            params={"GridSize": 64, "Radius": 30, "SizeScale": "0→100"},
            order=2,
            description="확장 링"
        ),
    ],
    
    "heal": [
        VFXLayer(
            name="Rising Particles",
            template="Fountain",
            params={"SpawnRate": 20, "Velocity": 100, "Gravity": 50, "Color": "Green"},
            order=0,
            description="올라가는 힐링 입자"
        ),
        VFXLayer(
            name="Aura",
            template="HangingParticulates",
            add_modules=["PointAttractor"],
            params={"SpawnRate": 15, "CurlNoise": 20, "Color": "LightGreen"},
            order=1,
            description="감싸는 오라"
        ),
        VFXLayer(
            name="Sparkles",
            template="SimpleSpritesBurst",
            params={"SpawnCount": 30, "Velocity": 50, "Size": 3, "Color": "White", "Additive": True},
            order=2,
            description="반짝임"
        ),
    ],
}


def get_layer_template(effect_type: str) -> list[VFXLayer] | None:
    """복잡한 이펙트의 레이어 템플릿 반환.
    
    Args:
        effect_type: 이펙트 유형 (예: "explosion_full", "magic_cast")
    
    Returns:
        레이어 리스트 또는 None
    """
    return COMPLEX_EFFECT_LAYERS.get(effect_type)


def list_complex_effects() -> list[str]:
    """사용 가능한 복잡한 이펙트 목록."""
    return list(COMPLEX_EFFECT_LAYERS.keys())


def format_layer_guide(effect_type: str) -> str:
    """레이어 가이드 포맷.
    
    Args:
        effect_type: 이펙트 유형
    
    Returns:
        포맷된 가이드 문자열
    """
    layers = get_layer_template(effect_type)
    if not layers:
        available = ", ".join(list_complex_effects())
        return f"Unknown effect type. Available: {available}"
    
    lines = [f"=== {effect_type} Layer Breakdown ===", ""]
    
    for layer in sorted(layers, key=lambda x: x.order):
        lines.append(f"Layer {layer.order + 1}: {layer.name}")
        lines.append(f"  Template: {layer.template}")
        if layer.add_modules:
            lines.append(f"  Add Modules: {', '.join(layer.add_modules)}")
        if layer.params:
            params_str = ", ".join(f"{k}={v}" for k, v in layer.params.items())
            lines.append(f"  Params: {params_str}")
        lines.append(f"  Description: {layer.description}")
        lines.append("")
    
    return "\n".join(lines)
