# Editor Prompt — 사이드

언리얼 **에디터를 켠 채로** 프롬프트를 치면, Cursor API가 **MCP**로 에디터 도구를 호출한다.  
출시·스토어 아님. 클립: 패널에 한 줄 → 뷰포트에 액터. 모델은 패널 드롭다운. Cursor 앱 채팅으로 조종하지 않음.

특정 게임에 붙이지 않는다. 기존 게임 모듈·맵·온라인 서비스 코드를 읽거나 고치지 않는다.

| | 경로 |
|--|------|
| 문서 | `D:\Cursor\UnrealPrompt\PromptEditor\` |
| 프로젝트 | `D:\Cursor\UnrealPrompt\PromptEditor\PromptEditor.uproject` |
| 플러그인 | `PromptEditor/Plugins/PromptEditor/` |
| 사이드카 | `PromptEditor/sidecar/` (Cursor SDK). 키는 `sidecar/.env` |
| MCP | 에디터 켠 뒤 loopback (기본 `18765`, 점유 시 다음 포트) |
| 엔진 | UE **5.4** — `D:\Program Files\Epic Games\UE_5.4` |

채팅에서 **에디터 프롬프트 Agent** / **Editor Prompt Agent**로 부르면 여기다.

읽는 순서: 이 파일 → [`work/README.md`](work/README.md) → [`work/ROADMAP.md`](work/ROADMAP.md) → 가리키는 **작업 한 장**.

**시작 규칙:** 파악만 하고 “해” 전에는 코드·플러그인·ini·키를 만들지 않는다.
