# Persistent Item flag names

Several `ERHE_*` extensions carry erhe `Item_flags` as a JSON array of
names (`"flags"`, `"mesh_flags"`). Names, never raw bit values: bit
positions are not stable across erhe versions. Writers emit only names
from this set; readers ignore unknown names (so the set can grow) and
apply the listed set exactly - a listed flag is enabled, an unlisted
persistent flag is disabled, flags outside this set are untouched.

Defined names (source of truth:
`src/erhe/gltf/erhe_gltf/gltf_item_flags.cpp`):

```
no_message
no_transform_update
transform_world_normative
show_in_ui
show_debug_visualizations
shadow_cast
lock_viewport_selection
lock_viewport_transform
visible
invisible_parent
render_wireframe
render_bounding_volume
content
id
tool
brush
controller
rendertarget
expand
lock_edit
show_in_developer_ui
exclude_from_prefab
```

Deliberately absent: transient presentation state (`selected`,
`hovered_*`, `negative_determinant`, `affects_shadow`) and the
structurally handled `import_root`.
