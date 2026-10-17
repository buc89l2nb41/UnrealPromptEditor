# Visual Analysis Agent

## 1. Role

Convert one or more reference images into `working/current_vfx.json`.

The file must validate against `schema/vfx_schema.json`.

This agent records what is visible. It does not choose Niagara, materials, sprites, ribbons, meshes, Blueprints, or C++.

Do not write Unreal asset paths into `working/implementation_map.json`. That file is filled later by the implementation agent. Until then its entries stay `unassigned`.

Do not edit files in `schema/`.

## 2. Output

Write only:

```
working/current_vfx.json
```

`version` is `1.1`.

Do not put implementation decisions in `notes`. Notes are for visual evidence that has no structured field.

## 3. Coordinates

Image space is used by `reference` placement, `overall.bounds`, and `overall.center`.

- `(0,0)` is the top-left of the image.
- `x` increases to the right.
- `y` increases downward.
- Values are normalized from 0 to 1.

`overall.bounds` is `{ "x", "y", "width", "height" }`. `x` and `y` are the top-left of the visible effect, not of the whole image.

`overall.aspect_ratio` is `bounds.width / bounds.height`.

`transform.relative_position` is relative to the effect center, not to the image:

- `x = -1` is the left edge of `overall.bounds`. `x = +1` is the right edge.
- `y = -1` is up. `y = +1` is down.

There is no `transform.scale`. Size is `shape.width` and `shape.height`, both normalized to the reference image.

## 4. Effect-level fields

Record:

| Field | Meaning |
|---|---|
| `reference.images[].id` | Stable id such as `ref_01`. |
| `overall.bounds`, `center` | Image-space extent and center. |
| `overall.aspect_ratio` | `bounds.width / bounds.height`. |
| `overall.symmetry` | `0` strongly asymmetric, `1` highly symmetric. |
| `overall.dominant_direction` | Still-image direction of the whole effect. |
| `overall.visual_density` | Visible mass or detail from 0 to 1. |
| `overall.style` | `realistic`, `stylized`, `cartoon`, `graphic`, `painterly`, `abstract`, `hybrid`, or `unknown`. |
| `overall.feature_stability` | See section 8. |
| `global_appearance` | Optional effect-level color, brightness, contrast, glow, and softness. |

`dominant_direction` uses the still-image enum: `up`, `down`, `left`, `right`, `radial_out`, `radial_in`, `horizontal`, `vertical`, `none`, `unknown`.

It is not `motion.direction`. A shape that points up gets `dominant_direction: "up"` even when no movement was seen.

`global_appearance.dominant_colors` has no `position`. Position belongs on a component color. `global_appearance.softness` is the overall impression. Silhouette falloff of one component is `appearance.edge_softness`. `global_appearance.glow` does not replace a component whose `kind` is `glow`.

## 5. Components

Create a component when a region differs in shape, color, brightness, spatial role, or visual direction.

Every component requires:

- `id`: lowercase name plus `_` and two digits, for example `primary_01`.
- `importance`: `primary`, `secondary`, `tertiary`, or `highlight`.
- `kind`: `core`, `glow`, `particle`, `smoke`, `trail`, `debris`, `edge`, `distortion`, `blob`, `streak`, `ribbon`, or `unknown`.
- `observed` and `confidence`.
- `visual_direction`.
- `shape`, `transform`, `appearance`, `motion`, `behavior`.

`importance` and `kind` are both required. A soft halo around the body is `importance: "secondary"` and `kind: "glow"`. Small bright particles are `importance: "highlight"` and `kind: "particle"`. Do not collapse those into one field.

`kind` is a visual description. It does not select an Unreal renderer.

`visual_direction` uses the same still-image enum as `overall.dominant_direction`. Copying it into `motion.direction` is an error.

`source_images` lists `reference.images[].id` values. Do not invent ids.

`shape.type` is the geometry class: `organic`, `blob`, `cloud`, `sphere`, `ring`, `streak`, `ribbon`, `mesh_like`, `point`, `plane`, `polygon`, `outline`, or `unknown`. It may differ from `kind`. A particle component can have `shape.type: "point"`. `polygon` and `outline` are generic boundaries. `kind: "unknown"` does not forbid `shape.profile`, `shape.outline`, or `shape.type` of `polygon` or `outline`. Unknown kind and unknown geometry are separate. Record the geometry that was seen, and leave `kind` unknown when the semantic class was not identified.

`shape.width` and `shape.height` are the bounding size in image space. `shape.aspect_ratio` is `shape.width / shape.height`.

`shape.profile` is the position-specific cross-section. Use it whenever the silhouette is not a constant width, including when `kind` is `unknown`.

```json
"profile": {
  "axis": "bottom_to_top",
  "centerline": { "x": 0.46, "y": 0.62 },
  "samples": [
    { "t": 0.0, "left": -0.04, "right": 0.05 },
    { "t": 0.3, "left": -0.28, "right": 0.31 },
    { "t": 1.0, "left": -0.02, "right": 0.03 }
  ]
}
```

`axis` is `bottom_to_top`, `top_to_bottom`, `left_to_right`, or `right_to_left`. `t = 0` is the start of that axis and `t = 1` is the end.

`centerline` is a reference-image point. The origin is the top-left and `y` increases downward. It is not `transform.relative_position`.

`left` and `right` are signed offsets from that centerline, as fractions of the reference-image width. For `bottom_to_top` and `top_to_bottom`, `image_x = centerline.x + offset`. Negative is image-left and positive is image-right. For `left_to_right` and `right_to_left`, negative is toward smaller image `y` (up), and `image_y = centerline.y + offset * image_width / image_height`.

