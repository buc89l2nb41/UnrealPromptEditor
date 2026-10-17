# Fountain 템플릿

> 위로 솟았다가 중력에 의해 떨어지는 분수 효과

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Update | Emitter State | 루핑 상태 |
| | **Spawn Rate** | 연속 스폰 |
| Particle Spawn | Initialize Particle | 초기화 |
| | Add Velocity | **위쪽 방향 속도** |
| | Shape Location (Point) | 한 점에서 스폰 |
| Particle Update | **Gravity Force** | 중력 (포물선) |
| | Scale Sprite Size | 크기 변화 |
| | Scale Color | 색상 변화 |
| Render | Sprite Renderer | 스프라이트 렌더링 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| **SpawnRate** | 20/s | 초당 스폰 수 |
| **InitialVelocity** | (0,0,500) | 초기 속도 (위쪽) |
| VelocityVariation | ±50 | 속도 랜덤 범위 |
| **Gravity** | -980 | 중력 가속도 |
| Lifetime | 2.0-3.0 | 수명 |
| SpawnLocation | (0,0,0) | 분사점 |

---

## 🔧 모듈 조합 효과 5가지

### 1. Fountain + Drag + ScaleSize → 물 분수
```
추가/수정: Drag Force, Scale Size (작아짐)
결과: 자연스러운 물방울 분수
용도: 분수대, 폭포, 호스
파라미터:
  - Drag: 0.5
  - SizeScale: 1.0 → 0.3
  - Color: LightBlue (반투명)
```

### 2. Fountain + CurlNoise + SubUV → 화염 기둥
```
추가 모듈: Curl Noise Force, SubUV Animation
결과: 흔들리며 올라가는 불꽃
용도: 모닥불, 횃불, 화염 기둥
파라미터:
  - CurlNoise: 100
  - Gravity: -200 (약한 중력)
  - Color: Yellow → Orange → Red
```

### 3. Fountain + VelocityFromPoint + Collision → 스파크 샤워
```
추가 모듈: Add Velocity From Point, Collision
결과: 부채꼴로 퍼지며 바닥에 튕기는 스파크
용도: 용접 불꽃, 불꽃놀이, 연삭
파라미터:
  - PointVelocity: radial 200
  - InitialVelocityZ: 300
  - Restitution: 0.4
```

### 4. Fountain + WindForce + SpriteRotation → 꽃가루 분사
```
추가 모듈: Wind Force, Sprite Rotation Rate
결과: 바람에 흩날리는 꽃가루
용도: 꽃가루, 씨앗, 포자
파라미터:
  - Wind: (100, 50, 0)
  - RotationRate: 90°/s
  - Gravity: -100
```

### 5. Fountain + PointAttractor + SizeByLife → 영혼 상승
```
추가 모듈: Point Attractor (위쪽), Size By Lifetime
결과: 위로 빨려 올라가며 사라지는 영혼
용도: 영혼, 에너지, 힐링
파라미터:
  - AttractorPosition: (0,0,500)
  - Gravity: 0
  - SizeScale: 0.5 → 1.5 → 0
```

---

## 🎨 활용 예시 5가지

### 1. ⛲ 물 분수 (Water Fountain)
```json
{
  "name": "FX_WaterFountain",
  "base": "Fountain",
  "modules": {
    "modify": {
      "SpawnRate": 100,
      "InitialVelocity": "(0, 0, 800)",
      "VelocityVariation": 50,
      "Gravity": -980,
      "Lifetime": "2.0-3.0"
    },
    "add": ["Drag", "Collision"]
  },
  "sprite": "T_WaterDrop",
  "color": "LightBlue (0.7 Alpha)",
  "collision": { "Restitution": 0.2, "SpawnSplash": true }
}
```

### 2. 🔥 캠프파이어 (Campfire)
```json
{
  "name": "FX_Campfire",
  "base": "Fountain",
  "modules": {
    "modify": {
      "SpawnRate": 30,
      "InitialVelocity": "(0, 0, 200)",
      "Gravity": -50,
      "Lifetime": "0.5-1.5"
    },
    "add": ["CurlNoiseForce", "SubUVAnimation", "LightRenderer"]
  },
  "sprite": "T_Fire_4x4",
  "color": "White → Yellow → Orange → Red → Black",
  "light": { "Intensity": 2000, "Color": "Orange" }
}
```

### 3. ⚡ 용접 스파크 (Welding Sparks)
```json
{
  "name": "FX_WeldingSparks",
  "base": "Fountain",
  "modules": {
    "modify": {
      "SpawnRate": 200,
      "InitialVelocity": "(0, 0, 400)",
      "VelocityVariation": 200,
      "Gravity": -980,
      "Lifetime": "0.5-1.0"
    },
    "add": ["VelocityFromPoint", "Collision", "SizeByVelocity"]
  },
  "sprite": "T_Spark",
  "color": "White → Orange",
  "additive": true
}
```

### 4. 🌸 벚꽃 나무 (Cherry Blossom)
```json
{
  "name": "FX_CherryBlossom",
  "base": "Fountain",
  "modules": {
    "modify": {
      "SpawnRate": 5,
      "InitialVelocity": "(0, 0, 50)",
      "Gravity": -50,
      "Lifetime": "5.0-10.0"
    },
    "add": ["WindForce", "SpriteRotation", "CurlNoiseForce"]
  },
  "sprite": "T_Petal",
  "color": "Pink → White",
  "wind": "(100, 50, 0)"
}
```

### 5. 💀 영혼 상승 (Soul Rising)
```json
{
  "name": "FX_SoulRising",
  "base": "Fountain",
  "modules": {
    "modify": {
      "SpawnRate": 10,
      "InitialVelocity": "(0, 0, 100)",
      "Gravity": 0,
      "Lifetime": "3.0-5.0"
    },
    "add": ["PointAttractor", "ScaleSize_GrowFade", "CurlNoiseForce"]
  },
  "sprite": "T_Ghost",
  "color": "LightBlue → White → Transparent",
  "attractor": { "Position": "(0,0,500)", "Strength": 50 }
}
```

---

## 📝 EditorPrompt 레시피

### 분수 효과 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_Fountain",
  template="Fountain"
)

# 2. 스폰 레이트 설정
set_niagara_module_input(
  system="/Game/FX/NS_Fountain",
  module="SpawnRate",
  input="SpawnRate",
  value=50
)

# 3. 초기 속도 설정 (위쪽)
set_niagara_module_input(
  system="/Game/FX/NS_Fountain",
  module="AddVelocity",
  input="Velocity",
  value="(0, 0, 600)"
)

# 4. 중력 설정
set_niagara_module_input(
  system="/Game/FX/NS_Fountain",
  module="GravityForce",
  input="Gravity",
  value=-980
)
```

---

## 📐 물리 공식

### 분수 최대 높이

```
H_max = v₀² / (2g)

예: v₀ = 500 cm/s, g = 980 cm/s²
H_max = 500² / (2 × 980) = 127.5 cm
```

### 체공 시간

```
T_total = 2 × v₀ / g

예: v₀ = 500 cm/s, g = 980 cm/s²
T_total = 2 × 500 / 980 = 1.02초
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 물 분수/폭포 | ⭐⭐⭐⭐⭐ |
| 화염 기둥 | ⭐⭐⭐⭐⭐ |
| 스파크 샤워 | ⭐⭐⭐⭐ |
| 한 번 터지는 효과 | ⭐⭐ (SimpleBurst 추천) |
| 떠다니는 효과 | ⭐⭐ (HangingParticulates 추천) |
