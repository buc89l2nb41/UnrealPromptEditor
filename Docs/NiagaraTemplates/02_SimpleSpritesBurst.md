# Simple Sprite Burst 템플릿

> 한 번에 여러 스프라이트를 터뜨리는 기본 버스트 효과

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Spawn | Emitter Properties | 이미터 속성 |
| Emitter Update | Emitter State | 상태 관리 (Once 모드) |
| | Spawn Burst Instantaneous | 즉시 버스트 스폰 |
| Particle Spawn | Initialize Particle | 초기화 |
| | Shape Location (Sphere) | 구 형태 스폰 |
| | Add Velocity | 초기 속도 |
| Particle Update | Gravity Force | 중력 |
| | Scale Sprite Size | 크기 변화 |
| | Scale Color | 색상/투명도 변화 |
| Render | Sprite Renderer | 스프라이트 렌더링 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| SpawnCount | 20 | 버스트 파티클 수 |
| Lifetime | 1.0-2.0 | 수명 (랜덤) |
| SphereRadius | 10 | 스폰 반경 |
| Velocity | 100-300 | 초기 속도 |
| Gravity | -980 | 중력 가속도 |
| StartSize | 10 | 시작 크기 |
| EndSize | 0 | 종료 크기 |

---

## 🔧 모듈 조합 효과 5가지

### 1. SimpleBurst + WindForce + Drag → 꽃잎 흩날림
```
추가 모듈: Wind Force, Drag
결과: 바람에 날리며 천천히 흩어지는 파티클
용도: 벚꽃, 꽃잎, 종이조각
파라미터:
  - Wind: (200, 100, 50)
  - Drag: 3.0
  - Gravity: -200
```

### 2. SimpleBurst + SubUV + ScaleColor → 폭발 스파크
```
추가 모듈: SubUV Animation, Scale Color (Alpha Fade)
결과: 애니메이션되며 사라지는 스파크
용도: 폭발, 불꽃, 충격
파라미터:
  - SubUV: 4x4 스프라이트 시트
  - ColorAlpha: 1.0 → 0.0
  - Velocity: 500-1000
```

### 3. SimpleBurst + Collision + Bounce → 파편 튕김
```
추가 모듈: Collision, (Restitution 설정)
결과: 땅에 부딪혀 튕기는 파편
용도: 잔해, 파편, 부서진 조각
파라미터:
  - Restitution: 0.5
  - Friction: 0.3
  - Gravity: -980
```

### 4. SimpleBurst + CurlNoise + NoGravity → 마법 파티클
```
추가/수정: Curl Noise Force, Gravity = 0
결과: 공중에서 소용돌이치는 마법 효과
용도: 마법 시전, 소환, 힐링
파라미터:
  - CurlNoise Strength: 50
  - Drag: 2.0
  - Color: 그라데이션
```

### 5. SimpleBurst + PointAttractor + SizeByVelocity → 흡수 효과
```
추가 모듈: Point Attractor, Size By Velocity
결과: 중심점으로 빨려들어가는 효과
용도: 아이템 획득, 에너지 흡수, 블랙홀
파라미터:
  - AttractorStrength: 500
  - AttractorPosition: (0,0,0)
  - KillRadius: 10
```

---

## 🎨 활용 예시 5가지

### 1. 💥 히트 이펙트 (Hit Spark)
```json
{
  "name": "FX_HitSpark",
  "base": "SimpleSpritesBurst",
  "modules": {
    "modify": {
      "SpawnCount": 30,
      "Velocity": "400-800",
      "Lifetime": "0.2-0.5",
      "Gravity": -500
    },
    "add": ["ScaleColor_AlphaFade"]
  },
  "sprite": "T_Spark",
  "color": "Orange → Red"
}
```

### 2. 🎉 컨페티 (Confetti)
```json
{
  "name": "FX_Confetti",
  "base": "SimpleSpritesBurst",
  "modules": {
    "modify": {
      "SpawnCount": 100,
      "Gravity": -200,
      "Drag": 5.0
    },
    "add": ["WindForce", "SpriteRotation"]
  },
  "sprite": "T_ConfettiSheet",
  "color": "RandomBright"
}
```

### 3. 💎 아이템 획득 (Item Collect)
```json
{
  "name": "FX_ItemCollect",
  "base": "SimpleSpritesBurst",
  "modules": {
    "modify": {
      "SpawnCount": 20,
      "Gravity": 0
    },
    "add": ["CurlNoiseForce", "ScaleSize_GrowShrink"]
  },
  "sprite": "T_Star",
  "color": "Gold → White"
}
```

### 4. 🔥 폭발 잔해 (Debris)
```json
{
  "name": "FX_Debris",
  "base": "SimpleSpritesBurst",
  "modules": {
    "modify": {
      "SpawnCount": 50,
      "Velocity": "300-600"
    },
    "add": ["Collision", "MeshRenderer"]
  },
  "mesh": "SM_RockChunk",
  "collision": { "Restitution": 0.3, "Friction": 0.5 }
}
```

### 5. ✨ 텔레포트 잔상 (Teleport Residue)
```json
{
  "name": "FX_TeleportBurst",
  "base": "SimpleSpritesBurst",
  "modules": {
    "modify": {
      "SpawnCount": 60,
      "Gravity": 100,
      "ShapeLocation": "Cylinder"
    },
    "add": ["ScaleColor_FadeIn", "CurlNoiseForce"]
  },
  "sprite": "T_Glow",
  "color": "Cyan → Purple"
}
```

---

## 📝 EditorPrompt 레시피

### 기본 버스트 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_Burst",
  template="SimpleSpritesBurst"
)

# 2. 파티클 수 조절
set_niagara_module_input(
  system="/Game/FX/NS_Burst",
  module="SpawnBurstInstantaneous",
  input="SpawnCount",
  value=50
)

# 3. 텍스처 설정
set_niagara_sprite_texture(
  system="/Game/FX/NS_Burst",
  texture="/Game/Textures/T_Spark"
)

# 4. 컴파일
compile_niagara(system="/Game/FX/NS_Burst")
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 히트/충돌 이펙트 | ⭐⭐⭐⭐⭐ |
| 폭발 효과 | ⭐⭐⭐⭐⭐ |
| 아이템 획득 | ⭐⭐⭐⭐ |
| 연속 발사 효과 | ⭐⭐ (Fountain 추천) |
| 떠다니는 효과 | ⭐⭐ (HangingParticulates 추천) |
