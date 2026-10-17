# Sprite Trail / Ribbon 템플릿

> 움직임에 따라 연결되는 띠/꼬리 효과

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Update | Emitter State | 루핑 |
| | Spawn Rate | 연속 스폰 |
| Particle Spawn | Initialize Particle | 초기화 |
| | **Initialize Ribbon** | 리본 ID 초기화 |
| | Add Velocity | 이동 방향 |
| Particle Update | Gravity/Forces | 힘 적용 |
| | **Update Ribbon** | 리본 연결 업데이트 |
| | Scale Width | 너비 변화 |
| Render | **Ribbon Renderer** | **리본 렌더링** |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| SpawnRate | 30-100 | 리본 세그먼트 밀도 |
| **RibbonWidth** | 10 | 리본 너비 |
| **RibbonLinkOrder** | Age | 연결 순서 |
| **FacingMode** | Camera | 카메라 향함 |
| UV0Mode | Normalized | UV 매핑 |
| Material | M_Ribbon | 리본 머티리얼 |
| Lifetime | 0.5-2.0 | 꼬리 길이(시간) |

---

## 🔧 모듈 조합 효과 5가지

### 1. Ribbon + CurlNoise + ScaleWidth → 연기 꼬리
```
추가 모듈: Curl Noise Force, Scale Width by Life
결과: 흔들리며 퍼지는 연기 트레일
용도: 연기, 안개 꼬리, 잔상
파라미터:
  - CurlNoise: 30
  - WidthScale: 5 → 30 → 0
  - Color: Gray → Transparent
```

### 2. Ribbon + ColorGradient + Additive → 마법 궤적
```
추가 모듈: Color by Age (Gradient), Additive Blend
결과: 화려한 색상의 마법 궤적
용도: 마법 투사체, 검기, 에너지
파라미터:
  - ColorGradient: Cyan → Purple → Blue
  - Additive: true
  - Width: 15 (일정)
```

### 3. Ribbon + MultiRibbon + TwistUV → 나선 효과
```
추가 모듈: Multiple Ribbons, Twist UV
결과: 꼬인 이중/삼중 나선
용도: DNA, 토네이도, 회오리
파라미터:
  - RibbonCount: 2-3
  - TwistRate: 360°/unit
  - OffsetAngle: 120° (3개일 때)
```

### 4. Ribbon + ParentVelocity + FadeHead → 이동 꼬리
```
추가 모듈: Inherit Velocity from Parent, Fade at Head
결과: 빠르게 이동하는 물체의 잔상
용도: 총알, 미사일, 빠른 캐릭터
파라미터:
  - InheritVelocity: 1.0
  - FadeHead: 0.2
  - Width: Velocity-based
```

### 5. Ribbon + Collision + SpawnOnCollision → 번개
```
추가 모듈: Line Trace Collision, Branch on Collision
결과: 벽에 부딪히면 분기하는 번개
용도: 번개, 전기, 균열
파라미터:
  - CollisionResponse: Branch
  - BranchCount: 2-3
  - Color: White → Cyan (Emissive)
```

---

## 🎨 활용 예시 5가지

### 1. ⚔️ 검기 (Sword Slash)
```json
{
  "name": "FX_SwordSlash",
  "base": "SpriteTrail",
  "modules": {
    "modify": {
      "SpawnRate": 60,
      "RibbonWidth": 20,
      "Lifetime": 0.3
    },
    "add": ["ScaleWidthByAge", "ColorFadeOut"]
  },
  "material": "M_SlashTrail",
  "widthCurve": "0 → 30 → 0",
  "color": "White → Cyan → Transparent",
  "additive": true
}
```

### 2. 🚀 미사일 연기 (Missile Trail)
```json
{
  "name": "FX_MissileTrail",
  "base": "SpriteTrail",
  "modules": {
    "modify": {
      "SpawnRate": 40,
      "RibbonWidth": 5,
      "Lifetime": 2.0
    },
    "add": ["CurlNoiseForce", "ScaleWidthGrow", "Gravity"]
  },
  "material": "M_Smoke",
  "widthCurve": "5 → 40",
  "color": "White → Gray → Transparent",
  "gravity": -50
}
```

