# 계획 — 로컬 TRELLIS로 이미지 → 메쉬

**구현 시작함 (2026-10-08).** M1–M4 코드 들어감. 에디터에서 한 사이클(M5) 확인 전. Tripo API는 이 장이 통과된 뒤에 따로 연다.

Editor Prompt 제품은 그대로다. 패널 → 사이드카 → MCP → 뷰포트. Cursor 앱 채팅으로 조종하지 않음.

구현 위치:

| 곳 | 파일 |
|----|------|
| 사이드카 | `sidecar/mesh_flow.py` (TRELLIS 클라이언트, 단계 말, 그림 결과), `server.py` (`request_mesh` 도구, `_mesh_turn` 게이트) |
| TRELLIS 켜기 | `sidecar/start-trellis.bat` → `start_trellis.py` (`.env`의 `TRELLIS_HOME`/`TRELLIS_URL`) |
| 언리얼 | `EditorPromptMcpMesh.cpp` (`import_static_mesh`, `discard_mesh_preview`) |

확인한 것: 실제 7960에 이 폼 값으로 GLB 3.5MB / 191초(RTX 2060S 8GB, 약 4.6만 삼각형, PBR 텍스처 2장). 같은 GLB를 UE 5.4 Interchange `ImportAssetsAutomated`로 메쉬·머티리얼·텍스처 2장 임포트 성공. Interchange는 파일 이름으로 메쉬 이름을 짓기 때문에 `SM_<이름>.glb` 사본을 임포트한다.

적용 경로는 머티리얼·텍스처가 같이 오므로 `/Game/Meshes/<이름>/SM_<이름>` 폴더 단위다.

---

## 한 줄

그림 생성 → **유저가 눈으로 그림 OK** → TRELLIS로 모델링 → **유저가 모델링 OK** → 그제야 씬에 적용. 어느 확인 전에도 다음 단계로 가지 않는다.

---

## 이 장이 겨냥하는 시도

채팅 `나무가 있고, 그 나무에서 나뭇잎이 떨어지는 이펙트가 필요해`.

지금은 나무 메쉬 손이 없어서 큐브를 나무 자리에 세우고, 그 아래 잎 파티클만 돌렸다. 이 장은 그 **나무 자리**를 실제로 만들 때 쓰인다. 잎 이펙트(Niagara)와 같은 채팅에 있어도 된다. 큐브 스탠드인으로 건너뛰지 않는다.

순서는 나무가 먼저다. 그림·메쉬가 적용된 뒤에 잎 파티클을 맞춘다. 한 Send에서 나무 TRELLIS와 `NS_Leaf`를 같이 돌리지 않는다.

---

## 세 단계 (턴을 끊는다)

Niagara 이름을 묻고 기다리듯이, 아래도 **Send를 끊는다.**

| 단계 | 사이드카가 하는 일 | 유저가 보는 것 | 다음으로 가는 말 | 가지 않는 말 |
|------|-------------------|----------------|------------------|--------------|
| **1. 그림** | 메쉬용 레퍼런스 PNG를 피드에 올림 | 패널 링크(원본) | 좋아 / 이걸로 / ok | TRELLIS, 임포트 |
| **2. 모델링** | TRELLIS `generate_no_preview` → GLB. **미리보기만** 뷰포트에 띄움 | 뷰포트의 미리보기 메쉬 | 적용해 / 이걸로 | `/Game/Meshes/SM_*` 확정 저장을 최종 자리에 |
| **3. 적용** | 이름 붙은 에셋으로 승격, 나무 자리에 둠 | 그게 씬의 나무 | (이후 잎 VFX 등) | |

`meta.json` 상태:

- `pending_mesh_image` — 그림만 나옴. TRELLIS 금지.
- `pending_mesh_apply` — GLB·미리보기 액터 있음. 적용 금지.
- 비움 — 적용됨. 같은 채팅의 다음 메쉬는 새 `mesh_id`.

되돌리기:

- 그림 단계에서 다시 / 첨부 → PNG만 교체. TRELLIS 안 감.
- 모델링 단계에서 다시 → 미리보기 액터 지우고 TRELLIS만 다시. 그림은 유지.
- 모델링에서 그림부터 → 미리보기 지우고 `pending_mesh_image`로.

---

## 미리보기와 적용이 다른 이유

TRELLIS는 비싸고, 한번 임포트한 `/Game/Meshes/SM_Tree`를 바로 씬의 나무로 쓰면 거절이 더럽다.

