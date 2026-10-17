# VFX 레시피 라이브러리 시스템

> PromptEditor가 학습하는 이펙트 레시피 시스템

---

## 📋 개요

VFX 레시피 라이브러리는 사용자가 만족한 이펙트를 저장하고, 나중에 비슷한 요청이 들어오면 참조할 수 있게 해주는 시스템입니다.

### 핵심 기능

1. **레시피 저장**: 만족한 이펙트를 라이브러리에 저장
2. **레시피 검색**: 태그, 이름, 설명으로 기존 레시피 검색
3. **태그 자동 추출**: 효과 설명에서 키워드 자동 추출
4. **만족 감지**: "좋아", "완벽" 등의 키워드로 저장 시점 감지
5. **세부 질문**: 모호한 요청 시 옵션 제시
6. **레이어 분해**: 복잡한 이펙트를 레이어별로 분해

---

## 📁 저장 구조

```
Saved/PromptEditor/
├── recipes/                    # 레시피 라이브러리
│   ├── index.json              # 전체 인덱스 (빠른 검색용)
│   ├── shooting_star/          
│   │   ├── recipe.json         # 레시피 상세
│   │   └── thumbnail.png       # 미리보기
│   ├── sword_slash/
│   │   ├── recipe.json
│   │   └── thumbnail.png
│   └── ...
├── chats/                      # 대화 기록
├── effects/                    # 이펙트 데이터
└── gen/                        # 생성된 PNG
```

---

## 📄 레시피 스키마 (recipe.json)

```json
{
  "id": "shooting_star",
  "name": "별똥별",
  "description": "하늘에서 떨어지는 별똥별, 밝은 꼬리",
  
  "template": "DirectionalBurst",
  "add_modules": ["RibbonRenderer", "GravityForce"],
  "params": {
    "ConeAngle": 15,
    "Velocity": 1000,
    "Gravity": -200,
    "RibbonWidth": 5
  },
  
  "tags": ["star", "sky", "trail", "fast", "별", "하늘"],
  
  "version": 1,
  "satisfaction": "high",
  "iterations": 2,
  
  "niagara_path": "/Game/FX/NS_ShootingStar",
  "chat_id": "abc123",
  
  "created": "2026-10-11T07:50:00Z",
  "modified": "2026-10-11T08:00:00Z",
  
  "thumbnail": "thumbnail.png"
}
```

---

## 🔧 커스텀 도구

### save_vfx_recipe

이펙트를 레시피로 저장합니다.

```
save_vfx_recipe(
  name="별똥별",
  template="DirectionalBurst",
  add_modules=["RibbonRenderer", "GravityForce"],
  params={"ConeAngle": 15, "Velocity": 1000},
  description="하늘에서 떨어지는 별똥별",
  niagara_path="/Game/FX/NS_ShootingStar",
  thumbnail="path/to/capture.png"
)
```

### search_vfx_recipe

기존 레시피를 검색합니다.

```
search_vfx_recipe(query="별똥별", limit=5)
search_vfx_recipe(query="유성")
search_vfx_recipe(query="trail sky")
```

### get_vfx_template_guide

템플릿 가이드를 조회합니다.

```
get_vfx_template_guide(effect="검기")
get_vfx_template_guide(template="Fountain")
```

---

## 🎯 사용 흐름

### 새 이펙트 생성 시

```
1. 사용자: "별똥별 이펙트 만들어줘"

2. AI: search_vfx_recipe("별똥별")
   → 결과 없음

3. AI: get_vfx_template_guide("별똥별")
   → DirectionalBurst + Ribbon 추천

4. AI: 이펙트 생성 및 조정

5. 사용자: "완벽해!"

6. AI: (만족 감지) "이 레시피를 저장할까요?"

7. 사용자: "응"

8. AI: save_vfx_recipe(...)
   → recipes/shooting_star/recipe.json 저장
```

### 기존 레시피 재사용 시

```
1. 사용자: "유성 이펙트 만들어줘"

2. AI: search_vfx_recipe("유성")
   → "별똥별" 레시피 발견!

3. AI: "이전에 만든 '별똥별' 레시피가 있습니다.
       이걸 기반으로 할까요?"

4. 사용자: "응"

5. AI: 저장된 레시피로 빠르게 생성
```

