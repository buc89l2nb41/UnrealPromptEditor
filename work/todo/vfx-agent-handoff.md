# 인수인계 — VFXAgent를 PromptEditor에 넣기

이 장은 지금 작업(`todo/vfx-loop.md` **V2**) 위에 얹는 계약이다. 제품을 바꾸지 않는다. 패널 → MCP → 뷰포트는 그대로다.

**시작 규칙:** 코드를 고치지 말고 아래 1단계만 말한 뒤 시작 여부를 묻는다. “해” 전에는 copy도 하지 않는다. 한 단계가 끝나기 전에 다음 단계로 가지 않는다.

Grid2D, 7Q, Niagara 그래프 손, `todo/checklist.md`의 **I**·**J**는 이 장이 아니다.

---

## 왜 있는가

`docs/VFXAgent`는 사이드카에 이미 있는 레퍼런스 VFX 순서(보기 → 짧은 스펙 → 가장 단순한 손 → `capture_viewport`)를 고도화하려고 만든 계약이다. `server.py`의 V1 프롬프트를 대체하는 새 제품이 아니다.

V1은 이미 `[x]`다. 이 계약은 V2에서 말로만 스펙을 말하지 않고, 효과마다 파일을 남기기 위한 것이다.

## 원본과 제품 복사

- move 하지 않는다. `docs/VFXAgent`는 그 자리에 둔다.
- 제품이 앞으로 읽는 규칙은 `PromptEditor/sidecar/vfx/`다.
- copy 대상은 `schema/`, `instructions/`, `AGENTS.md`뿐이다.
- `docs/VFXAgent/working/`은 copy 하지 않는다. 불 루프 한 건의 기록이고 배포 기본값이 아니다.
- `todo/vfx-loop.md`가 가리키는 `docs/AGENTS.md`는 없다. 실제 문서는 `docs/VFXAgent/AGENTS.md`이고, copy 이후의 제품 경로는 `sidecar/vfx/AGENTS.md`다. 그 한 줄은 copy 단계에서만 고친다.

## 효과마다 쌓이는 기록

사이드카가 알고 있어야 한다. 모델이 디스크를 뒤져 찾게 두지 않는다.

```
Saved/EditorPrompt/
├── chats/<채팅 id>/                 대화. 지금과 같음
└── effects/<효과 id>/
    ├── current_vfx.json             레퍼런스에서 본 명세
    ├── implementation_calibration.json
    ├── implementation_map.json      명세 경로 → Niagara 파라미터
    ├── render_setup.json
    ├── validation_result.json
    └── iteration_history.json
```

모닥불과 비는 폴더가 둘이다. 채팅 `meta.json`에는 활성 효과 id만 둔다. 채팅을 새로 열어도 그 id로 같은 효과 폴더에 돌아간다.

폴더 이름을 `NS_` 에셋 이름과 같게 만들지 않는다. 효과마다 새 Niagara 시스템을 만들 수 있고, 그 콘텐츠 경로는 그 폴더의 `implementation_map.json`에만 적는다. 시스템이 바뀌어도 효과 폴더는 유지한다.

지금 `server.py`의 “`/Game/FX/NS_Live`를 재사용하고, 사용자가 다른 효과를 말하기 전에는 `NS_Live2`를 만들지 않는다”는 V1 문장이다. copy 단계에서는 지우지 않는다.

## 토큰

PromptEditor는 VFX 전용이 아니다. 큐브, 라이트, 맵 턴에는 VFX 규칙을 붙이지 않는다.

규칙을 알려 주는 턴은 둘뿐이다.

- 이번 메시지가 레퍼런스 이미지로 이펙트를 만들거나 고치라는 요청일 때
- 이 채팅 `meta.json`에 활성 효과 id가 있을 때

그때도 스키마 전문은 넣지 않는다. 효과 폴더 경로, `sidecar/vfx/` 경로, “JSON은 검증 도구로만 저장한다”는 몇 줄만 넣는다. `vfx_schema.json`은 검증 도구가 디스크에서 읽는다.

## 단계

| | 단계 | 하는 일 | 하지 않는 일 |
|--|--|--|--|
| 1 | copy | `schema/`, `instructions/`, `AGENTS.md` → `sidecar/vfx/`. `vfx-loop.md`의 규칙 경로 한 줄을 `sidecar/vfx/AGENTS.md`로 고친다 | `working/` copy. `server.py` 수정. 프롬프트에 스키마 삽입 |
| 2 | 효과 폴더 | 사이드카가 활성 효과 id와 `Saved/EditorPrompt/effects/<id>/`를 만들고 기억한다 | 매 채팅에 VFX 설명 추가. `NS_Live` 문장 삭제 |
| 3 | 검증 도구 | JSON 저장 도구가 스키마를 통과한 파일만 쓴다. `generate_png`와 같은 커스텀 도구 | 모델이 검증했다고 말하는 것을 통과로 침 |
| 4 | VFX 턴만 | 위 두 조건일 때만 짧은 경로 지시를 붙인다. 그 다음에만 기존 MCP | 새 Niagara 그래프 손. Grid2D를 본선으로 |
| 5 | V2 | 레퍼런스를 붙이고 패널에서 한 번 돌린다. 뷰포트 전에 그 효과의 `current_vfx.json`이 검증을 통과해 있어야 한다 | checklist **I**, **J** |

1단계가 끝났다고 패널이 이 규칙을 따른다고 보지 않는다. 보장은 3단계의 검증 도구에 있다.

센티미터 anchor가 없어도 첫 스폰은 이미지 비율로 한다. 물리 길이는 사용자가 주기 전까지 `implementation_calibration.json`에 비워 둔다. 관측되지 않은 속도·수명·개수와, 레퍼런스에 없는 연기·불씨·글로우는 만들지 않는다.

## 지금 코드에서 건드릴 자리

- 프롬프트를 매 요청 앞에 붙이는 곳: `sidecar/server.py`의 `_system_prompt`, `_prompt_with_this_chat_only`. 2단계 전에는 고치지 않는다.
- 채팅 메타: `Saved/EditorPrompt/chats/<id>/meta.json`. 효과 id는 여기 추가하고, 효과 파일은 `chats/`와 형제인 `effects/`에 둔다.
- 손: 이미 있는 `create_niagara_system`, 모듈 입력, 스폰, `capture_viewport`. V3 전에는 손을 늘리지 않는다.
