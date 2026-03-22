# erhe_codegen — Python C++ Struct Code Generator

## Overview

A Python code generator that produces C++ structs with versioned JSON serialization, deserialization, and rich reflection from Python definitions.

## Features

- **Struct declarations** with typed fields, default values, and descriptions
- **Enum declarations** with per-value metadata and string conversion
- **JSON serialization** via direct string-building (pretty-printed with 4-space indentation)
- **JSON deserialization** via simdjson on-demand API
- **Schema versioning** — fields declare `added_in` / `removed_in`; deserialization handles all past versions
- **Migration callbacks** — run user code when deserializing older versions
- **Rich reflection** — field name, type enum, offset, size, version range, descriptions, numeric limits, path, visibility, enum info
- **Struct metadata** — struct name, version, short/long descriptions

## Generated Files

For each struct, three files are generated:

| File | Contents |
|------|----------|
| `foo.hpp` | Struct definition only (no simdjson dependency) |
| `foo_serialization.hpp` | `serialize()`, `deserialize()`, `get_fields()`, `get_struct_info()` declarations |
| `foo.cpp` | Implementations of all above functions, plus reflection tables |

For each enum, two files: `foo.hpp` (enum class + `to_string`/`from_string` declarations) and `foo.cpp` (implementations + `Enum_info` reflection).

The header split allows libraries to use the struct type without depending on simdjson. This is important when libraries compile with different arch flags (e.g. Jolt compatibility) that cause simdjson to select different implementation namespaces (haswell vs fallback).

## Supported Types

| Python | C++ | JSON |
|--------|-----|------|
| `Bool` | `bool` | `true`/`false` |
| `Int`, `Int8`..`Int64` | `int`, `int8_t`..`int64_t` | number |
| `UInt`, `UInt8`..`UInt64` | `unsigned int`, `uint8_t`..`uint64_t` | number |
| `Float` | `float` | number |
| `Double` | `double` | number |
| `String` | `std::string` | string |
| `Vec2`, `Vec3`, `Vec4` | `glm::vec2/3/4` | array of floats |
| `IVec2` | `glm::ivec2` | array of ints |
| `Mat4` | `glm::mat4` | array of 16 floats |
| `Vector(T)` | `std::vector<T>` | array |
| `Array(T, N)` | `std::array<T, N>` | array |
| `Optional(T)` | `std::optional<T>` | value or `null` |
| `StructRef("Name")` | `Name` | nested object |
| `EnumRef("Name")` | `Name` | string (enumerator name) |

## Python Definition API

All `struct()` arguments are keyword-only. `version`, `short_desc`, and `long_desc` come before `fields` for readability.

```python
from erhe_codegen import *

struct("Material",
    version=3,
    short_desc="Material",
    long_desc="PBR material definition",
    fields=[
        field(
            "name",
            String,
            added_in=1,
            default='""',
            short_desc="Material name",
            long_desc="Human-readable display name",
            visible=True
        ),
        field(
            "metallic",
            Float,
            added_in=1,
            default="0.0f",
            short_desc="Metallic",
            long_desc="0 = dielectric, 1 = metal",
            visible=True,
            ui_min="0.0f",
            ui_max="1.0f",
            hard_min="0.0f",
            hard_max="1.0f"
        ),
        field(
            "color",
            StructRef("Color"),
            added_in=1,
            short_desc="Color",
            long_desc="",
            visible=True
        ),
        field(
            "mode",
            EnumRef("Blend_mode"),
            added_in=2,
            short_desc="Blend Mode",
            long_desc="",
            visible=True
        ),
        field(
            "opacity",
            Float,
            added_in=2,
            removed_in=3,
            short_desc="Opacity",
            long_desc="Deprecated, replaced by alpha",
            visible=False
        ),
    ],
)

enum("Blend_mode",
    value("opaque",       0, short_desc="Fully opaque"),
    value("translucent",  1),
    value("additive",     2),
    underlying_type=Int,
)
```

### Struct Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `version` | `int` | required | Current schema version |
| `short_desc` | `str` | `None` | Brief name for UI display |
| `long_desc` | `str` | `None` | Detailed description |
| `fields` | `list` | required | List of `field()` definitions |

### Field Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `added_in` | `int` | required | Version when field was added |
| `removed_in` | `int` | `None` | Version when field was removed |
| `default` | `str` | `None` | C++ default initializer value |
| `short_desc` | `str` | `None` | Brief label for UI |
| `long_desc` | `str` | `None` | Tooltip / detailed description |
| `path` | `str` | `""` | Grouping path for UI |
| `visible` | `bool` | `True` | Whether field appears in UI |
| `ui_min`, `ui_max` | `str` | `None` | UI slider/drag range (numeric only) |
| `hard_min`, `hard_max` | `str` | `None` | Hard clamp range (numeric only) |

## C++ Reflection API

### Field_type enum

`erhe::codegen::Field_type` provides a type-safe enum for dispatching on field types without string comparison:

```
bool_, int_, unsigned_int, int8..int64, uint8..uint64,
float_, double_, string, vec2, vec3, vec4, ivec2, mat4,
vector, array, optional, struct_ref, enum_ref
```

### Field_info struct

Each field has rich metadata accessible at runtime:

- `name`, `type_name` — field and C++ type names
- `field_type` — `Field_type` enum for type-safe dispatch
- `offset`, `size` — for pointer arithmetic from struct base
- `added_in`, `removed_in` — version range
- `short_desc`, `long_desc` — UI label and tooltip
- `path` — grouping path for UI organization
- `visible` — whether to show in UI
- `numeric_limits` — ui_min/max, hard_min/max with has_* flags
- `is_numeric`, `is_enum` — convenience booleans
- `enum_info` — pointer to `Enum_info` for enum fields

### Struct_info struct

- `name` — struct type name
- `version` — current schema version
- `short_desc`, `long_desc` — UI display name and description
- `fields` — `std::span<const Field_info>`

### Access functions

```cpp
auto get_struct_info(const T*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const T*) -> std::span<const erhe::codegen::Field_info>;
```

Pass a null pointer of the struct type: `get_fields(static_cast<const MyStruct*>(nullptr))`

## CMake Integration

`erhe_codegen_generate()` runs the generator at configure time and adds a build-time custom command that reruns when any `.py` file changes.

```cmake
erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/definitions"
    OUTPUT_DIR      "${CMAKE_CURRENT_BINARY_DIR}/generated"
    DEFINITIONS
        "${CMAKE_CURRENT_SOURCE_DIR}/definitions/material.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/definitions/color.py"
)
```

All definition files must be listed explicitly in `DEFINITIONS` (no globbing). They appear in the IDE under "codegen definitions" source group and trigger regeneration on change. Generator source files are also tracked as dependencies.

## Architecture Notes

- **Header split**: `foo.hpp` has no simdjson dependency (pure POD struct). `foo_serialization.hpp` adds simdjson-dependent declarations. This is critical for cross-library use where different targets may compile simdjson with different arch flags.
- **Serialization**: Direct string concatenation, no intermediate DOM. Pretty-printed with configurable indent level.
- **Deserialization**: simdjson on-demand API. Version field is read first, then fields are deserialized with version guards. Missing fields use struct defaults.
- **Migration**: `erhe::codegen::register_migration<T>(callback)` registers a callback invoked when deserializing an older version. The callback receives the struct, old version, and new version.
- **Reflection**: Static const arrays in the `.cpp` file, accessed via `get_fields()` / `get_struct_info()`. Zero heap allocation.

## Command Line Usage

```bash
py -3 src/erhe/codegen/generate.py <definitions_dir> <output_dir>
```
