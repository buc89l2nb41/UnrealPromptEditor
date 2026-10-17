# Grid 템플릿

> 격자 패턴으로 배열된 파티클 시스템

---

## 📋 기본 모듈 구성

| 스테이지 | 모듈 | 설명 |
|----------|------|------|
| Emitter Update | Emitter State | 루핑/원스 |
| | Spawn Burst (또는 Rate) | 격자 수만큼 스폰 |
| Particle Spawn | Initialize Particle | 초기화 |
| | **Grid Location 2D/3D** | **격자 위치 계산** |
| | Set Position From Grid | 격자 좌표 적용 |
| Particle Update | Scale Color | 색상 변화 |
| | (선택적) Wave Motion | 물결 운동 |
| Render | Sprite Renderer | 스프라이트 |

---

## 🎛️ 주요 파라미터

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| **GridSizeX** | 10 | X축 격자 수 |
| **GridSizeY** | 10 | Y축 격자 수 |
| **GridSizeZ** | 1 | Z축 격자 수 (3D) |
| **CellSize** | 10 | 셀 간격 |
| SpawnCount | GridX × GridY × GridZ | 총 파티클 수 |
| Lifetime | Infinite 또는 고정 | 지속 시간 |

---

## 🔧 모듈 조합 효과 5가지

### 1. Grid + SineWave + ColorByPosition → 물결 그리드
```
추가 모듈: Sine Wave (Z축), Color by Position
결과: 물결치는 색상 그라데이션 격자
용도: 디지털 아트, UI 배경, 음악 시각화
파라미터:
  - WaveAmplitude: 20
  - WaveFrequency: 2.0
  - ColorGradient: Position-based
```

### 2. Grid + PositionNoise + ScaleByNoise → 유기적 지형
```
추가 모듈: Position Noise, Scale by Noise
결과: 불규칙한 높낮이의 점 구름
용도: 지형 미리보기, 점 구름, 데이터 시각화
파라미터:
  - NoiseFrequency: 0.1
  - NoiseAmplitude: 50
  - SizeVariation: 0.5-2.0
```

### 3. Grid + Attraction + Velocity → 자석 시뮬레이션
```
추가 모듈: Point Attractor, Add Velocity
결과: 격자점이 특정 위치에 끌리는 효과
용도: 자기장 시각화, 인터랙티브 아트
파라미터:
  - AttractorStrength: 100
  - ReturnForce: 50 (원래 위치로)
  - Damping: 0.9
```

### 4. Grid + RandomKill + FadeIn → 디지털 분해
```
추가 모듈: Kill Particles Randomly, Fade In
결과: 격자가 랜덤하게 나타나거나 사라짐
용도: 디지털 효과, 텔레포트, 글리치
파라미터:
  - KillProbability: Time-based
  - FadeInDuration: 0.5
  - Color: Neon
```

### 5. Grid + MeshRenderer + RotateByIndex → 큐브 배열
```
추가 모듈: Mesh Renderer, Rotation by Index
결과: 회전하는 3D 큐브 격자
용도: 모션 그래픽, 건축 시각화, 테크 아트
파라미터:
  - Mesh: SM_Cube
  - RotationOffset: Index × 10°
  - ScaleByIndex: Center → Edge
```

---

## 🎨 활용 예시 5가지

### 1. 🌊 물결 그리드 (Wave Grid)
```json
{
  "name": "FX_WaveGrid",
  "base": "Grid",
  "modules": {
    "modify": {
      "GridSizeX": 20,
      "GridSizeY": 20,
      "CellSize": 15,
      "Lifetime": "Infinite"
    },
    "add": ["SineWaveZ", "ColorByPositionXY"]
  },
  "wave": {
    "Amplitude": 30,
    "Frequency": 1.5,
    "Speed": 2.0
  },
  "color": "Blue → Cyan → White (by height)"
}
```