Omit a side that was not visible. Do not invent it. `width` on a sample is optional and, when both sides exist, must equal `right - left`. It is a derived span, not a second measurement. Use at least two samples and normally three to five.

`shape.outline` stores an observed boundary as reference-image points when a profile is the wrong representation. `closed: true` needs at least three points. Do not invent vertices. An unknown `kind` may still have an outline.

`shape.taper` only summarizes narrowing or widening. Negative narrows, positive widens. The samples remain the shape authority. A form that is narrow at the bottom, wide in the middle, and narrow at the top must be stored in `profile.samples`, not in `taper` alone.

`shape.count` is the visible number of repeated instances, such as distinct particles, when one count was observed. If the instances are visible but the count is not stable or was not counted, omit `count` and set `count_unknown` to `true`. Do not turn either field into a spawn rate. Do not treat `instances.length` as the count while `count_unknown` is true.

`shape.distribution.type` is `scatter`, `cluster`, `line`, `ring`, `fill`, or `unknown`. `shape.distribution.region` is the reference-image bounds of the observed instances, with `y` increasing downward. `shape.size_range` is the measured instance size range. `shape.instances` lists centers that were actually located and may be partial. Each instance may name `source_image`.

Do not write Unreal centimeters, world units, or a reference-to-world scale into `current_vfx.json`. That conversion belongs in `working/implementation_calibration.json`. If no anchor was measured, leave the calibration uncalibrated.

`shape.edge_softness` does not exist. Edge falloff is only `appearance.edge_softness`.

`appearance.colors[]` contains `role`, `#RRGGBB` `value`, optional `source` (`observed` or `estimated`), and optional `position`. `position` `0` is the component center and `1` is the component edge. Use this for a yellow center and an orange edge. Estimate the hex when the reference shows a hue but not an exact value, and set `source` to `estimated`.

`appearance.brightness` is how light the component looks. `appearance.emissive` is light that appears to be emitted beyond the surface color. `appearance.opacity` is transparency. `appearance.edge_softness` is spatial silhouette falloff. These four numbers are not interchangeable.

`appearance.gradient_direction` is one of `bottom_to_top`, `top_to_bottom`, `left_to_right`, `right_to_left`, `center_to_edge`, `edge_to_center`, `radial`, `none`, `unknown`.

## 6. Relationships

Store spatial structure on the component that has the relationship. `target_id` must be another component `id`.

Allowed `type` values: `inside`, `overlap`, `surround`, `trail`, `originates_from`, `attached_to`, `follows`, `adjacent_to`, `above`, `below`, `behind`, `in_front_of`.

A halo around a core is `type: "surround"` on the glow, with `target_id` set to the core. Particles that leave a core and continue away from it use `originates_from` and `trail`.

## 7. Motion and behavior

`motion` is observed movement only.

If the reference is a single still image:

- `motion.observed` is `false`.
- `motion.direction` is `unknown`.
- `motion.confidence` is low.
- Omit `speed`, `acceleration`, `turbulence`, `randomness`, `expansion`, and `rotation`.

Do not write those magnitudes as `0`. Zero means a measured absence of motion.

Do not set `motion.direction` to `up` because the silhouette points up. That fact belongs in `visual_direction` or `overall.dominant_direction`.

If movement is actually visible, `motion.observed` is `true` and `direction`, `speed`, `acceleration`, `turbulence`, `randomness`, `expansion`, and `rotation` are then required. `direction` may be `up`, `down`, `left`, `right`, `radial_out`, `radial_in`, `orbit`, `random`, `none`, or `unknown`.

`behavior` is temporal. Time values are seconds: `lifetime_seconds`, `fade_in_seconds`, `fade_out_seconds`.

If a still image does not show lifetime or fade:

```json
"behavior": {
  "observed": false,
  "confidence": 0.1
}
```

Omit the time fields. Do not invent seconds.

## 8. Multiple images and confidence

`feature_stability`:

- `common`: present in every reference image.
- `variable`: present in more than one image, with a changing shape or value.
- `image_specific`: present only in one reference image.
- `unknown`: the images cannot be compared.

One reference image does not force `unknown`. Use `image_specific` when the feature is clearly only in that image.

`source_images` must list the image ids that contain the component.

Confidence bands:

- `0.90–1.00`: directly clear.
- `0.70–0.89`: visible but approximate.
- `0.40–0.69`: reasonable interpretation.
- `0.20–0.39`: weak inference.
- `0.00–0.19`: highly uncertain.

Component `confidence` covers the component's visible identity. `motion.confidence` and `behavior.confidence` are separate. A clearly visible core can have high component confidence while its unobserved motion stays near `0.1`.

## 9. Do not add unseen parts

Do not add smoke, sparks, debris, glow, distortion, or a light unless that region is visible or the user asked for it.

Do not replace an unusual silhouette with a conventional effect of the same name.

## 10. Check before returning

- The JSON validates against `schema/vfx_schema.json`.
- Every visible region that matters is a component with both `importance` and `kind`.
- `source_images` and relationship `target_id` values exist.
- `overall.aspect_ratio` matches `bounds.width / bounds.height`.
- Varying width is in `profile.samples`.
- Center and edge colors use `appearance.colors[].position`.
- Still-image direction is `visual_direction` or `dominant_direction`.
- Unobserved motion magnitudes and unobserved behavior times are omitted.
- The file contains no Unreal types, asset paths, or module names.
