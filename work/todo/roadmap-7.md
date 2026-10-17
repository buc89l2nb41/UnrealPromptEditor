# 체크리스트 — 로드맵 7 (MCP 손 · 본체)

ROADMAP 7을 열 때만 이 파일. **한 칸씩.** 이 장 전체를 한 번에 구현하지 않는다.

v0 큐브(`spawn_cube`)는 전선 확인용이다. **이 장이 본체다.** Cursor 앱 `mcp.json` / 원격 MCP / 범용 Unreal MCP 통째 이식은 아니다.

표기: `[x]` 코드에 이미 있음. `[ ]` 아직. **118칸.**

**시작 규칙:** 코드를 고치지 말고, 지금 칸 번호와 플러그인 상태를 짧게 말한 뒤 시작 여부를 묻는다.

씬 변경은 MCP → `GEditor`만. `.umap` 텍스트 편집 없음. Niagara/머티리얼/Blueprint **그래프 노드 없음.** `execute_python` 없음. Landscape 조각 없음. 액터는 **라벨**로 가리킨다. `spawn_cube`는 유지.

---

## 7A. 목록 · 프롬프트 (2)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7A1.** `tools/list`에 도구가 둘 이상. `spawn_cube` 유지 | 목록에 큐브 + 다른 이름 |
| [x] | **7A2.** 사이드카 시스템 프롬프트가 **지금 목록**을 말함. 손 넣을 때마다 맞춤 | 없는 손을 안 부름 |

## 7B. 맵 (7)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7B1.** `get_level_info` — 이름·경로·더티 | 지금 맵을 말함 |
| [x] | **7B2.** `save_level` — 현재 레벨. 프로젝트 안만 | `.umap` 저장 |
| [x] | **7B3.** `save_level_as` — 새 경로 | 그 경로에 파일 |
| [x] | **7B4.** `new_level` — 빈 기본 템플릿 | 새 맵 |
| [x] | **7B5.** `open_level` — 프로젝트 안 `.umap` | 그 맵이 열림 |
| [x] | **7B6.** `list_levels` — `/Game` 맵 경로 | 목록 |
| [x] | **7B7.** `save_dirty_packages` — 더티 패키지 저장(프로젝트 안) | 더티 클리어 |

## 7C. 액터 (20)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7C1.** `list_actors` — 라벨·클래스·위치 (길면 자름) | 이름 목록 |
| [x] | **7C2.** `find_actors` — 이름 일부 또는 클래스 | 필터 목록 |
| [x] | **7C3.** `spawn_actor` — 클래스 또는 스태틱 메시, 위치·회전·라벨. Undo | 스폰 |
| [x] | **7C4.** `move_actor` — 라벨 + 위치. Undo | 이동 |
| [x] | **7C5.** `rotate_actor` — pitch/yaw/roll. Undo | 회전 |
| [x] | **7C6.** `scale_actor` — 스케일. Undo | 크기 |
| [x] | **7C7.** `set_actor_transform` — 위치·회전·스케일 한 번에. Undo | 트랜스폼 |
| [x] | **7C8.** `get_actor_transform` | 숫자 |
| [x] | **7C9.** `duplicate_actor` — 오프셋 또는 새 라벨. Undo | 복제 |
| [x] | **7C10.** `destroy_actor` — 라벨. Undo | 삭제 |
| [x] | **7C11.** `select_actor` | 선택 |
| [x] | **7C12.** `get_selected_actors` | 선택과 일치 |
| [x] | **7C13.** `rename_actor`. Undo | 아웃라이너 |
| [x] | **7C14.** `set_actor_visible`. Undo | 뷰포트 |
| [x] | **7C15.** `set_actor_mobility` — Static/Movable. Undo | 모빌리티 |
| [x] | **7C16.** `attach_actor` — 자식을 부모에. Undo | 계층 |
| [x] | **7C17.** `detach_actor`. Undo | 부모 없음 |
| [x] | **7C18.** `set_actor_tag` — 추가/제거. Undo | 태그 |
| [x] | **7C19.** `spawn_plane` — Engine Plane, 기본 뷰포트 대면. Undo | 카드 |
| [x] | **7C20.** `face_viewport_camera`. Undo | 빌보드 |