### 2. 📊 데이터 시각화 (Data Visualization)
```json
{
  "name": "FX_DataViz",
  "base": "Grid",
  "modules": {
    "modify": {
      "GridSizeX": 32,
      "GridSizeY": 32,
      "CellSize": 5
    },
    "add": ["ScaleByParameter", "ColorByParameter"]
  },
  "binding": {
    "HeightData": "External Array",
    "ColorData": "External Array"
  },
  "sprite": "T_Square"
}
```

### 3. 🎮 LED 디스플레이 (LED Display)
```json
{
  "name": "FX_LEDDisplay",
  "base": "Grid",
  "modules": {
    "modify": {
      "GridSizeX": 64,
      "GridSizeY": 32,
      "CellSize": 3
    },
    "add": ["ColorByTextureSample", "EmissiveOutput"]
  },
  "texture": "T_LEDPattern",
  "emissive": { "Intensity": 10 },
  "sprite": "T_Circle"
}
```

### 4. 🔮 홀로그램 표면 (Hologram Surface)
```json
{
  "name": "FX_HologramGrid",
  "base": "Grid",
  "modules": {
    "modify": {
      "GridSizeX": 50,
      "GridSizeY": 50,
      "CellSize": 4
    },
    "add": ["PositionNoise", "ScaleByNoise", "Flicker"]
  },
  "noise": {
    "Frequency": 0.2,
    "Amplitude": 10
  },
  "color": "Cyan (0.6 Alpha)",
  "additive": true
}
```

### 5. 🧱 분해되는 큐브 (Disintegrating Cubes)
```json
{
  "name": "FX_CubeDisintegrate",
  "base": "Grid",
  "modules": {
    "modify": {
      "GridSizeX": 10,
      "GridSizeY": 10,
      "GridSizeZ": 10,
      "CellSize": 8
    },
    "add": ["MeshRenderer", "GravityForce", "DelayedKill"]
  },
  "mesh": "SM_Cube",
  "gravity": -980,
  "killDelay": "Index-based (edge → center)"
}
```

---

## 📝 EditorPrompt 레시피

### 격자 시스템 생성

```python
# 1. 시스템 생성
create_niagara_system(
  path="/Game/FX/NS_Grid",
  template="Grid"
)

# 2. 격자 크기 설정
set_niagara_module_input(
  system="/Game/FX/NS_Grid",
  module="GridLocation2D",
  input="NumCellsX",
  value=20
)

set_niagara_module_input(
  system="/Game/FX/NS_Grid",
  module="GridLocation2D",
  input="NumCellsY",
  value=20
)

# 3. 셀 간격 설정
set_niagara_module_input(
  system="/Game/FX/NS_Grid",
  module="GridLocation2D",
  input="CellSize",
  value=10
)

# 4. 물결 효과 추가
add_niagara_module(
  system="/Game/FX/NS_Grid",
  stage="ParticleUpdate",
  module="SineWaveForce"
)
```

---

## 📐 수학 공식

### 격자 좌표 계산

```
// 2D Grid
Index = ParticleID
X = (Index % GridSizeX) × CellSize - (GridSizeX × CellSize / 2)
Y = (Index / GridSizeX) × CellSize - (GridSizeY × CellSize / 2)

// 3D Grid
X = (Index % GridSizeX) × CellSize
Y = ((Index / GridSizeX) % GridSizeY) × CellSize
Z = (Index / (GridSizeX × GridSizeY)) × CellSize
```

### 물결 함수

```
Z_offset = Amplitude × sin(X × Frequency + Time × Speed)
         + Amplitude × sin(Y × Frequency + Time × Speed)
```

---

## ✅ 사용 시점

| 상황 | 적합도 |
|------|--------|
| 데이터 시각화 | ⭐⭐⭐⭐⭐ |
| 홀로그램/디지털 효과 | ⭐⭐⭐⭐⭐ |
| 건축/구조물 시각화 | ⭐⭐⭐⭐ |
| 물결/파동 표현 | ⭐⭐⭐⭐ |
| 유기적 자연 효과 | ⭐⭐ (HangingParticulates 추천) |
| 폭발/버스트 | ⭐ (OmnidirectionalBurst 추천) |
