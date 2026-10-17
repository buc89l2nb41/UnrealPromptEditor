# Mesh Particle 템플릿

> 스프라이트 대신 3D 메시를 사용하는 파티클 시스템

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Update | Emitter State | 상태 관리 |
| | Spawn Burst/Rate | 스폰 방식 |
| Particle Spawn | Initialize Particle | 초기화 |
| | **Initialize Mesh Orientation** | 메시 회전 초기화 |
| | Shape Location | 스폰 위치 |
| | Add Velocity | 초기 속도 |
| Particle Update | Gravity Force | 중력 |
| | **Update Mesh Orientation** | 회전 업데이트 |
| | Scale Mesh Size | 크기 변화 |
| Render | **Mesh Renderer** | **3D 메시 렌더링** |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| **Mesh** | DefaultCube | 렌더링할 메시 |
| **Material** | DefaultLit | 메시 머티리얼 |
| SpawnCount | 10-50 | 파티클 수 |
| **MeshScale** | (1,1,1) | 메시 크기 |
| **RotationRate** | (0,0,0) | 회전 속도 (°/s) |
| Lifetime | 1.0-5.0 | 수명 |

---

## 🔧 모듈 조합 효과 5가지

### 1. MeshParticle + RandomRotation + Collision → 파편/잔해
```
추가 모듈: Random Rotation Rate, Collision
결과: 회전하며 날아가 바닥에 튕기는 파편
용도: 폭발 잔해, 부서진 조각, 파편
파라미터:
  - RotationRate: Random(0-360, 0-360, 0-360)
  - Collision: Restitution 0.4
  - Meshes: [SM_Rock, SM_Debris, SM_Shard]
```

### 2. MeshParticle + ScaleByLife + FadeOut → 분해 효과
```
추가 모듈: Scale Mesh by Lifetime, Fade Material
결과: 작아지면서 사라지는 분해
용도: 디졸브, 분해, 텔레포트
파라미터:
  - ScaleCurve: 1.0 → 0.0
  - MaterialOpacity: 1.0 → 0.0
  - CurlNoise: 20 (흩어짐)
```

### 3. MeshParticle + AlignToVelocity + Trail → 비행 물체
```
추가 모듈: Align Mesh to Velocity, Ribbon Trail
결과: 진행 방향으로 정렬된 비행체 + 꼬리
용도: 화살, 미사일, 비행 파편
파라미터:
  - AlignAxis: Forward (X)
  - TrailWidth: 5
  - Mesh: SM_Arrow
```

### 4. MeshParticle + Skeletal Animation + VelocityCone → 생물체
```
추가 모듈: Skeletal Mesh, Animation Play
결과: 애니메이션되는 메시 (새, 물고기 등)
용도: 새떼, 물고기 떼, 곤충 무리
파라미터:
  - SkeletalMesh: SK_Bird
  - Animation: Fly_Loop
  - VelocityVariation: Random direction
```

### 5. MeshParticle + MaterialParameter + EmissivePulse → 빛나는 오브젝트
```
추가 모듈: Dynamic Material Parameter, Emissive Pulse
결과: 깜빡이며 빛나는 3D 오브젝트
용도: 보석, 에너지 코어, 마법 구슬
파라미터:
  - EmissiveIntensity: Sine(0-10)
  - EmissiveColor: Animated
  - Mesh: SM_Gem
```

---

## 🎨 활용 예시 5가지

### 1. 💥 폭발 파편 (Explosion Debris)
```json
{
  "name": "FX_ExplosionDebris",
  "base": "MeshParticle",
  "modules": {
    "modify": {
      "SpawnCount": 30,
      "Velocity": "500-1000 (radial)",
      "Gravity": -980,
      "Lifetime": "3.0-5.0"
    },
    "add": ["RandomRotation", "Collision", "ScaleShrink"]
  },
  "meshes": [
    { "mesh": "SM_Rock_Small", "weight": 5 },
    { "mesh": "SM_Metal_Shard", "weight": 3 },
    { "mesh": "SM_Concrete_Chunk", "weight": 2 }
  ],
  "collision": { "Restitution": 0.3, "Friction": 0.5 }
}
```

