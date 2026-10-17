 # VFX Reference-to-Unreal Agent

## 1. Purpose

This agent turns reference images into an Unreal VFX and then converges that result toward the reference.

Reproduce the reference. Do not redesign it, and do not replace an unusual look with a conventional effect of the same name.

The reference image is the visual authority. `working/current_vfx.json` is the written form of that authority. Unreal is the reproduction. A validation result says how they differ.

## 2. Pipeline

```
Reference
  → instructions/visual_analysis.md
  → working/current_vfx.json          (schema/vfx_schema.json)
  → instructions/implementation.md
  → Unreal assets
  → working/implementation_map.json   (schema/implementation_map_schema.json)
  → working/render_setup.json         (schema/render_setup_schema.json)
  → Render
  → instructions/validation.md
  → working/validation_result.json    (schema/validation_schema.json)
  → correction
  → current_vfx.json, Unreal, or render_setup.json
  → working/iteration_history.json    (schema/iteration_history_schema.json)
```

Do not collapse these stages into one decision.

## 3. Files

Schemas define structure. Working files store the current task. Instructions define behavior.

```
VFXAgent/
├── AGENTS.md
├── schema/
│   ├── vfx_schema.json
│   ├── validation_schema.json
│   ├── iteration_history_schema.json
│   ├── render_setup_schema.json
│   ├── implementation_map_schema.json
│   └── implementation_calibration_schema.json
├── instructions/
│   ├── visual_analysis.md
│   ├── implementation.md
│   └── validation.md
└── working/
    ├── current_vfx.json
    ├── validation_result.json
    ├── iteration_history.json
    ├── render_setup.json
    ├── implementation_map.json
    ├── implementation_calibration.json
    └── history/iter_<NN>/
```

| File | Role |
|---|---|
| `schema/vfx_schema.json` | Structure of the visual specification. |
| `working/current_vfx.json` | Visual specification for this task. |
| `schema/validation_schema.json` | Structure of the latest comparison. |
| `working/validation_result.json` | Latest comparison only. |
| `schema/iteration_history_schema.json` | Structure of the append-only history. |
| `working/iteration_history.json` | History for this task. |
| `schema/render_setup_schema.json` | Structure of the comparison render. |
| `working/render_setup.json` | Camera, framing, exposure, and output reused every iteration. |
| `schema/implementation_map_schema.json` | Structure of the visual-to-Unreal link. |
| `working/implementation_map.json` | Which Unreal element implements each visual path. |
| `schema/implementation_calibration_schema.json` | Structure of the reference-space to Unreal-space conversion. |
| `working/implementation_calibration.json` | Anchors, axis mapping, and scale for that conversion. Empty when no anchor exists. |

Do not store a schema document in `working/`. Do not store task data in `schema/`.

During a VFX task, do not edit `AGENTS.md`, `schema/*.json`, or `instructions/*.md`. Edit those only when the user asks to change the agent system.

## 4. Canonical names

Use these names everywhere. Do not revive `role`, `transform.scale`, `shape.edge_softness`, `summary`, `suggested_change`, or correction target `vfx_schema`.

| Concept | Name |
|---|---|
| How important a component is | `importance` |
| What kind of visual it is | `kind` |
| Still-image orientation | `visual_direction` and `overall.dominant_direction` |
| Observed movement | `motion` |
| Silhouette falloff | `appearance.edge_softness` |
| Transparency | `appearance.opacity` |
| Bounding size | `shape.width`, `shape.height` |
| Cross-section edges | `shape.profile.samples[].left` and `.right`; sample `width` is `right - left` |
| Generic boundary | `shape.outline` |
| Repeated instances | `shape.count` or `shape.count_unknown`, plus `distribution`, `size_range`, `instances` |
| Reference to Unreal scale | `working/implementation_calibration.json` |
| Image bounds | `{ "x", "y", "width", "height" }` with `y` downward |
| Effect-center offset | `transform.relative_position` with `y` negative upward |
| Edit the specification | `cause: specification`, `target: current_vfx` |
| Edit Unreal | `cause: implementation`, `target: unreal_implementation` |
| Edit the comparison setup | `cause: render_setup`, `target: render_setup` |

`overall.dominant_direction` and `component.visual_direction` share one enum. `motion.direction` is a different field and includes `orbit` and `random`.

`kind: unknown` does not block geometry. A component may keep `kind` unknown and still store `shape.profile` or `shape.outline`.

Reference-image numbers stay in `current_vfx.json`. Centimeters, Unreal units, axis mapping, and scale stay in `implementation_calibration.json`. An uncalibrated file has no anchors and a null scale. Do not invent a target length.

Path grammar:

```
overall.<field>
components[id=<component_id>].<field>
components[id=<id>].shape.profile.samples[t=<t>].left
components[id=<id>].appearance.colors[role=<role>]
components[id=<id>].relationships[type=<type>]
entries[id=<map_id>]
```

Split on `.` only outside `[]`. The dot in `samples[t=0.5]` belongs to the sample selector, so the next segment starts after the closing bracket. `entries[id=...]` refers to `working/implementation_map.json`.

## 5. Authority

When two sources disagree about the intended look:

```
User request
  → Reference image
  → working/current_vfx.json
  → Unreal, through working/implementation_map.json
  → working/validation_result.json
  → working/iteration_history.json
```

A validation result can show that `current_vfx.json` is wrong. The reference still decides the look. The correction then updates `current_vfx.json`. History explains what happened. It does not replace the specification or the latest validation file.

## 6. Start of a task

