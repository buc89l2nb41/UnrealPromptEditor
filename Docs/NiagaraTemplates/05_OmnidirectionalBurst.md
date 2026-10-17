# Omnidirectional Burst 템플릿

> 중심에서 모든 방향으로 퍼지는 구형 폭발 효과

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Update | Emitter State | Once 모드 |
| | Spawn Burst Instantaneous | 즉시 버스트 |
| Particle Spawn | Initialize Particle | 초기화 |
| | **Add Velocity From Point** | **중심에서 방사형 속도** |
| | Shape Location (Sphere) | 구 표면에서 스폰 |
| Particle Update | Drag | 공기저항 |
| | Scale Sprite Size | 크기 변화 |
| | Scale Color | 페이드 아웃 |
| Render | Sprite Renderer | 스프라이트 렌더링 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| SpawnCount | 50 | 파티클 수 |
| **RadialVelocity** | 300-500 | 방사 속도 |
| SphereRadius | 10 | 스폰 구 반경 |
| Lifetime | 1.0-2.0 | 수명 |
| Drag | 1.0 | 공기저항 |
| Gravity | 0 또는 -980 | 중력 (선택) |

---

## 🔧 모듈 조합 효과 5가지

### 1. Omnidirectional + Gravity + Collision → 폭발 파편
```
추가 모듈: Gravity Force, Collision
결과: 폭발 후 중력에 의해 떨어지는 파편
용도: 폭발, 파괴, 잔해
파라미터:
  - RadialVelocity: 500-800
  - Gravity: -980
  - Restitution: 0.3
```

### 2. Omnidirectional + CurlNoise + Drag → 연기 폭발
```
추가 모듈: Curl Noise Force, Strong Drag
결과: 퍼지면서 불규칙하게 흐트러지는 연기
용도: 연기 폭발, 먼지 구름, 분진
파라미터:
  - CurlNoise: 50
  - Drag: 5.0
  - ScaleSize: 1.0 → 3.0
```

### 3. Omnidirectional + SubUV + Additive → 마법 폭발
```
추가 모듈: SubUV Animation, Additive Blend
결과: 화려하게 퍼지는 마법 효과
용도: 마법 폭발, 에너지 방출, 스킬
파라미터:
  - SubUV: 4x4 시트
  - Additive: true
  - Color: Gradient (다채로운)
```

### 4. Omnidirectional + PointAttractor + Invert → 임플로전
```
추가 모듈: Point Attractor (역방향)
결과: 퍼졌다가 다시 빨려들어오는 효과
용도: 블랙홀, 흡수, 역폭발
파라미터:
  - InitialVelocity: 300 (outward)
  - AttractorDelay: 0.5s
  - AttractorStrength: 500 (inward)
```

### 5. Omnidirectional + Mesh + RotateRandom → 3D 파편 폭발
```
추가 모듈: Mesh Renderer, Random Rotation
결과: 회전하며 날아가는 3D 조각
용도: 건물 파괴, 차량 폭발, 로봇 분해
파라미터:
  - Mesh: SM_Debris variants
  - RotationRate: Random
  - Gravity: -980
```

---

## 🎨 활용 예시 5가지

### 1. 💥 폭발 (Explosion)
```json
{
  "name": "FX_Explosion",
  "base": "OmnidirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 100,
      "RadialVelocity": "500-1000",
      "Lifetime": "0.5-1.5",
      "Gravity": -500
    },
    "add": ["SubUVAnimation", "ScaleColor_AlphaFade", "LightRenderer"]
  },
  "sprite": "T_Explosion_4x4",
  "color": "White → Yellow → Orange → Red → Black",
  "light": { "Intensity": 5000, "Decay": "Fast" }
}
```

### 2. 💨 먼지 구름 (Dust Cloud)
```json
{
  "name": "FX_DustCloud",
  "base": "OmnidirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 50,
      "RadialVelocity": "100-200",
      "Lifetime": "2.0-4.0",
      "Drag": 3.0
    },
    "add": ["CurlNoiseForce", "ScaleSize_Grow"]
  },
  "sprite": "T_Smoke",
  "color": "Brown (0.5 Alpha) → Transparent",
  "size": "10 → 50"
}
```

### 3. ✨ 마법 버스트 (Magic Burst)
```json
{
  "name": "FX_MagicBurst",
  "base": "OmnidirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 80,
      "RadialVelocity": "200-400",
      "Gravity": 0,
      "Drag": 2.0
    },
    "add": ["CurlNoiseForce", "SubUVAnimation"]
  },
  "sprite": "T_MagicStar",
  "color": "Cyan → Purple → Blue",
  "additive": true
}
```

### 4. 🕳️ 임플로전 (Implosion)
```json
{
  "name": "FX_Implosion",
  "base": "OmnidirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 60,
      "RadialVelocity": 300,
      "Gravity": 0
    },
    "add": ["PointAttractor", "ScaleSize_Shrink"]
  },
  "attractor": {
    "Delay": 0.3,
    "Strength": 800,
    "KillRadius": 5
  },
  "color": "Purple → White"
}
```

### 5. 🔩 메카 분해 (Mech Disassemble)
```json
{
  "name": "FX_MechExplode",
  "base": "OmnidirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 30,
      "RadialVelocity": "300-600",
      "Gravity": -980
    },
    "add": ["MeshRenderer", "RandomRotation", "Collision"]
  },
  "meshes": ["SM_Gear", "SM_Plate", "SM_Screw"],
  "collision": { "Restitution": 0.4, "Friction": 0.3 }
}
```

---

## 📝 EditorPrompt 레시피

### 전방향 폭발 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_Explosion",
  template="OmnidirectionalBurst"
)

# 2. 파티클 수 설정
set_niagara_module_input(
  system="/Game/FX/NS_Explosion",
  module="SpawnBurstInstantaneous",
  input="SpawnCount",
  value=100
)

# 3. 방사 속도 설정
set_niagara_module_input(
  system="/Game/FX/NS_Explosion",
  module="AddVelocityFromPoint",
  input="VelocityStrength",
  value=800
)

# 4. 중력 추가
add_niagara_module(
  system="/Game/FX/NS_Explosion",
  stage="ParticleUpdate",
  module="GravityForce"
)

# 5. 텍스처 설정
set_niagara_sprite_texture(
  system="/Game/FX/NS_Explosion",
  texture="/Game/Textures/T_Explosion"
)
```

---

## 📐 물리 공식

### 방사형 퍼짐 반경

```
R(t) = v₀ × t - 0.5 × drag × t²

예: v₀ = 500, drag = 2.0, t = 1.0
R(1) = 500 × 1 - 0.5 × 2 × 1² = 499 cm
```

### 최대 도달 거리 (Drag 적용 시)

```
R_max ≈ v₀ / drag (근사)

예: v₀ = 500, drag = 2.0
R_max ≈ 250 cm
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 폭발 효과 | ⭐⭐⭐⭐⭐ |
| 먼지/연기 구름 | ⭐⭐⭐⭐⭐ |
| 마법 방출 | ⭐⭐⭐⭐ |
| 방향성 발사 | ⭐⭐ (Directional 추천) |
| 연속 분출 | ⭐⭐ (Fountain 추천) |