- **미리보기:** `Saved/EditorPrompt/meshes/<id>/out.glb`를 임시 경로(예: `/Game/Meshes/Preview/SM_pending`)로 가져와 카메라 앞에 스폰. 태그 `EditorPromptPreview`. 유저가 돌아보며 본다. 이게 “모델링을 눈으로 확인”.
- **적용:** 유저가 적합하다고 한 뒤에만 `/Game/Meshes/SM_<이름>`으로 복사·임포트하고, 나무 자리(예전에 큐브가 있던 곳)에 둔다. 미리보기 액터는 지운다.

1차에 미리보기 PNG 턴테이블은 없어도 된다. 이 제품의 판정은 뷰포트다.

---

## 손 세 개

| 누가 | 하는 일 | 하지 않는 일 |
|------|---------|--------------|
| 패널 | 요청, 그림 링크, 좋아/다시/적용해, Stop, 뷰포트 판정 | TRELLIS 기동, TRELLIS 주소 알기 |
| 사이드카 | PNG, 두 게이트, TRELLIS HTTP 클라이언트, GLB를 Saved에 복사 | 가중치 로드, 최종 임포트 |
| 언리얼 MCP | 미리보기 임포트·스폰, 적용 임포트·스폰, 미리보기 삭제 | TRELLIS 프로세스 |

사이드카 안에 `trellis2` / torch를 import하지 않는다. TRELLIS는 **이미 떠 있는 HTTP 서버**다.

---

## TRELLIS 위치 — 하드코딩 없음

이 저장소는 GitHub에 올라가고, 받는 사람마다 TRELLIS 설치 위치·포트가 다르다. 코드·프롬프트·문서 예시에 **특정 드라이브 경로를 넣지 않는다.**

설정은 키와 같은 곳, `sidecar/.env`(git 제외)에 둔다. 저장소에는 값 없는 `sidecar/.env.example`만 올린다.

| 변수 | 필수 | 기본 | 뜻 |
|------|------|------|-----|
| `TRELLIS_URL` | 아니오 | `http://127.0.0.1:7960` | 떠 있는 StableProjectorz API 주소. 이것만 있으면 동작한다 |
| `TRELLIS_HOME` | 아니오 | 비움 | 설치 폴더. 1차에서는 “꺼져 있음” 안내 문구에 실행할 bat 경로를 보여 주는 데만 쓴다 |

- 사이드카는 **주소만** 안다. 경로를 몰라도 단계 2가 돈다.
- `TRELLIS_HOME`이 비어 있으면 안내는 “TRELLIS(StableProjectorz API)를 켜고 `TRELLIS_URL`을 확인해 주세요”까지만.
- `TRELLIS_HOME`이 있으면 안내에 `<TRELLIS_HOME>\run-stableprojectorz\run-stableprojectorz.bat`을 붙인다. 파일이 실제로 있을 때만.
- 플러그인(C++)은 TRELLIS 설정을 갖지 않는다. 사이드카 `/status`의 메시지를 보여 줄 뿐.
- 로그·피드에는 URL은 찍어도 되고, `.env`의 다른 값(키)은 찍지 않는다.
- 자동 기동은 1차에 없음. 사람이 `sidecar/start-trellis.bat`을 켠다.
- TRELLIS 동봉 `run-stableprojectorz.bat`은 설치 폴더를 옮기면 venv가 옛 경로를 가리켜 `torch`를 못 찾는다(이 PC에서 실제로 그랬다). 그래서 저장소 런처가 venv와 포터블 Python 중 torch가 있는 쪽을 골라 `api_spz/main_api.py`를 `TRELLIS_URL` 포트로 띄운다. 런처는 TRELLIS 프로세스에 `CURSOR_API_KEY`를 넘기지 않는다.
- 첫 기동은 모델 로드로 몇 분(이 PC 약 10분). 쉬는 동안 VRAM은 약 0.4GB라 언리얼과 같이 둬도 된다. 생성 중에만 크게 쓴다.

`.env.example` 만듦(빈 `CURSOR_API_KEY`, `TRELLIS_URL`, `TRELLIS_HOME`). `EditorPromptSidecar.cpp`의 “사이드카가 없습니다” 문구는 실제로 찾은 사이드카 경로를 넣도록 고침.

---

## 7960 vs 8080 — 7960으로 간다

같은 설치에 서버가 둘 있다. 둘 다 같은 TRELLIS 파이프라인을 돌리고, 7960은 내부 샘플러 값을 Gradio 앱의 튜닝값과 맞춰 둔다. **VRAM 때문에 둘을 동시에 띄우지 않는다.**