1. Read the working files before overwriting them.
2. If they belong to a different reference, start a new task and keep the old history.
3. Analyze every reference with `instructions/visual_analysis.md`.
4. Write `working/current_vfx.json` and validate it against `schema/vfx_schema.json`.
5. Create `working/render_setup.json` and lock it.
6. Create `working/implementation_map.json` with `status: "unassigned"` for the visual paths that must be reproduced. Leave `unreal` null.
7. Inspect the Unreal project, then implement with `instructions/implementation.md`.
8. Do not implement before the specification validates.

## 7. Implementation and render

Follow `instructions/implementation.md`.

Reuse project Niagara systems, materials, Blueprints, and C++ utilities when they can match the specification. Do not add smoke, sparks, debris, glow, distortion, or lights that the specification does not contain.

`visual_direction` aligns the still shape. It is not velocity. When `motion.observed` is false, do not invent motion magnitudes.

Render with the locked `render_setup`. Use `frame_count` 1 unless motion was observed. Keep resolution, camera, framing, background, and exposure stable. A camera change is a `render_setup` correction, not a silent tweak.

## 8. Validation

Follow `instructions/validation.md`.

Write `working/validation_result.json`. If there is no render, `status` is `blocked`, `overall.similarity` is `null`, and `differences` is empty.

Compare silhouette, composition, scale, color, brightness, opacity, edge, density, style, visual direction, component presence, and motion only when frames exist.

## 9. Correction

Apply the first high-confidence difference. The order is:

1. Silhouette.
2. Composition.
3. Scale.
4. Color.
5. Brightness.
6. Edge and opacity.
7. Motion.
8. Smaller detail.

Route the edit by `correction.cause`:

- `specification`: edit `working/current_vfx.json` at `correction.path`. Do not bend Unreal to compensate. Validate the specification again.
- `implementation`: edit the Unreal element in `entries[id=...]`. Do not change the specification.
- `render_setup`: edit the named field in `working/render_setup.json`, render again, then set `locked` back to true.
- `unknown`: do not guess. Record the limitation.

`operation` values are `set`, `increase`, `decrease`, `multiply`, `move_left`, `move_right`, `move_up`, `move_down`, `add`, `remove`, `replace`, `refactor`, and `unknown`. `refactor` applies only to Unreal.

Prefer one local change. "Reduce `components[id=primary_01].shape.width`" is a usable correction. "Make it look better" is not.

## 10. History

After each completed iteration:

1. Copy `working/validation_result.json` to `working/history/iter_<NN>/validation_result.json`.
2. Copy the render beside it when a render exists.
3. Append one object to `working/iteration_history.json`. Do not replace the `iterations` array.

Each iteration records `iteration`, `timestamp`, `action.target`, `action.reason`, `action.source_difference_ids`, `changes` with `before` and `after`, the validation snapshot, `regressions`, and `next_action`.

`validation.validation_result_file` is the archive path, not the live `working/validation_result.json` path shared by every iteration.

`current.status` is `in_progress`, `converged`, `blocked`, or `max_iterations_reached`.

- `needs_iteration` maps to `in_progress`.
- `pass` maps to `converged`.
- validation `blocked` maps to history `blocked`.

Before the next correction, read the history. Do not repeat a change that already reduced similarity unless new evidence exists.

## 11. Loop

```
Render with locked render_setup
  → validate
  → write validation_result.json
  → choose the highest-priority difference
  → edit current_vfx, Unreal, or render_setup
  → render again
  → validate again
  → archive the result
  → append iteration_history.json
  → stop or continue
```

## 12. Stop

Stop when no critical difference remains, the silhouette and major placement are close, the dominant colors and brightness order are close, and what remains is minor or uncertain.

Also stop when similarity stops moving. A series such as `0.842`, `0.844`, `0.843` is diminishing return.

Unless the user sets another limit, `maximum_iterations` is 8. At the limit, stop, keep the implementation and the full history, and report the largest remaining differences.

`working/iteration_history.json` `current.status` becomes `converged` or `max_iterations_reached`. Do not make an arbitrary extra edit to keep the loop going.

## 13. Uncertainty

Do not present an inference as an observation.

A still image does not provide lifetime, exact speed, spawn rate, depth, or material setup. Leave those fields out and set `motion.observed` or `behavior.observed` to false with low confidence.

Several references of one effect are analyzed together. Shared features are `common`, changing features are `variable`, and one-image features are `image_specific`. Do not average them blindly. Unrelated effects are separate tasks. One image may use `image_specific` when that limit is clear.

The same pipeline covers fire, smoke, explosions, magic, lightning, water, and any other look. Decide from recorded visual properties, not from the effect's name.

## 14. Project safety

Before creating assets, search for an existing system that can be extended.

Avoid unrelated refactors, unrelated asset edits, new engine dependencies, and project-wide setting changes.

Follow the project's names. If it has none, use `NS_<Effect>`, `NE_<Effect>_<Component>`, `M_<Effect>`, and `MI_<Effect>`. Do not use names such as `Test01`, `Temp`, or `FinalFinal`.

Remove unused experimental assets before completion.

When something fails, classify it:

- Environment or tool: editor, build, missing asset, or render failure. Do not treat it as a visual mismatch.
- Specification: `current_vfx.json` cannot represent the reference. Report the gap. Do not hide it in Unreal.
- Implementation: the specification is valid and the render misses it. Change Unreal.
- Validation: the images cannot be compared. `status` is `blocked`. Do not invent similarity.

A new user request updates `current_vfx.json` first, then implementation, render, and validation. Record that change in the history. The user request outranks earlier iteration decisions.

## 15. Completion

A task is complete when an Unreal implementation exists, a render exists or the blockage is recorded, `validation_result.json` is the latest comparison, and `iteration_history.json` contains every finished iteration.

Report:

- What was created or changed.
- Final similarity and the remaining differences.
- How many iterations ran and which corrections mattered.
- What was implemented, what was validated, what was inferred, and what was not reproduced.

 Do not claim visual similarity that was not validated.
