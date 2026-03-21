# erhe_codegen — Python C++ Struct Code Generator

## Overview

A Python code generator that produces C++ structs with versioned JSON serialization, deserialization, and rich reflection from Python definitions.

## Features

- **Struct declarations** with typed fields and default values
- **Enum declarations** with per-value metadata and string conversion
- **JSON serialization** via direct string-building (pretty-printed with 4-space indentation)
- **JSON deserialization** via simdjson on-demand API
- **Schema versioning** — fields declare `added_in` / `removed_in`; deserialization handles all past versions
- **Migration callbacks** — run user code when deserializing older versions
- **Rich reflection** — field name, type, offset, size, version range, descriptions, numeric limits, path, enum info

## Generated Files

For each struct, three files are generated:

| File | Contents |
|------|----------|
| `foo.hpp` | Struct definition only (no simdjson dependency) |
| `foo_serialization.hpp` | `serialize()`, `deserialize()`, `get_fields()`, `get_struct_info()` declarations |
| `foo.cpp` | Implementations of all above functions |

For each enum, two files: `foo.hpp` (enum + `to_string`/`from_string`) and `foo.cpp` (implementations + reflection).

The split allows libraries to use the struct type without depending on simdjson.

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
| `EnumRef("Name")` | `Name` | string |

## Python Definition API

```python
from erhe_codegen import *

struct("Material",
    field("name",     String, added_in=1, default='""',
          short_desc="Material name"),
    field("metallic", Float,  added_in=1, default="0.0f",
          ui_min="0.0f", ui_max="1.0f",
          hard_min="0.0f", hard_max="1.0f"),
    field("color",    StructRef("Color"), added_in=1),
    field("mode",     EnumRef("Blend_mode"), added_in=2),
    field("opacity",  Float,  added_in=2, removed_in=3),
    version=3,
)

enum("Blend_mode",
    value("opaque",       0, short_desc="Fully opaque"),
    value("translucent",  1),
    value("additive",     2),
    underlying_type=Int,
)
```

### Field Options

| Option | Type | Description |
|--------|------|-------------|
| `added_in` | `int` | Version when field was added (required) |
| `removed_in` | `int` | Version when field was removed (optional) |
| `default` | `str` | C++ default initializer value |
| `short_desc` | `str` | Brief description for UI/docs |
| `long_desc` | `str` | Detailed description |
| `path` | `str` | Grouping path for UI (default: `""`) |
| `ui_min`, `ui_max` | `str` | UI slider/drag range (numeric types only) |
| `hard_min`, `hard_max` | `str` | Hard clamp range (numeric types only) |

## CMake Integration

The `erhe_codegen_generate()` function runs the generator at configure time and adds a build-time custom command that reruns when definition files change.

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

All definition files must be listed explicitly in `DEFINITIONS` (no globbing). They appear in the IDE and trigger regeneration on change.

## Runtime Dependencies

- **Serialization**: `erhe_codegen` library (`serialize_helpers.hpp/cpp`, `migration.hpp/cpp`, `field_info.hpp`)
- **Deserialization**: simdjson (linked via `erhe::codegen` CMake target)
- **Struct headers only**: no runtime dependencies (pure POD)

## Command Line Usage

```bash
py -3 src/erhe/codegen/generate.py <definitions_dir> <output_dir>
```
