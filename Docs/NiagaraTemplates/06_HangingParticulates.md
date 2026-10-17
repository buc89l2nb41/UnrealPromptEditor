# Hanging Particulates 템플릿

> 공중에 떠다니는 먼지, 꽃가루, 미세입자 효과

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Update | Emitter State | 루핑 |
| | Spawn Rate | 연속 스폰 |
| Particle Spawn | Initialize Particle | 초기화 |
| | **Shape Location (Sphere/Box)** | 넓은 공간에 분포 |
| | Add Velocity (약함) | 미세한 초기 속도 |
| Particle Update | **Curl Noise Force** | **소용돌이 불규칙 운동** |
| | **Drag** | **공기저항** |
| | Wind Force | 바람 (선택) |
| | Scale Color | 페이드 인/아웃 |
| Render | Sprite Renderer | 작은 스프라이트 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| SpawnRate | 5-20/s | 스폰 레이트 |
| **ShapeRadius** | 100-500 | 스폰 공간 크기 |
| **CurlNoiseStrength** | 10-50 | 소용돌이 힘 |
| **Drag** | 1.0-5.0 | 공기저항 |
| Lifetime | 5.0-15.0 | 긴 수명 |
| SpriteSize | 1-5 | 작은 크기 |

---

## 🔧 모듈 조합 효과 5가지

### 1. Hanging + GravityForce + Collision → 🍂 낙엽/눈
```
추가 모듈: Gravity Force, Collision
결과: 흔들리며 천천히 떨어지다 땅에 착지
용도: 낙엽, 눈, 꽃잎
파라미터:
  - Gravity: -100 ~ -300 (약한 중력)
  - CurlNoise: 30
  - Collision: KillOnCollision=false
```
**[상세 분석 → 00_HangingParticulates_Analysis.md](00_HangingParticulates_Analysis.md)**

### 2. Hanging + PointAttractor + ScaleSize → ✨ 마법 오라
```
추가 모듈: Point Attractor, Scale Size by Life
결과: 중심으로 모이며 크기 변화하는 오라
용도: 마법 시전, 힐링 존, 버프
파라미터:
  - AttractorStrength: 20
  - AttractorPosition: Character
  - SizeScale: 0.5 → 1.5 → 0.5
```

### 3. Hanging + WindForce + SpriteRotation → 🌸 꽃가루
```
추가 모듈: Wind Force (강함), Sprite Rotation Rate
결과: 바람에 날리며 회전하는 꽃가루
용도: 꽃가루, 꽃잎, 솜털
파라미터:
  - Wind: (200, 100, 0)
  - RotationRate: 45°/s
  - CurlNoise: 20
```

### 4. Hanging + VelocityFromPlayer + FadeByDistance → 🌫️ 안개
```
추가 모듈: Velocity From Camera, Fade by Distance
결과: 플레이어 주변을 감싸는 안개
용도: 안개, 미스트, 던전 분위기
파라미터:
  - ShapeLocation: Large Box
  - SpawnRate: 50
  - Size: 50-200
  - Color: White (0.1 Alpha)
```

### 5. Hanging + LightRenderer + ColorByAge → 🔥 반딧불이
```
추가 모듈: Light Renderer, Color by Age (깜빡임)
결과: 깜빡이며 떠다니는 빛
용도: 반딧불이, 요정 빛, 마법 입자
파라미터:
  - LightIntensity: 100-500
  - ColorAlpha: Sine curve (깜빡임)
  - Color: Warm Yellow-Green
```

---

## 🎨 활용 예시 5가지

### 1. 🍂 낙엽 (Falling Leaves)
```json
{
  "name": "FX_FallingLeaves",
  "base": "HangingParticulates",
  "modules": {
    "modify": {
      "SpawnRate": 10,
      "ShapeRadius": 200,
      "CurlNoiseStrength": 30,
      "Drag": 3.0,
      "Lifetime": "5.0-10.0"
    },
    "add": ["GravityForce", "Collision", "SpriteRotation"]
  },
  "gravity": -150,
  "sprite": "T_Leaf",
  "color": "Green → Yellow → Orange → Brown"
}
```

