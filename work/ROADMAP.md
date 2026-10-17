# 로드맵 — 에디터 프롬프트

에이전트는 이 파일 + **지금 가리키는 작업 한 장**만 읽는다. 구현은 파악 후 “해” 뒤에만. 체크리스트는 **한 칸씩**.

한 줄: **에디터 켠 채 패널에 한 줄 → MCP → 뷰포트.** Cursor 앱 채팅으로 조종하지 않음. Fab에 안 올림.

| 단계 | 이름 | 통과 (한 줄) | 산출 |
|------|------|----------------|------|
| **0** | 자리 | 빈 UE 5.4 + 에디터 전용 플러그인 로드 | 프로젝트/플러그인 |
| **1** | 창 | Window → Editor Prompt. 전송·로그·Send 잠금 | Slate UI |
| **2** | LLM | 키는 `.env`. **모델은 패널 드롭다운.** 한 줄 → 답 → 로그 | Cursor API |
| **3** | MCP | loopback. 포트 회피. `spawn_cube`. Undo | 손 |
| **4** | 세션 | 프로젝트·chat_id별 Agent. cwd=프로젝트 | 맥락 |
| **5** | 저장 | `Saved/EditorPrompt/chats/<id>/turns/` + New Agent | 끄고 켜기 |
| **6** | 한도 | 현재 스레드만. 옛 턴 자르기. (나중 resume/검색) | 컨텍스트 |
| **7** | 손 (본체) | 맵·액터·빛·머티리얼·Niagara·카메라·환경 등 **100칸**. 그래프 아님 | [`todo/roadmap-7.md`](todo/roadmap-7.md) |
| **7V** | 레퍼런스 VFX | 이미지 보기 → 스펙 → 있는 손 → 스크린샷 비교. Grid2D는 본선 아님 | [`todo/vfx-loop.md`](todo/vfx-loop.md) |
| **8** | 배포 | 비개발자가 GitHub에서 받아 설치. **방법은 이 단계 시점의 코드 기준** | Release zip |

**지금 다음:** [`todo/vfx-loop.md`](todo/vfx-loop.md) **V2** (패널에서 레퍼런스 이펙트). 7Q는 보류. Grid2D 컴파일은 본선 아님. 그다음 [`todo/checklist.md`](todo/checklist.md) **I**, **J**.

엔진 5.4. 코드는 `PromptEditor/` 아래만.

**시크릿:** `CURSOR_API_KEY`는 `sidecar/.env`만. 채팅·커밋·로그·turns에 키 없음. 모델 id는 패널 설정.