---

## 🏷️ 태그 자동 추출

### 지원 키워드 (일부)

| 한글 | 영어 | 태그 |
|------|------|------|
| 검기 | sword | sword |
| 폭발 | explosion | explosion |
| 낙엽 | leaf | leaf |
| 눈 | snow | snow |
| 불 | fire | fire |
| 마법 | magic | magic |
| 밝은 | bright | bright |
| 빠른 | fast | fast |

### 자동 추출 예시

```python
extract_tags("검을 휘두를 때 나오는 화려한 검기 이펙트")
# → ['flashy', 'swing', 'sword', '검', '검기', '화려한', '휘두']
```

---

## 😊 만족 감지

### 긍정 키워드

```
한글: 좋아, 완벽, 최고, 훌륭, 저장해, 이걸로, 확정
영어: perfect, excellent, great, love it, save this, done
```

### 부정 키워드

```
한글: 아니, 다시, 수정, 바꿔, 고쳐, 더, 덜
영어: no, change, fix, redo, again, more, less
```

### 감지 예시

```python
is_satisfied("좋아!")        # → (True, "positive:좋아")
is_satisfied("완벽해")       # → (True, "positive:완벽")
is_satisfied("다시 해줘")    # → (False, "negative:다시")
is_satisfied("더 밝게")      # → (False, "negative:더")
```

---

## ❓ 세부 질문 시스템

### 모호한 요청 감지

```python
detect_ambiguous_effect("불 이펙트 만들어줘")
# → "fire"
```

### 옵션 제시

```
어떤 스타일의 불인가요?

1. 촛불 (작고 부드러운)
2. 모닥불 (중간, 흔들림)
3. 횃불 (위로 솟는)
4. 화염방사기 (방향성)
5. 마법 불꽃 (판타지)
```

### 지원 카테고리

- `fire` - 불/화염
- `explosion` - 폭발
- `trail` - 궤적/트레일
- `particles` - 파티클
- `water` - 물

---

## 🎂 멀티 레이어 분해

### 복잡한 이펙트 템플릿

| 이펙트 | 레이어 |
|--------|--------|
| `explosion_full` | Flash + FireBall + Smoke + Debris + Sparks |
| `magic_cast` | ChargeAura + EnergyRing + CoreGlow + BurstParticles |
| `hit_impact` | Flash + Sparks + Dust |
| `teleport` | DisappearParticles + Flash + Ring |
| `heal` | RisingParticles + Aura + Sparkles |

### 레이어 분해 예시

```
=== hit_impact Layer Breakdown ===

Layer 1: Flash
  Template: SimpleSpritesBurst
  Params: SpawnCount=1, Size=50, Lifetime=0.05
  Description: 순간 섬광

Layer 2: Sparks
  Template: DirectionalBurst
  Add Modules: GravityForce
  Params: SpawnCount=20, ConeAngle=60, Velocity=400
  Description: 스파크 분사

Layer 3: Dust
  Template: SimpleSpritesBurst
  Add Modules: Drag
  Params: SpawnCount=10, Velocity=100, Drag=3
  Description: 먼지
```

---

## 🎚️ 파라미터 미세 조정

### 키워드 → 파라미터 매핑

| 키워드 | 조정 대상 |
|--------|-----------|
| 밝게/brighter | ↑ Intensity, Alpha, Emissive |
| 어둡게/darker | ↓ Intensity, Alpha |
| 빠르게/faster | ↑ Velocity, ↓ Lifetime |
| 느리게/slower | ↓ Velocity, ↑ Lifetime, ↑ Drag |
| 많이/more | ↑ SpawnRate, SpawnCount |
| 적게/fewer | ↓ SpawnRate, SpawnCount |
| 크게/bigger | ↑ SpriteSize, Scale |
| 작게/smaller | ↓ SpriteSize, Scale |
| 강하게/stronger | ↑ force values, Gravity |
| 약하게/weaker | ↓ force values |

---

## 📚 관련 문서

- [00_TemplateIndex.md](00_TemplateIndex.md) - 템플릿 인덱스
- [06_HangingParticulates.md](06_HangingParticulates.md) - 떠다니는 입자
- [09_SpriteTrailRibbon.md](09_SpriteTrailRibbon.md) - 트레일/리본