### 3. ✨ 마법 오브 궤적 (Magic Orb Trail)
```json
{
  "name": "FX_MagicTrail",
  "base": "SpriteTrail",
  "modules": {
    "modify": {
      "SpawnRate": 50,
      "RibbonWidth": 15,
      "Lifetime": 0.5
    },
    "add": ["ColorByAge", "EmissiveIntensity"]
  },
  "material": "M_MagicRibbon",
  "color": "White → Magenta → Purple → Transparent",
  "emissive": 5,
  "additive": true
}
```

### 4. ⚡ 번개 (Lightning)
```json
{
  "name": "FX_Lightning",
  "base": "SpriteTrail",
  "modules": {
    "modify": {
      "SpawnRate": 100,
      "RibbonWidth": 3,
      "Lifetime": 0.1
    },
    "add": ["PositionNoise", "BranchOnLength", "Flicker"]
  },
  "material": "M_Lightning_Emissive",
  "noise": {
    "Frequency": 50,
    "Amplitude": 20
  },
  "color": "White → Cyan",
  "emissive": 20,
  "branches": { "Probability": 0.3, "Angle": "30-60°" }
}
```

### 5. 🌈 레인보우 트레일 (Rainbow Trail)
```json
{
  "name": "FX_RainbowTrail",
  "base": "SpriteTrail",
  "modules": {
    "modify": {
      "SpawnRate": 30,
      "RibbonWidth": 25,
      "Lifetime": 1.0
    },
    "add": ["MultiRibbon", "ColorByRibbonID"]
  },
  "ribbonCount": 7,
  "colors": ["Red", "Orange", "Yellow", "Green", "Blue", "Indigo", "Violet"],
  "spacing": 3
}
```

---

## 📝 PromptEditor 레시피

### 리본 트레일 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_Trail",
  template="SpriteTrail"
)

# 2. 스폰 레이트 설정 (밀도)
set_niagara_module_input(
  system="/Game/FX/NS_Trail",
  module="SpawnRate",
  input="SpawnRate",
  value=50
)

# 3. 리본 너비 설정
set_niagara_module_input(
  system="/Game/FX/NS_Trail",
  module="InitializeRibbon",
  input="RibbonWidth",
  value=20
)

# 4. 너비 커브 추가 (끝으로 갈수록 가늘어짐)
add_niagara_module(
  system="/Game/FX/NS_Trail",
  stage="ParticleUpdate",
  module="ScaleRibbonWidth"
)

# 5. 머티리얼 설정
set_niagara_ribbon_material(
  system="/Game/FX/NS_Trail",
  material="/Game/Materials/M_TrailGlow"
)
```

---

## 📐 UV 매핑 옵션

### UV0 모드

| 모드 | 설명 | 용도 |
|------|------|------|
| **Normalized** | 0-1로 정규화 | 일반적인 텍스처 |
| **DistanceFromStart** | 시작점 거리 | 반복 패턴 |
| **Age** | 나이 기반 | 시간 애니메이션 |

### UV 타일링

```
// 머티리얼에서
UV.x = UV.x × TilingX  // 길이 방향 반복
UV.y = UV.y × TilingY  // 너비 방향 반복
```

---

## 📊 성능 팁

### 최적화

| 항목 | 권장 |
|------|------|
| SpawnRate | 30-60 (품질/성능 균형) |
| MaxParticles | 100-500 |
| TessellationMode | Disabled (모바일) |
| 오버드로 | 반투명 리본 겹침 주의 |

### 페이딩 전략

```
1. 꼬리 끝 페이드: Alpha = Age / Lifetime
2. 거리 페이드: Alpha = 1 - (Distance / MaxDistance)
3. 속도 페이드: Alpha = Velocity / MaxVelocity
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 검기/무기 궤적 | ⭐⭐⭐⭐⭐ |
| 미사일/총알 꼬리 | ⭐⭐⭐⭐⭐ |
| 마법 투사체 | ⭐⭐⭐⭐⭐ |
| 번개/전기 | ⭐⭐⭐⭐ |
| 연기/안개 | ⭐⭐⭐ |
| 폭발 효과 | ⭐⭐ (Burst 추천) |
| 떠다니는 입자 | ⭐ (HangingParticulates 추천) |