### 2. ❄️ 눈 (Snowfall)
```json
{
  "name": "FX_Snowfall",
  "base": "HangingParticulates",
  "modules": {
    "modify": {
      "SpawnRate": 100,
      "ShapeRadius": 500,
      "CurlNoiseStrength": 15,
      "Drag": 5.0
    },
    "add": ["GravityForce", "Collision"]
  },
  "gravity": -100,
  "sprite": "T_Snowflake",
  "size": "2-8",
  "color": "White"
}
```

### 3. 🌟 먼지 (Dust Motes)
```json
{
  "name": "FX_DustMotes",
  "base": "HangingParticulates",
  "modules": {
    "modify": {
      "SpawnRate": 20,
      "ShapeRadius": 300,
      "CurlNoiseStrength": 10,
      "Lifetime": "10.0-20.0"
    }
  },
  "sprite": "T_SoftDot",
  "size": "1-3",
  "color": "White (0.3 Alpha)"
}
```

### 4. ✨ 마법 파티클 (Magic Particles)
```json
{
  "name": "FX_MagicAura",
  "base": "HangingParticulates",
  "modules": {
    "modify": {
      "SpawnRate": 30,
      "ShapeRadius": 50,
      "CurlNoiseStrength": 40
    },
    "add": ["PointAttractor", "ScaleSize", "LightRenderer"]
  },
  "sprite": "T_MagicStar",
  "color": "Cyan → Purple",
  "light": { "Intensity": 200 }
}
```

### 5. 🔥 불씨 (Ember)
```json
{
  "name": "FX_Ember",
  "base": "HangingParticulates",
  "modules": {
    "modify": {
      "SpawnRate": 15,
      "CurlNoiseStrength": 25
    },
    "add": ["GravityForce", "LightRenderer"]
  },
  "gravity": 50,
  "sprite": "T_Spark",
  "color": "Orange → Red → Black",
  "light": { "Intensity": 100, "Color": "Orange" }
}
```

---

## 📝 PromptEditor 레시피

### 떠다니는 입자 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_Dust",
  template="HangingParticulates"
)

# 2. 스폰 영역 설정
set_niagara_module_input(
  system="/Game/FX/NS_Dust",
  module="ShapeLocation",
  input="SphereRadius",
  value=300
)

# 3. Curl Noise 강도 설정
set_niagara_module_input(
  system="/Game/FX/NS_Dust",
  module="CurlNoiseForce",
  input="NoiseStrength",
  value=20
)

# 4. 낙엽 효과를 위한 중력 추가
add_niagara_module(
  system="/Game/FX/NS_Dust",
  stage="ParticleUpdate",
  module="GravityForce"
)

set_niagara_module_input(
  system="/Game/FX/NS_Dust",
  module="GravityForce",
  input="Gravity",
  value=-150
)
```

---

## 📐 물리 공식

### 왜 Curl Noise + Drag = 떠다니는 느낌?

```
가속도 = (CurlNoise - Drag × Velocity) / Mass

평형 상태:
CurlNoise = Drag × Velocity
→ Velocity = CurlNoise / Drag

예: CurlNoise = 20, Drag = 2.0
→ 평균 속도 ≈ 10 cm/s (느린 표류)
```

### 중력 추가 시 (낙엽/눈)

```
Terminal Velocity = sqrt(2 × Mass × |Gravity| / (Drag × Area))

HangingParticulates 기본값 + Gravity -150:
→ 낮은 종단속도 = 천천히 떨어짐
→ CurlNoise가 좌우 흔들림 추가
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 먼지/꽃가루 | ⭐⭐⭐⭐⭐ |
| 낙엽/눈 | ⭐⭐⭐⭐⭐ (+ Gravity) |
| 안개/미스트 | ⭐⭐⭐⭐ |
| 마법 오라 | ⭐⭐⭐⭐ |
| 폭발/버스트 | ⭐ (SimpleBurst 추천) |
| 분수/연속 발사 | ⭐⭐ (Fountain 추천) |
