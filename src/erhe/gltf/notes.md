# erhe_gltf

## Purpose
glTF file import and export using the fastgltf library. Loads .gltf/.glb files into
erhe scene objects (nodes, meshes, cameras, lights, materials, textures, animations, skins)
and exports erhe scene graphs back to glTF format.

## Key Types
- `Gltf_data` -- Container for all imported scene data: vectors of shared_ptr to animations, cameras, lights, meshes, skins, nodes, materials, textures, samplers.
- `Gltf_scan` -- Lightweight scan result listing names of all assets in a glTF file without fully loading them.
- `Gltf_parse_arguments` -- Parameters for `parse_gltf()`: graphics device, executor, image transfer, root node, mesh layer, file path.
- `Image_transfer` -- Manages GPU texture upload via a ring buffer for streaming image data.

## Public API
- `parse_gltf(arguments)` -- Load a glTF file and return populated `Gltf_data`.
- `scan_gltf(path)` -- Quick scan returning asset names without full parse.
- `export_gltf(root_node, binary)` -- Export a scene subtree to glTF/GLB string.
- `Image_transfer(device)` -- Create image upload manager.
- `Image_transfer::upload_to_texture(image_info, range, texture, gen_mipmap)` -- Upload image to GPU texture.

## Dependencies
- **erhe libraries:** `erhe::graphics` (private), `erhe::scene` (private), `erhe::primitive` (private), `erhe::geometry` (private), `erhe::file` (private), `erhe::log` (private), `erhe::profile` (private)
- **External:** fastgltf (conditionally, via `ERHE_GLTF_LIBRARY`), Taskflow (for parallel loading), fmt

## Notes
- Backend is selected at CMake time: `ERHE_GLTF_LIBRARY_FASTGLTF` or `ERHE_GLTF_LIBRARY_NONE`.
- `gltf.hpp` is a dispatch header that includes the appropriate backend.
- Uses `Image_transfer` with a ring buffer for asynchronous texture uploads.