| | 7960 StableProjectorz API (`api_spz/main_api.py`) | 8080 Gradio (`app.py`) |
|--|--|--|
| 사이드카에서 부르기 | multipart POST 한 번 + GET 한 번. 새 의존성 없음 | `gradio_client` 추가, 세션 → 위치 인자 20개 → 추출 세 번 |
| 진행 표시 | `GET /status` (progress, message, busy) → 패널 job events | 없음 |
| 패널 Stop | `POST /interrupt` (취소 시 499) | 생성 도중엔 못 끊음 |
| 바쁨 | `/ping`의 `busy`, 생성 중 503 | 따로 없음 |
| 깨지기 쉬움 | 이름 있는 폼 필드 | 인자 순서가 앱 업데이트에 묶임 |
| 검증 | 아직 안 돌려 봄 | PromptAssetForge가 GLB 다수 생성 |
| 세밀 조정 | steps·ss guidance·텍스처 값만 | 세 단계 샘플러 전부 |

Editor Prompt는 한 번에 한 물체를 유저가 지켜보며 Stop을 누를 수 있는 도구라 진행·취소·바쁨이 있는 7960이 맞다. 세밀 조정이 모자란 건 1차에 문제 되지 않는다.

PromptAssetForge의 8080 경로는 **값 참고용**으로만 쓴다. 해상도 512, 텍스처 1024로 8GB VRAM에서 돌았다는 것.

| 때 | 호출 |
|----|------|
| 켜져 있는지 | `GET /ping` (`busy`면 “다른 생성 중”으로 멈춤) |
| 진행 | `GET /status` → 패널 job events |
| 생성 | `POST /generate_no_preview` (확인된 `ref.png`) |
| 결과 | `GET /download/model` → `out.glb` |
| Stop | `POST /interrupt` |

첫 실험 폼 값: `resolution=512`(API 기본 1024는 8GB에서 위험), `num_inference_steps=12`, `guidance_scale=7.5`, `mesh_simplify=50`(천 단위 → 약 5만 면), `texture_size=1024`, `output_format=glb`. 결과 GLB가 10KB 미만이면 실패로 본다.

TRELLIS가 꺼져 있으면 단계 2를 시작하지 않는다. 위 안내 문구만.

---

## 레퍼런스 그림

메쉬용 한 장이다. 물체 하나, 흰 배경, 그림자 없음. 첨부가 있으면 그게 1단계 PNG다.

**그리는 손은 Pillow가 아니라 cursor-sdk의 `generateImage` 도구다.** 지금 `generate_png`는 다각형 실루엣이라 TRELLIS가 입체를 읽을 정보가 없다.

- 모델은 **패널에서 고른 모델** 그대로 쓴다(지금 Send가 쓰는 `_model()`). Composer 고정이 아니다.
- SDK의 도구 이름 목록에 `generateImage`가 모델 구분 없이 들어 있다. 다만 모델마다 실제로 부를 수 있는지는 아직 안 돌려 봤다. M1에서 확인한다.
- 고른 모델이 도구를 안 부르거나 도구 오류가 나면, 한 번만 `CURSOR_AI_MODEL`(기본 composer-2.5)로 다시 굽고 피드에 “○○ 모델이 이미지를 못 만들어 △△로 그렸습니다”라고 적는다. 몰래 바꾸지 않는다.
- 결과는 `meshes/<id>/ref.png`. 키는 기존 `CURSOR_API_KEY` 그대로.
- 프롬프트 규칙은 PromptAssetForge와 같다: 물체 하나, 흰 배경, 그림자 없음, 도구는 한 번만.

VFX `generate_png`(아틀라스·잎 스프라이트)와 파일을 섞지 않는다. 잎 파티클 시트는 `NS_Leaf`용이고, 나무 정면 컷은 `meshes/<id>/ref.png`다.

---

## 저장

`Saved/EditorPrompt/meshes/<id>/` (Content 아님). VFX `effects/`와 분리.

| 파일 | 내용 |
|------|------|
| `meta.json` | id, 채팅, 이름, 단계, 백엔드=`trellis`, 쓴 폼 값 |
| `ref.png` | 유저가 OK한 그림 |
| `out.glb` | TRELLIS 결과 (적용 전에도 있음) |

`meta.json`에 TRELLIS 주소·설치 경로는 저장하지 않는다. 다른 PC에서 열어도 의미 없는 값이다.

