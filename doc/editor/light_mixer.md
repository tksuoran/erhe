# Light Mixer window

Stability: experimental

The Light Mixer window (`windows/light_mixer_window.*`, Window menu "Light
Mixer") lists every light of the active scene in one table, so the lights can
be balanced against each other without selecting them one at a time.

## Columns

| Column | Content |
|---|---|
| Light | The light's name; clicking selects the light. Greyed while mute or solo keeps the light from being shaded. |
| M | Mute toggle. |
| S | Solo toggle. |
| Type | `D`, `P` or `S` (directional, point, spot); the tooltip names it. |
| Intensity | `Light.intensity`, logarithmic slider over the property's range. |
| Color | `Light.color`, a swatch opening the color picker. |
| Range | `Light.range`; point and spot lights. |
| Inner, Outer | `Light.inner_spot_angle` / `outer_spot_angle` in degrees; spot lights. |

The sliders and the color picker write the light live while dragged and record
one undoable `Property_set_operation` per completed edit, as the Properties
window rows do. A write-sealed property's widget is disabled.

## Mute and solo

Mute and solo are session state, not part of the scene: they are the transient
editor flags `Editor_item_flags::light_muted` and `light_solo`
(`editor_item_bits.hpp`), never serialized and not on the undo stack.

- A muted light is not shaded and casts no shadow.
- While any light of the scene is solo, only the solo lights are shaded. The
  mute column is disabled and keeps its state, so clearing the last solo
  brings back the previous mute setup. A solo light is shaded even when it is
  also muted.
- "Unmute All" clears every mute; "Clear Solo" (shown while a light is solo)
  clears every solo.

The flags reach the renderer through `erhe::scene_renderer::Light_filter`
(`doc/erhe/scene_renderer.md` "Muted and solo lights"):
`get_light_count_limits()` (`app_settings.cpp`) puts the two masks into every
`Light_count_limits` the editor builds, so the scene's `Light_set` and the
light partition of the shader variants leave filtered lights out alike.
Changing a flag goes through `set_light_muted()` / `set_light_solo()`
(`light_mixer_window.hpp`), which call `Scene_root::on_light_changed()` so the
light sets re-resolve on their next prepare phase. Adding or removing a light
invalidates them as well, so removing the last solo light restores the mute
setup without a separate notification.

Mute and solo act on real-time shading only: the lightmap baker, the debug
light visualizations and the sky's sun direction read the scene's lights
directly and ignore them.

`get_scene_lights` (MCP) reports `shaded` per light, which reflects mute and
solo.