### 2. 🪨 낙석 (Falling Rocks)
```json
{
  "name": "FX_FallingRocks",
  "base": "MeshParticle",
  "modules": {
    "modify": {
      "SpawnRate": 5,
      "Gravity": -980,
      "MeshScale": "0.5-2.0 (random)"
    },
    "add": ["RandomRotation", "Collision", "DustOnImpact"]
  },
  "mesh": "SM_Rock",
  "collision": {
    "SpawnOnCollision": "FX_DustPuff",
    "Restitution": 0.2
  }
}
```

### 3. 🎯 화살비 (Arrow Rain)
```json
{
  "name": "FX_ArrowRain",
  "base": "MeshParticle",
  "modules": {
    "modify": {
      "SpawnRate": 20,
      "Velocity": "(100, 0, -500)",
      "Gravity": -200
    },
    "add": ["AlignToVelocity", "Collision"]
  },
  "mesh": "SM_Arrow",
  "alignAxis": "Z",
  "collision": { "Kill": true, "StickOnCollision": true }
}
```

### 4. 💎 보석 획득 (Gem Collect)
```json
{
  "name": "FX_GemCollect",
  "base": "MeshParticle",
  "modules": {
    "modify": {
      "SpawnCount": 10,
      "Gravity": 0
    },
    "add": ["PointAttractor", "SpriteRotation", "EmissivePulse"]
  },
  "mesh": "SM_Gem",
  "material": "M_Gem_Emissive",
  "attractor": {
    "Target": "Player",
    "Strength": 500,
    "KillRadius": 20
  }
}
```

### 5. 🐦 새떼 (Bird Flock)
```json
{
  "name": "FX_BirdFlock",
  "base": "MeshParticle",
  "modules": {
    "modify": {
      "SpawnCount": 20,
      "Lifetime": 30
    },
    "add": ["FlockBehavior", "AlignToVelocity", "CurlNoise"]
  },
  "skeletalMesh": "SK_Bird",
  "animation": "Anim_Fly",
  "flock": {
    "Separation": 50,
    "Alignment": 0.5,
    "Cohesion": 0.3
  }
}
```

---

## 📝 EditorPrompt 레시피

### 메시 파티클 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_MeshDebris",
  template="MeshParticle"
)

# 2. 메시 설정
set_niagara_mesh(
  system="/Game/FX/NS_MeshDebris",
  mesh="/Game/Meshes/SM_RockChunk"
)

# 3. 머티리얼 설정
set_niagara_mesh_material(
  system="/Game/FX/NS_MeshDebris",
  material="/Game/Materials/M_Rock"
)

# 4. 회전 추가
add_niagara_module(
  system="/Game/FX/NS_MeshDebris",
  stage="ParticleSpawn",
  module="InitializeMeshRotation"
)

# 5. 랜덤 회전 속도
set_niagara_module_input(
  system="/Game/FX/NS_MeshDebris",
  module="UpdateMeshOrientation",
  input="RotationRate",
  value="Random(0-180, 0-180, 0-180)"
)
```

---

## 📐 성능 고려사항

### 스프라이트 vs 메시

| 항목 | Sprite | Mesh |
|------|--------|------|
| Draw Call | 1 (배칭) | 메시당 1+ |
| 버텍스 | 4 (쿼드) | 메시에 따라 |
| LOD | 없음 | 지원 |
| 그림자 | 비용 낮음 | 비용 높음 |
| **권장 수량** | 수천 개 | 수십~수백 개 |

### 최적화 팁

```
1. LOD 사용: 거리에 따라 메시 단순화
2. 인스턴싱: 같은 메시는 GPU 인스턴싱
3. 컬링: 화면 밖 메시 제외
4. 그림자 끄기: Cast Shadow = false
5. 충돌 단순화: Simple Collision 사용
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 폭발 파편 | ⭐⭐⭐⭐⭐ |
| 떨어지는 물체 | ⭐⭐⭐⭐⭐ |
| 3D 오브젝트 비산 | ⭐⭐⭐⭐⭐ |
| 대량 파티클 | ⭐⭐ (Sprite 추천) |
| 연기/불꽃 | ⭐ (Sprite 추천) |
| 리본/트레일 | ⭐⭐ (Ribbon 추천) |
