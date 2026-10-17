# Empty 템플릿

> 빈 캔버스에서 시작하는 완전 커스텀 이미터

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Spawn | - | 없음 |
| Emitter Update | Emitter State | 이미터 상태 관리 |
| Particle Spawn | Initialize Particle | 파티클 초기화 |
| Particle Update | - | 없음 |
| Render | Sprite Renderer | 기본 스프라이트 렌더링 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| Lifetime | 1.0 | 파티클 수명 |
| Color | White | 파티클 색상 |
| Sprite Size | 10 | 스프라이트 크기 |
| Position | (0,0,0) | 스폰 위치 |

---

## 🔧 모듈 조합 효과 5가지

### 1. Empty + SpawnBurst + Gravity → 기본 낙하 파티클
```
추가 모듈: Spawn Burst Instantaneous, Gravity Force
결과: 한 번에 스폰되어 떨어지는 파티클
용도: 간단한 테스트, 프로토타이핑
```

### 2. Empty + SpawnRate + Velocity → 연속 발사
```
추가 모듈: Spawn Rate, Add Velocity
결과: 지속적으로 특정 방향으로 발사
용도: 연기, 증기, 화염 베이스
```

### 3. Empty + ShapeLocation + ColorByLife → 공간 분포
```
추가 모듈: Shape Location (Sphere), Scale Color
결과: 구 형태로 분포되며 색상 변화
용도: 오라, 쉴드, 에너지 필드
```

### 4. Empty + ScratchPad + Custom HLSL → 프로시저럴 운동
```
추가 모듈: Scratch Pad, Custom HLSL
결과: 완전 커스텀 수학적 운동
용도: 독특한 궤적, 실험적 효과
```

### 5. Empty + EventHandler + SubUV → 이벤트 반응
```
추가 모듈: Event Handler, SubUV Animation
결과: 특정 이벤트에 반응하는 애니메이션
용도: 충돌 반응, 트리거 효과
```

---

## 🎨 활용 예시 5가지

### 1. 미니멀 UI 파티클
```json
{
  "name": "UI_Sparkle",
  "modules": {
    "spawn": ["SpawnBurstInstantaneous"],
    "update": ["ScaleSize", "ScaleColor"],
    "params": {
      "SpawnCount": 5,
      "Lifetime": 0.5,
      "SizeScale": "1.0 → 0.0"
    }
  }
}
```

### 2. 프로그래밍 학습용 파티클
```json
{
  "name": "Learn_Physics",
  "modules": {
    "spawn": ["InitializeParticle"],
    "update": ["GravityForce"],
    "params": {
      "Gravity": -980,
      "InitialVelocity": "(0, 0, 500)"
    }
  }
}
```

### 3. 디버그 시각화
```json
{
  "name": "Debug_Visualizer",
  "modules": {
    "spawn": ["ShapeLocation_Box"],
    "update": [],
    "params": {
      "BoxSize": "(100, 100, 100)",
      "Color": "Red",
      "Lifetime": 10
    }
  }
}
```

### 4. 단일 스프라이트 배치
```json
{
  "name": "Single_Sprite",
  "modules": {
    "spawn": ["SpawnBurstInstantaneous"],
    "update": ["FacingMode_Camera"],
    "params": {
      "SpawnCount": 1,
      "Lifetime": 999,
      "LoopBehavior": "Once"
    }
  }
}
```

### 5. 커스텀 수학 함수 테스트
```json
{
  "name": "Math_Test",
  "modules": {
    "spawn": ["SpawnRate"],
    "update": ["ScratchPad_Sine"],
    "params": {
      "SpawnRate": 1,
      "CustomExpression": "sin(Age * 5) * 100"
    }
  }
}
```

---

## 📝 EditorPrompt 레시피

### 빈 템플릿에서 시작

```python
# 1. 빈 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_Custom",
  template="Empty"
)

# 2. 스폰 모듈 추가
add_niagara_module(
  system="/Game/FX/NS_Custom",
  emitter="Emitter",
  stage="ParticleSpawn",
  module="SpawnBurstInstantaneous"
)

# 3. 파라미터 설정
set_niagara_module_input(
  system="/Game/FX/NS_Custom",
  emitter="Emitter",
  module="SpawnBurstInstantaneous",
  input="SpawnCount",
  value=10
)
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 완전 커스텀이 필요할 때 | ⭐⭐⭐⭐⭐ |
| 빠른 프로토타이핑 | ⭐⭐ |
| 학습/실험 목적 | ⭐⭐⭐⭐⭐ |
| 복잡한 효과 | ⭐⭐⭐ |
| 간단한 효과 | ⭐⭐ (다른 템플릿이 더 빠름) |