채팅 meta: `mesh_id` + 위 단계 필드. New Agent = 새 메쉬. 1차는 id를 덮지 않는다.

적용 에셋: `/Game/Meshes/SM_<유저 이름>`. 이름은 **적용 직전**에 Niagara처럼 한 번 묻는다. `SM_Live` 금지. 그림·모델링 확인보다 이름을 먼저 묻지 않아도 된다. 미리보기는 이름 없이 된다.

---

## 언리얼 손 (아직 없음)

`import_static_mesh` — 프로젝트 안 GLB만. 미리보기 경로와 최종 경로를 인자로 구분. 그다음 있는 `spawn_actor` · `destroy_actor` · `focus_actor` · `capture_viewport`.

FBX/OBJ는 1차에 없음. GLB+PBR만.

---

## Tripo (이 장 밖 — 분석만)

**단계 1→2→3이 뷰포트에서 한 번 돈 뒤에** 연다. 1차 코드·프롬프트에 Tripo 없음.

### 어디에 끼우나

**단계 2의 백엔드만 바뀐다.** 그림 확인, 미리보기, 이름, 적용, 두 게이트는 그대로다. 사이드카가 `ref.png`를 받아 `out.glb`를 내놓는 함수 하나를 TRELLIS/Tripo 둘로 두면 된다. `meta.json`의 백엔드 필드가 `trellis` | `tripo`.

선택은 `sidecar/.env`의 `MESH_BACKEND`(기본 `trellis`). 나중에 패널에서 고르게 하더라도 키는 사이드카에만.

### API 흐름 (이미지 → 모델)

공식 문서(2026-10 기준)에 두 세대가 같이 있다. 구현 때 하나로 정한다.

| | v2 | v3 |
|--|--|--|
| 기본 주소 | `https://api.tripo3d.ai/v2/openapi` | `https://openapi.tripo3d.ai/v3` |
| 업로드 | `POST /upload/sts` (multipart) → `image_token` | file_token 또는 URL |
| 생성 | `POST /task` `{"type":"image_to_model","file":{...}}` | `POST /generation/image-to-model` |
| 조회 | `GET /task/{id}` | `GET /tasks/{id}` |
| 잔액 | wallet | `GET /account/balance` |

공통:

1. `ref.png` 업로드 (10MB 이하, 256px 이상).
2. 생성 요청 → `task_id`.
3. 2초 간격으로 조회. `status`: `queued`(`queuing_num` 순번) → `running`(`progress`) → `success` / `failed` / `banned` / `expired` / `cancelled` / `unknown`. 진행은 패널 job events로 그대로 보낸다.
4. `success`면 `output`의 모델 URL을 **5분 안에** 받아 `out.glb`로 저장. URL은 로그·피드에 남기지 않는다.

인증은 `Authorization: Bearer <TRIPO_API_KEY>`. 키는 `sidecar/.env`의 `TRIPO_API_KEY`에만 둔다(Tripo 문서도 커밋 금지를 권한다). 같은 키로 만든 작업만 조회된다.

공식 Python SDK(`tripo3d`)가 있지만 호출이 네 개뿐이라 사이드카에서는 HTTP로 직접 부른다. 새 의존성은 넣지 않는다.

### 언리얼에 맞는 값

| 값 | 쓰는 이유 |
|----|-----------|
| `face_limit` | 게임용 면 수 상한. TRELLIS의 `mesh_simplify`와 같은 역할 |
| `pbr=true` (기본) | GLB PBR → Interchange가 머티리얼까지 가져옴 |
| `orientation=align_image` | 그림 정면과 모델 정면을 맞춤. 나무 자리에 놓을 때 회전 덜 함 |
| `auto_size=true` | 미터 단위 실제 크기. 임포트 때 cm로 ×100 하면 큐브 스탠드인 대신 키 맞는 나무 |
| `texture_alignment=original_image` (기본) | 유저가 OK한 그림과 닮게 |
| `model_version` | 기본은 v2.5. `P1-20260311`은 게임용 로우폴리(면 48~20000) |
| `smart_low_poly` / `quad` | 1차 밖. `quad`는 FBX로 바뀌어 GLB 경로가 깨진다 |

### 비용 — 게이트가 하나 더 필요하다

선불 크레딧, $1 = 100 크레딧. 성공했을 때만 차감되고 실패·취소는 돌려준다.

| 작업 | 크레딧 |
|------|--------|
| image_to_model, 텍스처 포함 (v2.5/v3) | 30 (텍스처 품질 standard 기준, detailed +10, extreme +20) |
| 같은 것, P1 | 50 |
| 리깅 / 리타깃 | 25 / 애니메이션당 10 |
| 포맷 변환 | 5 |

