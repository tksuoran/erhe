# erhe_gltf -- Code Review

## Summary
A glTF import/export library built on fastgltf, supporting meshes, materials, textures, cameras, lights, animations, and skins. The import pipeline is comprehensive and handles the full glTF spec well. The `Image_transfer` class provides clean GPU upload via ring buffers. The main concern is the very large monolithic implementation file.

## Strengths
- Comprehensive glTF feature support including animations, skins, morph targets, and KHR_lights_punctual
- `Gltf_scan` provides a lightweight scan mode that extracts metadata without full parsing -- useful for UI previews
- `Image_transfer` cleanly encapsulates the texture upload pipeline with proper buffer management
- Good use of `fastgltf` library with proper error handling via `std::visit` on variants
- Correct handling of accessor normalization and component type mapping
- Proper separation between data structures (`Gltf_data`, `Gltf_scan`) and the parsing functions

## Issues
- **[moderate]** `gltf_fastgltf.cpp` is likely very large (the first 120 lines are just includes and helper functions). Large monolithic files are harder to maintain and slower to compile. Consider splitting into separate files for import, export, and mesh processing.
- **[moderate]** `is_float()` (gltf_fastgltf.cpp:62) is missing a return type annotation -- it uses `auto` without `-> bool`, which compiles but is less readable and may confuse tools.
- **[moderate]** The local `c_str(Vertex_attribute_usage)` function (gltf_fastgltf.cpp:78-95) duplicates `erhe::dataformat::c_str()` from vertex_format.cpp. This is redundant.
- **[minor]** `gltf_fastgltf.hpp:89` is missing the closing namespace comment `// namespace erhe::gltf`.
- **[minor]** `gltf_none.cpp`/`gltf_none.hpp` presumably provide stub implementations when glTF support is disabled -- this backend pattern is good but should be documented.
- **[minor]** The `mat4_yup_from_zup` matrix (gltf_fastgltf.cpp:55-60) is file-scope `constexpr` which is fine, but it's unclear when/if it's used since glTF specifies Y-up.

## Suggestions
- Split `gltf_fastgltf.cpp` into smaller files (e.g., `gltf_import.cpp`, `gltf_export.cpp`, `gltf_mesh.cpp`).
- Remove the duplicate `c_str()` function and use `erhe::dataformat::c_str()` directly.
- Add explicit return type `-> bool` to `is_float()`.
- Consider supporting glTF extensions like KHR_materials_unlit, KHR_texture_transform if not already handled.
- Add error reporting for unsupported glTF features rather than silently ignoring them.
