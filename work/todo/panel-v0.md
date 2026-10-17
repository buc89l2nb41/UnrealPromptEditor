# 패널 v0 (창 + Cursor + MCP + 큐브)

ROADMAP이 이 파일을 가리킬 때만 연다.

**시작 규칙:** 코드를 고치지 말고, `PromptEditor/`에 플러그인/프로젝트가 있는지와 에디터에서 Window 메뉴를 어떻게 열지 짧게 말한 뒤, 시작 여부를 묻는다. “해” 전에는 구현 없음.

실행은 [`checklist.md`](checklist.md). 이 장은 첫 클립 정의만.

**이 작업이 끝나면:** 에디터를 **켠 채로** 패널에 한 줄을 치면, MCP `spawn_cube`로 **원점에 Cube**가 생긴다. Ctrl+Z로 사라진다. 키가 커밋에 없다. 모델은 패널에서 고른다.

---

## 왜

Cursor에서 `UnrealEditor-Cmd`로 py를 돌리면 에디터를 다시 켠다. 기준은 **같은 창에서 치고 결과가 보이는 것**이다. Cursor 클론·에이전트 전체 이식이 아님. 도구는 JSON을 플러그인이 파싱하지 않고 **MCP**로 연다.

## 고정

- **에디터 모듈만.** Runtime 게임 코드 없음. PIE에서 플레이어를 조종하지 않음.
- **LLM v0:** Cursor API. 키는 `CURSOR_API_KEY` (`sidecar/.env`). 모델은 **패널 드롭다운** (`.env`에 고정하지 않음). OpenAI 아님. Agent API. SDK는 사이드카만.
- **MCP v0:** 에디터가 **loopback HTTP** MCP 서버를 연다. stdio 아님 (에디터는 장수 프로세스). 도구는 **하나:** `spawn_cube` (위치 기본 `0,0,0`, 엔진 BasicShapes/Cube). 트랜잭션 이름 있게 해서 Undo. 실행은 전부 MCP → `GEditor`. JSON 이중 경로 없음.
- **클라이언트는 패널.** Send → 사이드카 Agent에 이 MCP를 붙임. Cursor 앱 `mcp.json`으로 조종하지 않음. 에디터가 꺼지면 MCP는 없음.
- 패널: 입력, 보내기, 스크롤 로그 (유저 줄 / 모델 줄 / MCP 도구 호출). 스트리밍은 Should, 없어도 통과.
- `.umap`을 텍스트로 쓰지 않음. 액터는 에디터 API로 스폰. 저장은 사용자가 Ctrl+S 해도 됨 (v0 필수 아님).

## 할 일

- [ ] **0. 자리.** 빈 UE 5.4 에디터 프로젝트 또는 `Plugins/PromptEditor`. 모듈 `EditorNoCommandlet`. 기존 게임 프로젝트에 플러그인을 넣지 않음. 에디터 재시작 후 플러그인 로드.

- [ ] **1. 창.** `Tools → PromptEditor`. Slate: `SMultiLineEditableTextBox` + Send. 빈 전송 무시. 포커스가 뷰포트를 삼키지 않게.

- [ ] **2. LLM.** 키는 `sidecar/.env`. 모델은 패널 드롭다운. Send 시 Cursor 에이전트. 응답을 로그에. 실패는 로그에만. 키 문자열 출력 금지.

- [ ] **3. MCP.** loopback MCP 서버. `spawn_cube`만. `GEditor` 트랜잭션 안에서 스폰, Undo 스택에 남음. 패널 Send 시 에이전트에 이 서버를 붙임. 도구를 안 쓰면 로그만.

- [ ] **클립:** 에디터 켠 상태 → 패널에서 모델 선택 → “원점에 큐브” → 뷰포트에 큐브 → Ctrl+Z. (영상은 사용자가)

## 하지 말 것

기존 게임 소스·맵·온라인 서비스. Cursor를 에디터에 통째로 넣기. Blueprint 그래프 생성. Landscape/Foliage/Niagara. MCP 도구 수십 개. 비-loopback(원격) MCP. 에디터 꺼진 채 MCP. Fab 업로드. 키 커밋. 게임 Play 자동화. `UnrealEditor-Cmd`로 이 패널을 대체하기 (그건 다른 길). JSON 파싱으로 `spawn_cube`를 두 번째로 구현하기.
