# Plan: Serialize/Deserialize Editor::Scene_root

> **First action:** Copy this plan to `doc/scene_serialization_plan.md` in the repository.

## Context

The editor currently has no way to save and reload scenes. The goal is to introduce a two-layer serialization scheme:

1. **erhe scene .json** - A new JSON format for the node hierarchy, cameras, lights, physics config, and references to external assets. Uses `erhe_codegen`-generated structs for all data types.
2. **glTF references** - Meshes, materials, and animations are stored/referenced via glTF files. When glTF is imported, each erhe resource records a reference back to the specific item in the specific glTF file it came from. The scene .json serializes these references.

Nodes are NOT stored in glTF. glTF can still be imported/exported separately, but the authoritative node hierarchy lives in the erhe scene .json.

File layout: flat alongside project, relative paths for glTF references.

---

## Step 1: Add glTF Source Reference Tracking

**Goal:** When glTF is imported, each created erhe resource (Mesh/Primitive, Material, Animation, Skin, Texture) remembers where it came from.

### 1a. Define a `Gltf_source_reference` struct

New codegen definition file: `src/editor/scene/definitions/gltf_source_reference.py`

### 1b. Store the reference on erhe Items

Add `std::optional<Gltf_source_reference>` to `Content_library_node` since all library items (materials, meshes, animations, skins, textures) are stored as `Content_library_node` children.

### 1c. Populate references during glTF import

Modify `src/editor/parsers/gltf.cpp` `import_gltf()`: after importing each mesh/material/animation/skin/texture from `Gltf_data`, set the `gltf_source` on the corresponding `Content_library_node` with the path and index.

---

## Step 2: Define Codegen Structs for Scene Data

**Goal:** Create Python codegen definitions for all serializable scene data types.

New directory: `src/editor/scene/definitions/` containing:
- `gltf_source_reference.py`
- `transform_data.py`
- `projection_data.py`
- `camera_data.py`
- `light_data.py`
- `node_data.py`
- `mesh_reference.py`
- `scene_file.py`

---

## Step 3: Scene Serialization (Save)

New file: `src/editor/scene/scene_serialization.hpp/.cpp`

Functions:
- `serialize_scene_root()` - Walks the scene graph and populates the codegen struct
- `save_scene()` - Calls serialize, writes to file

---

## Step 4: Scene Deserialization (Load)

Extend `src/editor/scene/scene_serialization.cpp`

Functions:
- `load_scene()` - Reads file, parses with simdjson, calls deserialize()
- `deserialize_scene_root()` - Reconstructs the live scene

---

## Step 5: Integrate into Editor

- Add Save/Load UI (menu items or keyboard shortcuts)
- Wire into `App_scenes`

---

## Key Files Summary

| File | Action |
|------|--------|
| `src/editor/scene/definitions/*.py` | Create (8 files) |
| `src/editor/scene/scene_serialization.hpp` | Create |
| `src/editor/scene/scene_serialization.cpp` | Create |
| `src/editor/content_library/content_library.hpp` | Modify - add `gltf_source` to `Content_library_node` |
| `src/editor/parsers/gltf.cpp` | Modify - populate `gltf_source` during import |
| `src/editor/CMakeLists.txt` | Modify - add codegen generation + new source files |
| `src/editor/operations/operations_window.hpp` | Modify - add save/load scene commands |
| `src/editor/operations/operations_window.cpp` | Modify - implement save/load scene UI |

## Implementation Status

- [x] Step 1: glTF source reference tracking (codegen struct, Content_library_node field, gltf.cpp population)
- [x] Step 2: Codegen struct definitions (8 Python files + 2 enum files, 20 generated C++ files)
- [x] Step 3: Scene serialization / save (`scene_serialization.cpp`)
- [x] Step 4: Scene deserialization / load (`scene_serialization.cpp`)
- [x] Step 5: Editor integration (Save/Load Scene menu commands in Operations window)
- [x] Mesh re-import from glTF references during load
- [x] Dual-format mesh serialization: Geometry-normative meshes saved as geogram `.meshb`, glTF-normative meshes saved to companion `.glb`
- [x] Material persistence: Companion `.glb` always exported so materials are preserved for both mesh types
- [x] Node_physics serialization: motion mode, friction, restitution, damping, mass; collision shape re-derived from mesh geometry on load

## Dual Mesh Serialization

- **Geometry-normative**: Mesh has `erhe::geometry::Geometry` (scene-built or manipulated). Saved as `.geogram` files (one per primitive). Preserves polygon structure. Materials stored in companion `.glb`.
- **glTF-normative**: Mesh has only `Triangle_soup` (glTF import, unmodified). Saved to companion `.glb` file. Preserves triangle mesh as-is.
- Detection: if any primitive in a mesh has `render_shape->get_geometry_const()` non-null, the mesh is geometry-normative.
- `Mesh_reference` codegen struct (v2): added `source_type` (enum), `geometry_path`, `mesh_name`, `primitive_count`.
- `Mesh_source_type` enum: `geometry` (0), `gltf` (1).
- File naming: `{scene_stem}_mesh_{N}_p{P}.geogram` for geometry primitives.
- Companion `.glb` is always exported when any meshes exist, storing materials (and glTF-normative mesh data).
- On load, geometry-normative mesh materials are resolved from the companion `.glb` via name lookup, then added to content library.

## Node Physics Serialization

- `Node_physics_data` codegen struct: `node_id`, `motion_mode`, `friction`, `restitution`, `linear_damping`, `angular_damping`, `mass` (optional), `density` (optional), `enable_collisions`.
- `Motion_mode_serial` enum: `e_static` (1), `e_kinematic_non_physical` (2), `e_kinematic_physical` (3), `e_dynamic` (4).
- `Scene_file` v2: added `node_physics` field (`Vector(Node_physics_data)`).
- On save: iterates all nodes, serializes `Node_physics` attachment properties. Mass read from rigid body if active.
- On load: creates `Node_physics` attachments. Collision shape is re-derived as convex hull from the mesh geometry attached to the same node (same approach as `Brush`). Falls back to empty shape if no geometry available.
- Collision shape parameters (box extents, sphere radius, etc.) are NOT serialized - convex hull is always used on load. This is acceptable for geometry-normative meshes where the original geometry is available.

## Verification

- [x] Build verified successfully (2026-03-18)
- [ ] Round-trip test: save scene, load it, verify preservation
- [ ] Dual-format round-trip: scene with both geometry and glTF meshes

User verifies builds manually. Claude should not run build commands.

## Build Issues Resolved

- `erhe::primitive::Material` needed explicit `#include "erhe_primitive/material.hpp"` in scene_serialization.cpp (forward declaration insufficient for `Item_base*` conversion)
- `erhe_codegen_generate()` CMake function: changed from `add_custom_command` (build-time) to `execute_process` (configure-time) to avoid Windows "Open with" dialog for .py files
- `Python3_EXECUTABLE` not available in function scope: cached as `ERHE_PYTHON3_EXECUTABLE` (CACHE INTERNAL)
- simdjson ABI mismatch: Jolt Physics PUBLIC `/arch:AVX2` caused `simdjson::haswell` vs `simdjson::fallback` namespace mismatch between editor and erhe_codegen library. Fixed by making all `deserialize_field` overloads `inline` in header.
- Enum `const` internal linkage: `const` at namespace scope in C++ has internal linkage; added `extern` to enum_info definitions.
