# Editor Prompt sidecar

언리얼 패널이 Cursor SDK를 부르기 위한 로컬 HTTP. MCP는 에디터 플러그인이 `127.0.0.1:18765`에서 연다.

```powershell
cd sidecar
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy .env.example .env
# .env 에 CURSOR_API_KEY
```

`.env`는 git에 올라가지 않는다. 키와 TRELLIS 주소는 여기에만 둔다. 저장소에는 값 없는 `.env.example`만 있다.

에디터 패널 Send가 사이드카를 자동 기동한다. 수동:

```powershell
.\.venv\Scripts\Activate.ps1
uvicorn server:app --host 127.0.0.1 --port 18766
```

| Method | Path | 용도 |
|--------|------|------|
| GET | `/health` | 상태. 키 문자열 없음 |
| POST | `/prompt` | 한 줄 → Cursor agent + Unreal MCP |

키 발급: [Dashboard → Integrations](https://cursor.com/dashboard/integrations)

## 로컬 TRELLIS (이미지 → 메쉬)

`.env`:

| 변수 | 뜻 |
|------|-----|
| `TRELLIS_URL` | 떠 있는 StableProjectorz API 주소. 기본 `http://127.0.0.1:7960` |
| `TRELLIS_HOME` | (선택) TRELLIS 설치 폴더. `start-trellis.bat`이 이걸 읽어 켠다 |

켜기: `start-trellis.bat`. 첫 기동은 모델 로드로 몇 분 걸린다. 사이드카가 자동으로 켜지는 않는다.

흐름: 채팅에서 모델이 필요하면 그림 → "좋아" → TRELLIS → 뷰포트 미리보기 → "적용해, 이름은 Tree" → `/Game/Meshes/Tree/SM_Tree`.

## 텍스처 (GenerateImage)

풀 카드, 스프라이트, 땅 텍스처는 에이전트가 `request_texture` → `GenerateImage`로 그린다. 코드로 그리지 않는다.

흐름: 그림 → "좋아" → 배경 제거 A/B 비교 → "A" 또는 "B" (여러 장이면 "1A 2B") → `Saved/EditorPrompt/gen/<이름>.png` → 에이전트가 원래 요청을 이어서 처리.

| use | 그리는 배경 | A | B |
|-----|-------------|---|---|
| `cutout` | 마젠타 `#FF00FF` | 색 키 + 번짐 제거 | rembg |
| `glow` | 검정 | 밝기 → 알파 | rembg |
| `tile` | 없음 | 가장자리를 섞어 이음매 제거 (선택 없음) | — |

rembg 모델은 `REMBG_MODEL` (기본 `isnet-general-use`, 약 170MB, 첫 사용 때 `~/.u2net`에 받음). `generate_png`는 셰이더용 데이터(`noise`, `mask`)만 만든다.