## 7D. 빛 (9)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7D1.** `spawn_point_light` — 위치·색·강도. Undo | 점광 |
| [x] | **7D2.** `spawn_spot_light` — 위치·회전·콘·색·강도. Undo | 스폿 |
| [x] | **7D3.** `spawn_directional_light` — 회전·색·강도. Undo | 태양 |
| [x] | **7D4.** `spawn_rect_light` — 위치·크기·색. Undo | 면광 |
| [x] | **7D5.** `spawn_sky_light`. Undo | 스카이라이트 |
| [x] | **7D6.** `set_light_color`. Undo | 색 |
| [x] | **7D7.** `set_light_intensity`. Undo | 밝기 |
| [x] | **7D8.** `set_light_attenuation` — 점/스폿. Undo | 범위 |
| [x] | **7D9.** `recapture_sky_light` | 캡처 갱신 |

## 7E. Niagara (19)

그래프 없음. **있는** System만.

| | 칸 | 통과 |
|--|--|--|
| [x] | **7E1.** `list_niagara_assets` | 경로 |
| [x] | **7E2.** `spawn_niagara` — 경로 + 위치 + 라벨. Undo | 액터 |
| [x] | **7E3.** `set_niagara_playing` — 재생/정지. Undo | 파티클 |
| [x] | **7E4.** `reset_niagara` | 다시 시작 |
| [x] | **7E5.** `set_niagara_float` — user float | 값 |
| [x] | **7E6.** `set_niagara_color` — user 색 | 색 |
| [x] | **7E7.** `set_niagara_sprite_texture` — PNG를 시스템별 스프라이트 MIC에. `mode=cutout`/`material` 유지 | 스프라이트 그림 |
| [x] | **7E8.** `set_niagara_sprite_subuv` — SubImageSize + blend | 시트 |
| [x] | **7E9.** `configure_niagara_emitter` — spawn/lifetime/size/color/velocity/gravity | 이미터 설계 |
| [x] | **7E10.** `set_niagara_sprite_renderer` — facing/alignment/pivot/sort | 렌더러 |
| [x] | **7E11.** `list_niagara_renderers` | 덤프 |
| [x] | **7E12.** `add_niagara_renderer` — sprite | 렌더러 추가 |
| [x] | **7E13.** `set_niagara_mesh_renderer` | 메시 파티클 |
| [x] | **7E14.** `set_niagara_ribbon_renderer` | 리본 |
| [x] | **7E15.** `set_niagara_attribute` — spawn/update assignment | 파티클 속성 |
| [x] | **7E16.** `set_niagara_curve` — 모듈 curve DI | 커브 |
| [x] | **7E17.** `set_niagara_over_life` — size/color vs NormalizedAge | 수명 |
| [x] | **7E18.** `set_niagara_link` — 입력을 Particles.* 에 바인드 | 계산 |
| [x] | **7E19.** `unbind_niagara_link` — 링크와 Map Get 삭제 | 타입 충돌 해제 |

## 7F. 머티리얼 (11)

표현식 노드 없음. `create_flipbook_mic` / `create_noise_mic`은 플러그인이 만든 부모의 MIC만 만든다.

| | 칸 | 통과 |
|--|--|--|
| [x] | **7F1.** `list_materials` | 경로 |
| [x] | **7F2.** `assign_material` — 액터 + 경로. Undo | 입혀짐 |
| [x] | **7F3.** `assign_material_slot` — 슬롯 인덱스. Undo | 그 슬롯 |
| [x] | **7F4.** `get_actor_materials` | 목록 |
| [x] | **7F5.** `create_color_material` — 단색 MIC + Content 경로 | 에셋 |
| [x] | **7F6.** `create_material_instance` — 부모 + 저장 경로 | MIC |
| [x] | **7F7.** `set_material_color` — MIC 벡터/색 파라미터 | 색 |
| [x] | **7F8.** `set_material_scalar` — MIC float 파라미터 | 스칼라 |
| [x] | **7F9.** `create_flipbook_mic` — 아틀라스 Time UV MIC | 플립북 |
| [x] | **7F10.** `create_noise_mic` — 구운 RG 컬 노이즈로 스프라이트 UV 왜곡 | 노이즈 |
| [x] | **7F11.** `apply_vfx_look` — 사이드카가 구운 PNG를 한 룩으로 붙임 (플립북/노이즈 카드/파티클/리본/메시). 그래프 손 아님 | 레시피 |

