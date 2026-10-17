# VFX Validation Agent

## 1. Role

Compare the reference image, `working/current_vfx.json`, and the rendered image. Write `working/validation_result.json` so it validates against `schema/validation_schema.json`.

This agent does not edit Unreal assets, `current_vfx.json`, `render_setup.json`, or files in `schema/`. It names the correction. The orchestrator applies it.

Do not invent a similarity score when no render exists.

## 2. Inputs

- Reference image listed in `current_vfx.reference.images`.
- `working/current_vfx.json`.
- `working/render_setup.json`.
- The rendered image at `render_setup.output`, when it exists.
- The previous `working/validation_result.json`, when a regression check is possible.

Record those paths in `inputs`. `inputs.render_image` is `null` when there is no render.

## 3. When comparison is blocked

If `inputs.render_image` is null, or the render and reference cannot be compared:

- `status` is `blocked`.
- `overall.similarity` is `null`.
- `overall.confidence` is low.
- `differences` is empty.
- `limitations` states why.
- `iteration.should_continue` is `false`.

Do not fill similarity with `0` to mean "not compared."

## 4. Alignment

Compare image space. The origin is the top-left, `x` increases right, and `y` increases down.

`alignment.reference_bounds` and `alignment.render_bounds` use `{ "x", "y", "width", "height" }`, the same object as `overall.bounds`. Centers use `{ "x", "y" }`.

A framing, exposure, or background mismatch is category `render_setup`. Do not call it a shape error.

`transform.relative_position` is not image space. Its negative `y` is up. Convert it to image space before comparing it with the render.

## 5. Scores

`overall` may include `silhouette`, `composition`, `scale`, `color`, `brightness`, `opacity`, `edge`, `motion`, `density`, `style`, and `visual_direction`. Each score is `{ "value", "confidence" }`.

`silhouette` is the whole outline. A single component's form is `shape` on that component result.

Component results may score `shape`, `position`, `scale`, `color`, `brightness`, `opacity`, `edge`, `emissive`, `density`, `motion`, and `visual_direction`.

`edge` means `appearance.edge_softness`. `opacity` is separate.

`component_id` matches `current_vfx` `component.id`. `status` is `matched`, `partially_matched`, `missing`, `extra`, or `uncertain`.

## 6. Differences

Every difference needs `id`, `category`, `severity`, `confidence`, `description`, and `correction`.

`category` is one of:

`silhouette`, `composition`, `shape`, `scale`, `color`, `brightness`, `opacity`, `edge`, `motion`, `density`, `component`, `style`, `visual_direction`, `render_setup`, `unknown`.

`property` is the canonical path of the visual value, for example `components[id=primary_01].shape.width`. Split on `.` only outside `[]`, so `samples[t=0.5].width` keeps the decimal inside the selector.

`severity` is `critical`, `major`, `moderate`, or `minor`.

Order the `differences` array by visual impact times confidence. The priority is:

1. `silhouette`
2. `composition`
3. `scale`
4. `color`
5. `brightness`
6. `edge` and `opacity`
7. `motion`
8. Smaller component detail

Do not report a motion mismatch when `motion.observed` is `false` and `render_setup.frame_count` is `1`. The direction of a still shape is `visual_direction`, not motion.

## 7. Correction

`correction.cause` chooses `correction.target`:

| cause | target | Who edits |
|---|---|---|
| `specification` | `current_vfx` | Orchestrator edits `working/current_vfx.json` at `path`. |
| `implementation` | `unreal_implementation` | Implementation agent edits the mapped Unreal element. `path` is `entries[id=<map id>]`. |
| `render_setup` | `render_setup` | Orchestrator edits `working/render_setup.json` at `path`. |
| `unknown` | `unknown` | No edit until the cause is known. |

`operation` is `set`, `increase`, `decrease`, `multiply`, `move_left`, `move_right`, `move_up`, `move_down`, `add`, `remove`, `replace`, `refactor`, or `unknown`.

`set` stores the new absolute value. `multiply` stores a factor. `increase` and `decrease` store a normalized delta. `move_*` stores a normalized offset. `refactor` is only for `unreal_implementation`.

A specification correction must name a real `current_vfx` path. An implementation correction must not be used to hide a wrong specification. A render-setup correction must not be used to reshape the effect.

```json
{
  "id": "diff_primary_width",
  "component_id": "primary_01",
  "category": "shape",
  "property": "components[id=primary_01].shape.width",
  "severity": "major",
  "confidence": 0.9,
  "description": "The implemented core is wider than the specification.",
  "reference_value": 0.18,
  "render_value": 0.27,
  "correction": {
    "cause": "implementation",
    "target": "unreal_implementation",
    "path": "entries[id=map_primary_width]",
    "operation": "multiply",
    "value": 0.67,
    "description": "Reduce the mapped width to the specified width."
  }
}
```

The full document also includes `version` `"1.1"`, `status`, `inputs`, `overall`, and `differences`.

## 8. Regressions and history

`regressions` lists properties that got worse since the previous render. The category enum matches `differences`.

This file is the latest result. Copy it to `working/history/iter_<NN>/validation_result.json` before the next comparison replaces it. The iteration history stores that archive path and the `difference.id` values. Do not rely on the live `working/validation_result.json` to remember older results.

## 9. Stop

`status` is `pass` when no critical difference remains, the silhouette and major placement are close, and the rest is minor or uncertain. `iteration.should_continue` is then `false`.

`status` is `needs_iteration` when a concrete correction remains. `should_continue` is `true` only when another iteration is justified.

`pass` does not mean the effect is a good conventional VFX. It means the render is close to the reference.
