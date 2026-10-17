# Directional Burst 템플릿

> 특정 방향으로 집중 발사되는 버스트 효과

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Spawn | Emitter Properties | 이미터 속성 |
| Emitter Update | Emitter State | 상태 관리 |
| | Spawn Burst Instantaneous | 즉시 버스트 |
| Particle Spawn | Initialize Particle | 초기화 |
| | Add Velocity in Cone | **원뿔 형태 방향 속도** |
| Particle Update | Gravity Force | 중력 |
| | Drag | 공기저항 |
| | Scale Sprite Size | 크기 변화 |
| Render | Sprite Renderer | 스프라이트 렌더링 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| SpawnCount | 20 | 버스트 파티클 수 |
| **ConeAngle** | 30° | 원뿔 퍼짐 각도 |
| **ConeDirection** | (0,0,1) | 발사 방향 |
| VelocityStrength | 300-500 | 발사 속도 |
| Lifetime | 1.0-2.0 | 수명 |
| Gravity | -980 | 중력 |
| Drag | 0.5 | 공기저항 |

---

## 🔧 모듈 조합 효과 5가지

### 1. DirectionalBurst + Ribbon + NoGravity → 레이저/빔 트레일
```
추가 모듈: Ribbon Renderer, Gravity=0
결과: 직선으로 발사되며 꼬리가 따라오는 효과
용도: 레이저, 마법 투사체, 추적 미사일
파라미터:
  - ConeAngle: 5° (거의 직선)
  - Velocity: 2000
  - RibbonWidth: 10
```

### 2. DirectionalBurst + Collision + SubEmitter → 총알 충돌
```
추가 모듈: Collision, Generate Death Event, SubEmitter
결과: 벽에 맞으면 스파크 발생
용도: 총알, 화살, 투사체
파라미터:
  - ConeAngle: 2°
  - KillOnCollision: true
  - DeathEvent → SparkBurst
```

### 3. DirectionalBurst + CurlNoise + ScaleColor → 화염 방사기
```
추가 모듈: Curl Noise Force, Scale Color
결과: 불규칙하게 흔들리며 퍼지는 화염
용도: 화염방사기, 용의 숨결, 분사
파라미터:
  - ConeAngle: 20°
  - CurlNoise: 30
  - Color: Orange → Red → Black
```

### 4. DirectionalBurst + PointAttractor + VelocityCurve → 회오리
```
추가 모듈: Vortex Force, Point Attractor
결과: 회전하며 빨려들어가는 효과
용도: 토네이도, 마법 소용돌이, 흡입
파라미터:
  - VortexAxis: (0,0,1)
  - VortexStrength: 200
  - AttractorStrength: 100
```

### 5. DirectionalBurst + MeshRenderer + RotateAroundAxis → 파편 발사
```
추가 모듈: Mesh Renderer, Rotate Around Axis
결과: 회전하며 날아가는 3D 파편
용도: 폭발 파편, 조각난 갑옷, 잔해
파라미터:
  - ConeAngle: 60°
  - RotationRate: 360°/s
  - Mesh: SM_Debris
```

---

## 🎨 활용 예시 5가지

### 1. 🔫 총구 화염 (Muzzle Flash)
```json
{
  "name": "FX_MuzzleFlash",
  "base": "DirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 15,
      "ConeAngle": 25,
      "ConeDirection": "(1,0,0)",
      "Velocity": "400-600",
      "Lifetime": "0.05-0.15",
      "Gravity": 0
    }
  },
  "sprite": "T_Flash",
  "color": "Yellow → Orange",
  "additive": true
}
```

### 2. 🌊 워터 스프레이 (Water Spray)
```json
{
  "name": "FX_WaterSpray",
  "base": "DirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 100,
      "ConeAngle": 40,
      "Velocity": "200-400",
      "Gravity": -500
    },
    "add": ["Drag", "ScaleSize_Shrink"]
  },
  "sprite": "T_WaterDroplet",
  "color": "LightBlue",
  "translucent": true
}
```

### 3. 🎯 화살 발사 (Arrow Trail)
```json
{
  "name": "FX_ArrowTrail",
  "base": "DirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 1,
      "ConeAngle": 0,
      "Velocity": 3000,
      "Gravity": -100
    },
    "add": ["RibbonRenderer", "Collision"]
  },
  "mesh": "SM_Arrow",
  "ribbonWidth": 5,
  "collision": { "Kill": true, "SpawnEvent": "ImpactSpark" }
}
```

### 4. 🔥 드래곤 브레스 (Dragon Breath)
```json
{
  "name": "FX_DragonBreath",
  "base": "DirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 200,
      "ConeAngle": 30,
      "Velocity": "300-500",
      "Lifetime": "1.0-2.0",
      "Gravity": 50
    },
    "add": ["CurlNoiseForce", "ScaleColor", "SubUVAnimation"]
  },
  "sprite": "T_FireSheet_4x4",
  "color": "White → Orange → Red → Black"
}
```

### 5. ✨ 마법 투사체 (Magic Projectile)
```json
{
  "name": "FX_MagicProjectile",
  "base": "DirectionalBurst",
  "modules": {
    "modify": {
      "SpawnCount": 5,
      "ConeAngle": 10,
      "Velocity": 1000,
      "Gravity": 0
    },
    "add": ["RibbonRenderer", "CurlNoiseForce", "LightRenderer"]
  },
  "sprite": "T_MagicOrb",
  "color": "Cyan → Purple",
  "light": { "Intensity": 1000, "Radius": 50 }
}
```

---

## 📝 PromptEditor 레시피

### 방향성 버스트 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_DirectionalShot",
  template="DirectionalBurst"
)

# 2. 발사 방향 설정 (앞 방향)
set_niagara_module_input(
  system="/Game/FX/NS_DirectionalShot",
  module="AddVelocityInCone",
  input="ConeAxis",
  value="(1, 0, 0)"
)

# 3. 퍼짐 각도 설정
set_niagara_module_input(
  system="/Game/FX/NS_DirectionalShot",
  module="AddVelocityInCone",
  input="ConeAngle",
  value=15
)

# 4. 속도 설정
set_niagara_module_input(
  system="/Game/FX/NS_DirectionalShot",
  module="AddVelocityInCone",
  input="VelocityStrength",
  value=800
)
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 총알/화살 발사 | ⭐⭐⭐⭐⭐ |
| 화염 분사 | ⭐⭐⭐⭐⭐ |
| 물 스프레이 | ⭐⭐⭐⭐ |
| 전방향 폭발 | ⭐⭐ (Omnidirectional 추천) |
| 떠다니는 효과 | ⭐ (HangingParticulates 추천) |
