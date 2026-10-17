# HangingParticulates + Gravity + Collision 분석

> PromptEditor 업그레이드 참고 자료 - 나뭇잎/눈 떨어지는 효과 분석

## 📋 배경

PromptEditor에서 나뭇잎 파티클(`NS_Leaf`)을 만들 때, `HangingParticulates` 템플릿에 Gravity와 Collision 모듈만 추가한 것이 직접 처음부터 만든 것보다 더 자연스러웠던 이유를 분석합니다.

---

## 🔬 HangingParticulates 템플릿의 핵심 구조

| 모듈 | 역할 | 기본값 |
|------|------|--------|
| **Curl Noise Force** | 불규칙한 3D 소용돌이 힘 → 파티클이 공중에서 흔들림 | Strength: 10-50 |
| **Drag Force** | 공기저항 → 속도가 빨라질수록 감속력 증가 | Drag: 1.0-5.0 |
| **Wind Force** | 일정 방향 바람 → 옆으로 밀림 | 선택적 |
| **Shape Location** | 큰 구/원기둥에서 스폰 → 공간에 분포 | Radius: 50-200 |
| **Sprite Renderer** | 작은 점/원 스프라이트 | Size: 1-5 |

**원래 목적**: 먼지, 꽃가루, 미세입자가 **공중에 떠다니는** 효과

---

## 🍂 Gravity 모듈 추가 시 변화

### 힘의 합성

```
F_net = F_gravity + F_curl_noise + F_drag + F_wind
      = m·g (아래) + Noise(불규칙) + -k·v (감속) + Wind(옆)
```

### 핵심 변화

1. **파티클이 아래로 끌려감** (중력)
2. **Curl Noise가 좌우로 흔들어줌** (불규칙 운동 유지)
3. **Drag Force가 낙하 속도를 제한** → Terminal Velocity (종단속도)

**결과**: 직선 낙하가 아닌 **"흔들리며 천천히 떨어지는"** 패턴

---

## 🌿 나뭇잎 낙하의 물리학

### 실제 나뭇잎 낙하 특성

| 현상 | 설명 | Niagara 대응 |
|------|------|--------------|
| **Flutter (펄럭임)** | 공기저항 + 불균일한 면적 → 좌우 흔들림 | Curl Noise Force |
| **Tumble (구르기)** | 무게중심 이동 → 회전 | Sprite Rotation |
| **Pendulum Swing** | 진자 운동처럼 좌우로 swing | Curl Noise + Drag |

### Curl Noise 벡터장 시각화

```
Curl Noise 벡터장:
        ↗ ↘ ← → ↙ ↖
         ↓ ↑ ← → ↓
        → ← ↗ ↘ → 
```

- 수평 성분이 수직 낙하에 간섭
- 시간에 따라 불규칙하게 변화
- **결과**: 나뭇잎이 좌우로 흔들리며 떨어지는 궤적

---

## ❄️ 눈송이 낙하 패턴과의 유사성

눈송이도 같은 원리:
- 가벼운 질량 + 큰 표면적 = 높은 공기저항
- Curl Noise의 작은 소용돌이에 반응
- 지그재그로 천천히 내려옴

---

## 🎯 Collision 모듈의 역할

| 효과 | 설명 | 파라미터 |
|------|------|----------|
| **착지** | 땅에 닿으면 멈춤 | Kill on Collision |
| **바운스** | 튕김 효과 | Restitution > 0 |
| **마찰** | 미끄러지다 멈춤 | Friction |
| **쌓임** | 파티클이 누적 | Lifetime 증가 |

---

## 📊 물리 수식

### 종단속도 (Terminal Velocity)

```
v_terminal = √(2·m·g / (ρ·A·C_d))

변수:
- m: 질량 (Niagara: Mass)
- g: 중력가속도 (Niagara: Gravity Force 크기)
- ρ: 공기밀도 (암묵적)
- A: 표면적 (Niagara: Sprite Size)
- C_d: 항력계수 (Niagara: Drag Force coefficient)
```

### 나뭇잎/눈이 천천히 떨어지는 조건

| 조건 | Niagara 설정 |
|------|--------------|
| 질량(m) 작음 | Mass = 0.1 ~ 1.0 |
| 표면적(A) 큼 | Sprite Size = 5 ~ 20 |
| Drag 계수(C_d) 높음 | Drag = 2.0 ~ 10.0 |
| **결과** | 종단속도가 낮음 |

---

## ⚠️ 직접 만들 때 주의점

### NS_Leaf가 덜 자연스러웠던 이유

| 문제점 | 영향 |
|--------|------|
| **Wind Force = 0** | 수평 흔들림 없음 |
| **Curl Noise 약화** | 불규칙 운동 감소 |
| **초기속도 ↓ 방향으로 강함** | 중력 + 초기속도 = 빠른 직선 낙하 |
| **Drag 부족** | 종단속도 없이 가속 |

### 해결책

템플릿의 바람·양력·Curl Noise를 유지하면서, **약한 중력과 콜리전만 추가**

---

## ✅ PromptEditor 적용 가이드

### 나뭇잎 효과 레시피

```json
{
  "base_template": "HangingParticulates",
  "add_modules": [
    { "name": "GravityForce", "stage": "ParticleUpdate", "params": { "Gravity": -200 } },
    { "name": "Collision", "stage": "ParticleUpdate", "params": { "Restitution": 0.1 } }
  ],
  "modify_params": {
    "SpawnRate": 10,
    "SpriteSize": { "min": 5, "max": 15 },
    "Lifetime": { "min": 5, "max": 10 }
  },
  "keep_defaults": ["CurlNoiseForce", "DragForce", "WindForce"]
}
```

### 눈 효과 레시피

```json
{
  "base_template": "HangingParticulates",
  "add_modules": [
    { "name": "GravityForce", "stage": "ParticleUpdate", "params": { "Gravity": -100 } },
    { "name": "Collision", "stage": "ParticleUpdate", "params": { "Restitution": 0, "KillOnCollision": false } }
  ],
  "modify_params": {
    "SpawnRate": 50,
    "SpriteSize": { "min": 1, "max": 5 },
    "SpriteColor": "White",
    "Drag": 5.0
  }
}
```

---

## 📚 참고

- 대화 소스: PromptEditor 채팅 `c199d769-49f2-02af-90c0-63ace2ec0879`
- Turn 39-45에서 NS_Leaf 작업 및 분석