## 7G. 카메라 · 뷰포트 (6)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7G1.** `get_viewport_camera` | 숫자 |
| [x] | **7G2.** `set_viewport_camera` — 위치·회전 | 뷰포트 |
| [x] | **7G3.** `focus_actor` | 프레임 |
| [x] | **7G4.** `spawn_camera`. Undo | 카메라 액터 |
| [x] | **7G5.** `spawn_cine_camera`. Undo | CineCamera |
| [x] | **7G6.** `capture_viewport` — `Saved/PromptEditor/screenshots/` | 파일 |

## 7H. 환경 (6)

볼륨/액터 + 소수 파라미터만.

| | 칸 | 통과 |
|--|--|--|
| [x] | **7H1.** `spawn_sky_atmosphere`. Undo | 하늘 |
| [x] | **7H2.** `set_sky_atmosphere` — 탁도 또는 태양 디스크 | 톤 |
| [x] | **7H3.** `spawn_exponential_fog` — 밀도·색. Undo | 안개 |
| [x] | **7H4.** `set_exponential_fog`. Undo | 안개 변경 |
| [x] | **7H5.** `spawn_post_process_volume` — 언바운드. Undo | 볼륨 |
| [x] | **7H6.** `set_post_exposure` — 노출 | 밝기 |

## 7I. 콘텐츠 검색 (6)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7I1.** `list_assets` — 접두 + 최대 N | 경로 |
| [x] | **7I2.** `list_static_meshes` | 스폰용 경로 |
| [x] | **7I3.** `list_blueprints` | 목록 |
| [x] | **7I4.** `list_textures` | 목록 |
| [x] | **7I5.** `list_sounds` | 목록 |
| [x] | **7I6.** `asset_exists` | bool |

## 7J. 컴포넌트 (4)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7J1.** `list_components` | 이름 |
| [x] | **7J2.** `set_static_mesh` — 메시 경로. Undo | 교체 |
| [x] | **7J3.** `set_component_relative_transform`. Undo | 오프셋 |
| [x] | **7J4.** `set_component_visible`. Undo | 가시성 |

## 7K. 물리 · 콜리전 (4)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7K1.** `set_simulate_physics`. Undo | 시뮬 |
| [x] | **7K2.** `set_collision_enabled`. Undo | 콜리전 |
| [x] | **7K3.** `set_collision_profile`. Undo | 프로파일 |
| [x] | **7K4.** `set_actor_enable_gravity`. Undo | 중력 |

## 7L. 아웃라이너 (4)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7L1.** `set_actor_folder`. Undo | 폴더 |
| [x] | **7L2.** `create_actor_folder` | 폴더 생김 |
| [x] | **7L3.** `set_actor_hidden_in_game`. Undo | 플래그 |
| [x] | **7L4.** `isolate_actor` — 선택·포커스 | 뷰 |

## 7M. 사운드 (4)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7M1.** `spawn_ambient_sound` — 에셋 + 위치. Undo | 오디오 액터 |
| [x] | **7M2.** `set_sound_volume`. Undo | 크기 |
| [x] | **7M3.** `set_sound_playing` | 재생/정지 |
| [x] | **7M4.** `set_sound_attenuation`. Undo | 거리 |

## 7N. Sequencer (4)

샷 연출 전체 아님.

| | 칸 | 통과 |
|--|--|--|
| [x] | **7N1.** `create_level_sequence` — Content 경로 | 에셋 |
| [x] | **7N2.** `open_level_sequence` | 시퀀서 열림 |
| [x] | **7N3.** `sequence_bind_actor` | 바인딩 |
| [x] | **7N4.** `sequence_set_range` — 시작/끝 프레임 | 범위 |

## 7O. Blueprint 껍데기 (3)

노드 없음.

