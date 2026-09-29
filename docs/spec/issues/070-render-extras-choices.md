# 070: render extras: what render-pipeline.md leaves open

Status: open, choices made. Raised by WP-35. Affects `engine/src/render/{shadow_renderer,
ground_marks,sprite_renderer,dynamic_lights,envmap,mesh_renderer}.cpp`,
`apps/viewer/level_render.cpp`.

## 1. Planar shadow yaw of attached models

Section 5.3 rotates a planar shadow by the entity yaw (`angles[2]`). An attachment with the
`abs` modifier takes its orientation from its parent, so its own `angles[2]` is not the yaw it
is drawn with. Choice: the yaw of a shadow is the heading of the record's local X axis in the
world (`atan2` of the world matrix column 0), which equals `angles[2]` for roots and follows
the parent for `abs` children. Every model record with a `shadow` statement casts its own
shadow, children included; effect-list records never do (section 5.3).

## 2. Projected shadow rotation key

Map objects pass their placement byte (units of 30 degrees). The level viewer gives an
attached child the rotation key of the placement it belongs to; `create`-spawned entities
(section 5.3 quirk, key = `int(yaw)`) are the game loop's business, the renderer only takes
the key as an argument.

## 3. Shadow silhouette bounds

Section 5.2 floors and ceils the projected bounding rectangle. In floating point, rotating
by a quarter turn leaves noise (`cos 90 deg` is not exactly zero) that would widen the
rectangle by one unit. Choice: floor(min + 0.001) and ceil(max - 0.001).

## 4. Which 32 lights

Section 2.4 keeps the first 32 lights submitted. The order of submission belongs to the
entity pass. The level viewer, which has no entity pass, keeps the object lights whose sphere
touches the view frustum, nearest to the camera first. `DynamicLightList` keeps the first 32
added, like the original.

## 5. Silhouette texture generation

Section 5.4 recommends an FBO. `ShadowRenderer::generate` renders at 2W x 2H into an FBO with
a tighter depth range than the original's (model z extent instead of +-99999; only the order
matters), so there is no window-size cropping. Output alpha follows section 5.2 exactly: at
most 102 of 255.

## 6. Sprite and mark draw details

* Sprite rotation and scale are applied about the sprite origin in the view plane
  (section 3.3); `scale` <= 0.001 means unscaled.
* Marks that touch no terrain (rectangle off the map) are dropped when added.
* Ground marks and shadows are drawn with the terrain triangulation of the terrain renderer,
  not the bilinear `heightAt`, so the decal lies exactly on the visible ground.

## 7. ENV_CHROME alpha

Section 4.3 gives the RGB of ENV_CHROME only. Choice: alpha = env alpha times colour alpha.
No shipped object uses the mode, so nothing renders differently.
