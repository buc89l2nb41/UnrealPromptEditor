# Niagara 템플릿 인덱스

> 언리얼 엔진 5.x Niagara 이미터 템플릿 가이드
> PromptEditor VFX 자동화를 위한 참고 자료

---

## 📋 템플릿 목록

| # | 템플릿명 | 용도 | 문서 |
|---|----------|------|------|
| 1 | **Empty** | 빈 템플릿, 처음부터 직접 구성 | [01_Empty.md](01_Empty.md) |
| 2 | **Simple Sprite Burst** | 한 번에 스프라이트 폭발 | [02_SimpleSpritesBurst.md](02_SimpleSpritesBurst.md) |
| 3 | **Directional Burst** | 특정 방향으로 발사 | [03_DirectionalBurst.md](03_DirectionalBurst.md) |
| 4 | **Fountain** | 분수처럼 위로 솟음 | [04_Fountain.md](04_Fountain.md) |
| 5 | **Omnidirectional Burst** | 전방향 폭발 | [05_OmnidirectionalBurst.md](05_OmnidirectionalBurst.md) |
| 6 | **Hanging Particulates** | 공중에 떠다니는 입자 | [06_HangingParticulates.md](06_HangingParticulates.md) |
| 7 | **Grid** | 그리드 패턴 배열 | [07_Grid.md](07_Grid.md) |
| 8 | **Mesh Particle** | 3D 메시 파티클 | [08_MeshParticle.md](08_MeshParticle.md) |
| 9 | **Sprite Trail / Ribbon** | 트레일/리본 효과 | [09_SpriteTrailRibbon.md](09_SpriteTrailRibbon.md) |
| 10 | **Audio Oscilloscope** | 오디오 반응 시각화 | [10_AudioOscilloscope.md](10_AudioOscilloscope.md) |

---

## 🎯 카테고리별 분류

### 💥 버스트/폭발 계열
- Simple Sprite Burst
- Directional Burst
- Omnidirectional Burst

### 🌊 연속 방출 계열
- Fountain
- Hanging Particulates

### 📐 구조적 배열 계열
- Grid
- Mesh Particle

### 🎗️ 연결형 계열
- Sprite Trail / Ribbon

### 🎵 반응형 계열
- Audio Oscilloscope

---

## 📚 사용 방법

각 템플릿 문서에는 다음 내용이 포함됩니다:

1. **기본 모듈 구성** - 템플릿에 포함된 기본 모듈
2. **주요 파라미터** - 조절 가능한 핵심 값
3. **모듈 조합 효과 5가지** - 다른 모듈과 조합했을 때의 효과
4. **활용 예시 5가지** - 구체적인 VFX 사례
5. **PromptEditor 레시피** - MCP 도구 호출 예시

---

## 🔧 PromptEditor 연동

### 템플릿 기반 시스템 생성

```
create_niagara_system(
  path="/Game/FX/NS_MyEffect",
  template="Fountain"
)
```

### 모듈 추가

```
add_niagara_module(
  system="/Game/FX/NS_MyEffect",
  emitter="Emitter",
  stage="ParticleUpdate",
  module="GravityForce"
)
```

### 파라미터 설정

```
set_niagara_module_input(
  system="/Game/FX/NS_MyEffect",
  emitter="Emitter",
  module="SpawnRate",
  input="SpawnRate",
  value=50
)
```