| | 칸 | 통과 |
|--|--|--|
| [x] | **7O1.** `create_blueprint` — 부모 + Content 경로 | BP 에셋 |
| [x] | **7O2.** `compile_blueprint` | 컴파일 성공 |
| [x] | **7O3.** `spawn_blueprint` — 경로 + 위치·라벨. Undo | 인스턴스 |

## 7P. 에디터 유틸 (4)

| | 칸 | 통과 |
|--|--|--|
| [x] | **7P1.** `editor_undo` | Ctrl+Z와 같음 |
| [x] | **7P2.** `editor_redo` | Ctrl+Y |
| [x] | **7P3.** `redraw_viewports` | 갱신 |
| [x] | **7P4.** `get_editor_world_name` | 월드 이름 |

## 7R. NiagaraEditor 그래프 (시도)

에디터 API로 System/모듈/컴파일. 범용 MCP 이식 아님. `execute_python` 없음.

| | 칸 | 통과 |
|--|--|--|
| [x] | **7R1.** `create_niagara_system` — `/Game` 경로. NiagaraEditor Initialize | 에셋 |
| [x] | **7R2.** `list_niagara_modules` — 스택별 라이브러리 모듈 | 경로 |
| [x] | **7R3.** `add_niagara_module` — 스택에 모듈 | 스택 |
| [x] | **7R4.** `list_niagara_stack` — 스택·모듈·`set name=` 입력 | 값 |
| [x] | **7R5.** `compile_niagara` — 에러/경고 문자열 후 `spawn_niagara` | 뷰포트 |
| [x] | **7R6.** `create_niagara_grid2d` — GPU + Grid2D Collection + sim stage | 에셋 |
| [x] | **7R7.** `add_niagara_simulation_stage` — DataInterface 스테이지 | 스테이지 |
| [x] | **7R8.** `list_niagara_stack` / `add_niagara_module` — sim_stage · Grid2D DI | 덤프 |
| [x] | **7R9.** `create_niagara_parameter_collection` + `set_niagara_collection` | NPC 에셋 |
| [x] | **7R10.** `bind_niagara_collection` | 시스템에 연결 |
| [x] | **7R11.** `create_render_target` | RT 에셋 |
| [x] | **7R12.** `set_niagara_user_object` — mesh/texture/rt | User 오브젝트 |
| [x] | **7R13.** `add_niagara_di` — grid2d/rt/par/query/mesh | 이미터 DI |
| [x] | **7R14.** `set_niagara_sim_stage` — execute always/reset, iterate DI/particles | 스테이지 |
| [x] | **7R15.** `add_niagara_emitter` — sprite/grid2d/empty | 두 번째 이미터 |
| [x] | **7R16.** `attach_niagara` + `set_component_tag` + `set_niagara_grid_target` | 붙이기·태그·Grid 수출 |
| [x] | **7R17.** `preview_niagara_grid` — Grid → RT → 레벨 플레인 | 뷰포트 히트맵 |
| [x] | **7R18.** `remove_niagara_module` — 스택에서 모듈 제거 | 빨간 NS 고치기 |

## 7Q. 한 장면 통과 (5)

사용자 에디터 패스. 에이전트는 이 칸을 구현하지 않는다.

| | 칸 | 통과 |
|--|--|--|
| [ ] | **7Q1.** 새 맵 → 메시 → 단색 머티리얼 → 디렉셔널+포인트 라이트 → 하늘 | 장면 |
| [ ] | **7Q2.** Niagara 스폰 → float/색 하나 → 카메라 맞춤 → 저장 | `.umap` |
| [ ] | **7Q3.** 스크린샷이 그 장면을 담음 | 이미지 |
| [ ] | **7Q4.** 이동·복제·삭제 후 `editor_undo` | Undo |
| [ ] | **7Q5.** 저장본을 다시 열면 액터가 남음 | 재오픈 |

## 하지 말 것

범용 Unreal MCP 플러그인 복사. 한 칸에 도구 몰아넣기. Landscape/Foliage 조각. 머티리얼/Blueprint **그래프 노드**. 복잡한 Sequencer 샷. PIE 게임 자동화. 원격 MCP. `execute_python`으로 GEditor 우회. `spawn_cube` JSON 이중 구현. 패키징·UBT.