그래서 Tripo일 때 단계 1의 확인 문구에 **예상 크레딧과 현재 잔액**을 같이 보여 준다. 유저가 “좋아”라고 하는 게 곧 결제 승인이다. 잔액이 모자라면 단계 2를 시작하지 않는다. 같은 그림으로 “다시”도 다시 과금된다는 걸 문구에 넣는다.

### Stop의 한계

문서에서 작업 취소 엔드포인트는 확인하지 못했다(`cancelled` 상태는 있음). 확인될 때까지 패널 Stop은 **폴링만 멈추고**, Tripo 쪽 작업은 끝까지 돌아 과금될 수 있다고 안내한다. TRELLIS의 `/interrupt`와 다르다.

### 동시성·제한

기본 동시 작업 10(P1은 5). 초과 시 429 / 코드 2000 → `Retry-After` 보고 지수 백오프. 크레딧 소진은 코드 2010. Editor Prompt는 한 번에 하나라 거의 안 걸린다.

### 나중에 쓸 수 있는 것 (1차·2차 밖)

- **`rendered_image`**: Tripo가 주는 모델 미리보기 PNG. 언리얼 임포트 전에 피드에 먼저 띄우면 확인이 한 번 더 싸진다.
- **멀티뷰** (`generate_multiview_image` → `multiview_to_model`): 앞·옆·뒤 네 장으로 더 정확하게. 그림 확인이 네 장이 된다.
- **리깅·애니메이션** (rig-check → rig `spec=mixamo` → retarget): 캐릭터일 때. 스켈레탈 메쉬 임포트 손이 따로 필요하다. 나무에는 필요 없다.
- **Tripo `generate_image`**: 이미지도 Tripo에서 굽는 방법. 지금은 패널 모델로 그리기로 했으니 쓰지 않는다.

### 주의

- 그림이 PC 밖(Tripo 서버)으로 나간다. 첫 Tripo 사용 때 한 번 알린다.
- Tripo는 원격 HTTP API이지 원격 MCP가 아니다. 언리얼을 만지는 손은 여전히 로컬 MCP뿐이다.
- 웹앱 크레딧과 API 크레딧은 따로다. API 대시보드에서 충전해야 한다.

### Tripo 칸 (M5 통과 후)

| | 칸 | 통과 |
|--|--|--|
| [ ] | **T1.** `MESH_BACKEND`, `TRIPO_API_KEY` 읽기, 잔액 조회. 키 없으면 안내만 | 키가 로그·피드에 0번 |
| [ ] | **T2.** 단계 1 확인 문구에 예상 크레딧·잔액 | “좋아” 전 과금 0 |
| [ ] | **T3.** 업로드 → 작업 → 폴링 → 5분 안에 GLB → 기존 미리보기 경로 | 단계 3은 TRELLIS와 같은 코드 |
| [ ] | **T4.** 같은 나무 그림으로 TRELLIS와 Tripo 결과를 뷰포트에서 비교 | 그 뒤 기본 백엔드를 정한다 |

---

## 1차 칸 (시작 후, 한 칸씩)

| | 칸 | 통과 |
|--|--|--|
| [x] | **M1.** 패널 모델로 `generateImage` 그림만 굽고(안 되면 기본 모델로 한 번, 피드에 알림) `pending_mesh_image`. 피드에 링크. TRELLIS 안 감 | “좋아” 전까지 TRELLIS 무호출 |
| [x] | **M2.** `.env.example` + `TRELLIS_URL`/`TRELLIS_HOME` 읽기. ping. 꺼져 있으면 안내만 | 코드에 드라이브 경로 0개, 가중치 import 없음 |
| [x] | **M3.** “좋아” 다음 Send: TRELLIS → GLB → **미리보기** 스폰. `/status` 진행 표시, Stop은 `/interrupt`. `pending_mesh_apply` | 최종 `SM_*` 없음 |
| [x] | **M4.** “적용해” + 이름 → `/Game/Meshes/SM_*`, 미리보기 삭제, 나무 자리에 둠 | 거절이면 미리보기만 지움 |
| [ ] | **M5.** 나무 채팅처럼: 그림 OK → 메쉬 OK → 적용이 한 사이클 | 잎 VFX와 한 Send에 안 섞임 |

M5 통과 후에야 해상도·다시 굽기·자동 기동·Tripo를 논의한다.
