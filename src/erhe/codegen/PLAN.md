# erhe_codegen — Python-Based C++ Struct Code Generator

## Goal

A unidirectional Python code generator that takes struct definitions written in Python and produces C++ headers/sources with:

- Struct declarations with default values
- Enum declarations with per-value metadata
- Versioned JSON serialization (to simdjson-compatible format, using simdjson for deserialization)
- Versioned JSON deserialization (using simdjson on-demand API)
- Rich reflection (field name, type, offset, version range, descriptions, numeric limits, enum value info)

## Design Principles

- **Unidirectional**: Python definitions are the source of truth. No C++ parsing.
- **Simple**: Python definitions should be concise and readable.
- **POD-first**: Start with plain data types, composable structs, `std::vector<T>`, `std::array<T, N>`.
- **simdjson**: Use simdjson on-demand API for deserialization. For serialization, generate direct string-building code (simdjson is read-only).
- **Versioned**: Every serialized struct carries a version number. Fields declare when they were added/removed. Deserialization handles all past versions gracefully.

---

## 1. Python Definition API

Struct definitions live in `.py` files under a `definitions/` directory (or passed as arguments).

```python
# example: definitions/material.py
from erhe_codegen import *

struct("Material",
    field("name",             String,
        added_in=1,
        short_desc="Material name",
        long_desc="Human-readable display name for this material",
        default='""',
    ),
    field("albedo",           Vec4,
        added_in=1,
        short_desc="Albedo color",
        long_desc="Base color in linear RGBA",
        default="0.5f, 0.5f, 0.5f, 1.0f",
    ),
    field("metallic",         Float,
        added_in=1,
        short_desc="Metallic factor",
        long_desc="0 = dielectric, 1 = metal",
        default="0.0f",
        ui_min="0.0f", ui_max="1.0f",
        hard_min="0.0f", hard_max="1.0f",
    ),
    field("roughness",        Float,
        added_in=1,
        short_desc="Roughness factor",
        default="0.5f",
        ui_min="0.0f", ui_max="1.0f",
        hard_min="0.0f", hard_max="1.0f",
    ),
    field("emissive",         Vec3,
        added_in=2,
        short_desc="Emissive color",
        long_desc="Linear RGB emissive contribution",
        default="0.0f, 0.0f, 0.0f",
    ),
    field("texture_paths",    Vector(String),
        added_in=1,
        short_desc="Texture paths",
    ),
    field("opacity",          Float,
        added_in=2, removed_in=3,
        short_desc="Opacity (deprecated)",
        long_desc="Replaced by albedo alpha channel in v3",
        default="1.0f",
        ui_min="0.0f", ui_max="1.0f",
        hard_min="0.0f", hard_max="1.0f",
    ),
    field("uv_scales",        Array(Float, 4),
        added_in=3,
        short_desc="UV channel scales",
        default="1.0f, 1.0f, 1.0f, 1.0f",
    ),
    version=3,
)
```

### 1.0.1 Enum Definitions

Enums are defined with `enum()` and `value()` calls. Each value has metadata for UI display.

```python
# example: definitions/blend_mode.py
from erhe_codegen import *

enum("Blend_mode",
    value("opaque",       0, short_desc="Opaque",        long_desc="No transparency"),
    value("alpha_blend",  1, short_desc="Alpha Blend",   long_desc="Standard alpha blending"),
    value("additive",     2, short_desc="Additive",      long_desc="Additive blending for glow effects"),
    value("multiply",     3, short_desc="Multiply",      long_desc="Multiplicative blending"),
    underlying_type=UInt8,
)
```

Enum values can then be used as struct field types via `EnumRef("Blend_mode")`:

```python
struct("Material",
    # ... other fields ...
    field("blend_mode",  EnumRef("Blend_mode"),
        added_in=4,
        short_desc="Blend mode",
        default="Blend_mode::opaque",
    ),
    version=4,
)
```

#### Enum Definition Options

| Parameter         | Required | Type   | Description                                       |
|-------------------|----------|--------|---------------------------------------------------|
| `underlying_type` | no       | type   | C++ underlying type (default: `Int`→`int`)        |

Only integer types are valid as `underlying_type` (`Int8`, `UInt8`, `Int16`, `UInt16`, `Int32`, `UInt32`, `Int`, `UInt`, `Int64`, `UInt64`).

#### Enum Value Options

| Parameter    | Required | Type | Description                                     |
|--------------|----------|------|-------------------------------------------------|
| (positional) | **yes**  | str  | C++ enumerator name                             |
| (positional) | **yes**  | int  | Numeric value (must be explicit, no auto-assign) |
| `short_desc` | no       | str  | Short human-readable name (for UI dropdowns)    |
| `long_desc`  | no       | str  | Longer description (for tooltips)               |

#### Serialization Format

Enums are serialized as **strings** (the enumerator name). This makes JSON human-readable and resilient to value renumbering:

```json
{
    "_version": 4,
    "blend_mode": "alpha_blend"
}
```

Deserialization matches the string against known enumerator names. Unknown strings leave the field at its default and are not treated as errors (forward compatibility).

### 1.1 Built-in Type Map

| Python Name | C++ Type              | simdjson Access         |
|-------------|-----------------------|-------------------------|
| `Bool`      | `bool`                | `.get_bool()`           |
| `Int`       | `int`                 | `.get_int64()`          |
| `UInt`      | `unsigned int`        | `.get_uint64()`         |
| `Int8`      | `int8_t`              | `.get_int64()` + cast   |
| `UInt8`     | `uint8_t`             | `.get_uint64()` + cast  |
| `Int16`     | `int16_t`             | `.get_int64()` + cast   |
| `UInt16`    | `uint16_t`            | `.get_uint64()` + cast  |
| `Int32`     | `int32_t`             | `.get_int64()` + cast   |
| `UInt32`    | `uint32_t`            | `.get_uint64()` + cast  |
| `Int64`     | `int64_t`             | `.get_int64()`          |
| `UInt64`    | `uint64_t`            | `.get_uint64()`         |
| `Float`     | `float`               | `.get_double()` + cast  |
| `Double`    | `double`              | `.get_double()`         |
| `String`    | `std::string`         | `.get_string()`         |
| `Vec2`      | `glm::vec2`           | array of 2 floats       |
| `Vec3`      | `glm::vec3`           | array of 3 floats       |
| `Vec4`      | `glm::vec4`           | array of 4 floats       |
| `Mat4`      | `glm::mat4`           | array of 16 floats      |

### 1.2 Composite Types

| Python Expression          | C++ Type                      |
|----------------------------|-------------------------------|
| `Vector(T)`                | `std::vector<T>`              |
| `Array(T, N)`              | `std::array<T, N>`            |
| `StructRef("Foo")`         | `Foo` (value, nested struct)  |
| `Vector(StructRef("Foo"))` | `std::vector<Foo>`            |
| `EnumRef("Bar")`           | `Bar` (enum type)             |
| `Vector(EnumRef("Bar"))`   | `std::vector<Bar>`            |

### 1.3 Field Options

| Option        | Required | Type   | Description                                                |
|---------------|----------|--------|------------------------------------------------------------|
| `added_in`    | **yes**  | int    | Struct version in which this field first appeared           |
| `removed_in`  | no       | int    | Struct version in which this field was removed (exclusive)  |
| `default`     | no       | str    | C++ default member initializer expression (verbatim)       |
| `short_desc`  | no       | str    | Short human-readable description (for UI labels/tooltips)  |
| `long_desc`   | no       | str    | Longer description (for documentation / detailed tooltips) |
| `ui_min`      | no       | str    | Recommended minimum for UI sliders/inputs (numeric only)   |
| `ui_max`      | no       | str    | Recommended maximum for UI sliders/inputs (numeric only)   |
| `hard_min`    | no       | str    | Absolute minimum, values clamped on deserialization         |
| `hard_max`    | no       | str    | Absolute maximum, values clamped on deserialization         |
| `comment`     | no       | str    | Code comment emitted next to the field                     |

**Validation rules** (enforced by the generator):

- `added_in` must be >= 1 and <= struct `version`.
- `removed_in`, if set, must be > `added_in` and <= struct `version` + 1.
- `ui_min`, `ui_max`, `hard_min`, `hard_max` are only valid on numeric scalar types (`Int*`, `UInt*`, `Float`, `Double`).
- The struct's `version` is the current (latest) version number. It must be >= 1.

### 1.4 Field Lifetime

A field is **active** in struct version `v` when `added_in <= v` and (`removed_in` is not set or `v < removed_in`).

- Serialization: only writes fields active in the current (latest) version.
- Deserialization: reads the `_version` from JSON, then for each field, only attempts to read it if the field was active in that version. Fields not present (because they didn't exist in that version, or were already removed) keep their C++ default.
- Removed fields still exist in the C++ struct (with their default) so that migration code can reference them. They are marked `[[deprecated]]` in the generated header.

---

## 2. Versioning

### 2.1 Serialized Format

Every serialized struct includes a `_version` key at the top level:

```json
{
    "_version": 3,
    "name": "Brick",
    "albedo": [0.8, 0.3, 0.2, 1.0],
    "metallic": 0.0,
    "roughness": 0.7,
    "emissive": [0.0, 0.0, 0.0],
    "texture_paths": ["textures/brick_diffuse.png"],
    "uv_scales": [1.0, 1.0, 1.0, 1.0]
}
```

Note: `opacity` is absent because it was removed in v3.

### 2.2 Deserialization of Older Versions

When deserializing version 2 data:

```json
{
    "_version": 2,
    "name": "Brick",
    "albedo": [0.8, 0.3, 0.2, 1.0],
    "metallic": 0.0,
    "roughness": 0.7,
    "emissive": [0.0, 0.0, 0.0],
    "opacity": 0.9,
    "texture_paths": ["textures/brick_diffuse.png"]
}
```

The deserializer:
1. Reads `_version` = 2.
2. For `name` (added_in=1): active in v2, reads it.
3. For `emissive` (added_in=2): active in v2, reads it.
4. For `opacity` (added_in=2, removed_in=3): active in v2, reads it.
5. For `uv_scales` (added_in=3): **not active** in v2, skips — keeps default `{1,1,1,1}`.
6. Returns the populated struct. Caller can optionally check `_version < current` and run migration logic.

### 2.3 Migration Callbacks

Users can register migration callbacks at runtime to transform data when deserializing older versions. The generated deserializer calls registered callbacks after reading all fields, giving the callback access to the fully populated struct (including deprecated fields that were active in the old version).

#### Registration API

```cpp
// include/erhe_codegen/migration.hpp
#pragma once

#include <cstdint>
#include <functional>

namespace erhe::codegen {

// Callback signature: receives the struct, the version found in JSON, and the
// current (latest) version. Returns true if migration succeeded.
template <typename T>
using Migration_callback = std::function<auto(T& value, uint32_t old_version, uint32_t new_version) -> bool>;

// Register a migration callback for a struct type. Multiple callbacks can be
// registered; they are called in registration order.
template <typename T>
void register_migration(Migration_callback<T> callback);

// Called by generated deserialize(). Returns true if all callbacks succeeded.
// Not intended to be called by user code directly.
template <typename T>
auto run_migrations(T& value, uint32_t old_version, uint32_t new_version) -> bool;

} // namespace erhe::codegen
```

#### Implementation

```cpp
// src/migration.hpp (internal)
#include <erhe_codegen/migration.hpp>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <any>

namespace erhe::codegen {

// Type-erased storage for per-type callback lists
struct Migration_registry
{
    std::mutex                                                     mutex;
    std::unordered_map<std::type_index, std::vector<std::any>>     callbacks;

    static auto instance() -> Migration_registry&
    {
        static Migration_registry reg;
        return reg;
    }
};

template <typename T>
void register_migration(Migration_callback<T> callback)
{
    auto& reg = Migration_registry::instance();
    std::lock_guard lock{reg.mutex};
    reg.callbacks[std::type_index(typeid(T))].push_back(std::move(callback));
}

template <typename T>
auto run_migrations(T& value, uint32_t old_version, uint32_t new_version) -> bool
{
    if (old_version >= new_version) return true; // no migration needed

    auto& reg = Migration_registry::instance();
    std::lock_guard lock{reg.mutex};

    auto it = reg.callbacks.find(std::type_index(typeid(T)));
    if (it == reg.callbacks.end()) return true; // no callbacks registered

    for (const auto& cb_any : it->second) {
        const auto& cb = std::any_cast<const Migration_callback<T>&>(cb_any);
        if (!cb(value, old_version, new_version)) {
            return false;
        }
    }
    return true;
}

} // namespace erhe::codegen
```

#### User Example

```cpp
// In application initialization code:
#include "material.hpp"
#include <erhe_codegen/migration.hpp>

void register_material_migrations()
{
    erhe::codegen::register_migration<Material>(
        [](Material& mat, uint32_t old_version, uint32_t new_version) -> bool
        {
            // v2 → v3: opacity was removed, fold it into albedo alpha
            if (old_version < 3) {
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
                mat.albedo.a = mat.opacity;
                #pragma GCC diagnostic pop
            }
            return true;
        }
    );
}
```

#### Generated Deserialization with Migration

The generated `deserialize()` function calls `run_migrations()` when the data version differs from the current version:

```cpp
auto deserialize(simdjson::ondemand::object obj, Material& out) -> simdjson::error_code
{
    uint64_t version = 1;
    {
        simdjson::ondemand::value v;
        if (!obj["_version"].get(v)) {
            v.get_uint64().get(version);
        }
    }

    // ... read all fields with version guards (as before) ...

    // Run user-registered migration callbacks if version differs
    if (version != Material::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Material::current_version
        );
    }

    return simdjson::SUCCESS;
}
```

The migration callback receives the struct with all version-appropriate fields populated:
- Fields active in the old version have their deserialized values (including deprecated fields like `opacity`).
- Fields not yet active in the old version have their C++ defaults.
- The callback can then transform deprecated field values into new field values.

### 2.4 Missing `_version` Key

If `_version` is absent, the deserializer assumes version 1. This handles legacy data that predates the versioning system.

### 2.5 Generated Deserialization with Versioning

```cpp
auto deserialize(simdjson::ondemand::object obj, Material& out) -> simdjson::error_code
{
    // Read version, default to 1 if missing
    uint64_t version = 1;
    {
        simdjson::ondemand::value v;
        if (!obj["_version"].get(v)) {
            v.get_uint64().get(version);
        }
    }

    simdjson::ondemand::value val;

    // name: added_in=1
    if (version >= 1) {
        if (!obj["name"].get(val)) {
            deserialize_field(val, out.name);
        }
    }

    // emissive: added_in=2
    if (version >= 2) {
        if (!obj["emissive"].get(val)) {
            deserialize_field(val, out.emissive);
        }
    }

    // opacity: added_in=2, removed_in=3
    if (version >= 2 && version < 3) {
        if (!obj["opacity"].get(val)) {
            deserialize_field(val, out.opacity);
        }
    }

    // uv_scales: added_in=3
    if (version >= 3) {
        if (!obj["uv_scales"].get(val)) {
            deserialize_field(val, out.uv_scales);
        }
    }

    // Run user-registered migration callbacks if version differs
    if (version != Material::current_version) {
        erhe::codegen::run_migrations(
            out,
            static_cast<uint32_t>(version),
            Material::current_version
        );
    }

    return simdjson::SUCCESS;
}
```

### 2.6 Generated Serialization with Versioning

Serialization always writes the current (latest) version. Only fields active in the current version are written:

```cpp
auto serialize(const Material& value) -> std::string
{
    std::string out;
    out += '{';
    out += "\"_version\":3,";
    // name: active in current version (added=1, no removal)
    out += "\"name\":";  serialize_string(out, value.name);  out += ',';
    // opacity: NOT active in current version (removed_in=3) — skip
    // uv_scales: active (added=3)
    out += "\"uv_scales\":"; serialize_array(out, value.uv_scales); out += ',';
    // ...
    if (out.back() == ',') out.back() = '}'; else out += '}';
    return out;
}
```

---

## 3. Reflection

### 3.1 Field_info

```cpp
// include/erhe_codegen/field_info.hpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace erhe::codegen {

struct Numeric_limits
{
    double ui_min;
    double ui_max;
    double hard_min;
    double hard_max;
    bool   has_ui_min;
    bool   has_ui_max;
    bool   has_hard_min;
    bool   has_hard_max;
};

struct Enum_value_info
{
    const char* name;        // C++ enumerator name (used as serialization key)
    int64_t     value;       // Numeric value
    const char* short_desc;  // Short UI name (e.g. "Alpha Blend"), nullptr if not set
    const char* long_desc;   // Longer description, nullptr if not set
};

struct Enum_info
{
    const char*                     name;
    const char*                     underlying_type_name;
    std::span<const Enum_value_info> values;
};

struct Field_info
{
    const char*    name;
    const char*    type_name;
    std::size_t    offset;
    std::size_t    size;
    uint32_t       added_in;
    uint32_t       removed_in;    // 0 = not removed
    const char*    short_desc;    // nullptr if not set
    const char*    long_desc;     // nullptr if not set
    const char*    default_value; // nullptr if not set (string repr of the default)
    Numeric_limits numeric_limits;
    bool           is_numeric;
    bool           is_enum;
    const Enum_info* enum_info;   // non-null if is_enum == true
};

struct Struct_info
{
    const char*                name;
    uint32_t                   version;
    std::span<const Field_info> fields;
};

} // namespace erhe::codegen
```

### 3.2 Generated Reflection Table

```cpp
static constexpr erhe::codegen::Field_info material_fields[] = {
    {
        .name          = "name",
        .type_name     = "std::string",
        .offset        = offsetof(Material, name),
        .size          = sizeof(std::string),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Material name",
        .long_desc     = "Human-readable display name for this material",
        .default_value = "\"\"",
        .numeric_limits = {},
        .is_numeric    = false,
    },
    {
        .name          = "metallic",
        .type_name     = "float",
        .offset        = offsetof(Material, metallic),
        .size          = sizeof(float),
        .added_in      = 1,
        .removed_in    = 0,
        .short_desc    = "Metallic factor",
        .long_desc     = "0 = dielectric, 1 = metal",
        .default_value = "0.0f",
        .numeric_limits = {
            .ui_min     = 0.0,
            .ui_max     = 1.0,
            .hard_min   = 0.0,
            .hard_max   = 1.0,
            .has_ui_min   = true,
            .has_ui_max   = true,
            .has_hard_min = true,
            .has_hard_max = true,
        },
        .is_numeric    = true,
    },
    // ...
};

static constexpr erhe::codegen::Struct_info material_struct_info = {
    .name    = "Material",
    .version = 3,
    .fields  = material_fields,
};

auto get_struct_info(const Material*) -> const erhe::codegen::Struct_info&
{
    return material_struct_info;
}

auto get_fields(const Material*) -> std::span<const erhe::codegen::Field_info>
{
    return material_fields;
}
```

### 3.3 Generated Enum Output

#### Header (`blend_mode.hpp`)

```cpp
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <erhe_codegen/field_info.hpp>

enum class Blend_mode : uint8_t
{
    opaque      = 0,
    alpha_blend = 1,
    additive    = 2,
    multiply    = 3,
};

// String conversion (for serialization)
auto to_string  (Blend_mode value) -> std::string_view;
auto from_string(std::string_view str, Blend_mode& out) -> bool;

// Reflection
auto get_enum_info(const Blend_mode*) -> const erhe::codegen::Enum_info&;
```

#### Source (`blend_mode.cpp`)

```cpp
#include "blend_mode.hpp"

auto to_string(Blend_mode value) -> std::string_view
{
    switch (value) {
        case Blend_mode::opaque:      return "opaque";
        case Blend_mode::alpha_blend: return "alpha_blend";
        case Blend_mode::additive:    return "additive";
        case Blend_mode::multiply:    return "multiply";
    }
    return "opaque"; // fallback to first value
}

auto from_string(std::string_view str, Blend_mode& out) -> bool
{
    if (str == "opaque")      { out = Blend_mode::opaque;      return true; }
    if (str == "alpha_blend") { out = Blend_mode::alpha_blend; return true; }
    if (str == "additive")    { out = Blend_mode::additive;    return true; }
    if (str == "multiply")    { out = Blend_mode::multiply;    return true; }
    return false; // unknown string, out unchanged
}

static constexpr erhe::codegen::Enum_value_info blend_mode_values[] = {
    { .name = "opaque",      .value = 0, .short_desc = "Opaque",      .long_desc = "No transparency" },
    { .name = "alpha_blend", .value = 1, .short_desc = "Alpha Blend", .long_desc = "Standard alpha blending" },
    { .name = "additive",    .value = 2, .short_desc = "Additive",    .long_desc = "Additive blending for glow effects" },
    { .name = "multiply",    .value = 3, .short_desc = "Multiply",    .long_desc = "Multiplicative blending" },
};

static constexpr erhe::codegen::Enum_info blend_mode_enum_info = {
    .name                 = "Blend_mode",
    .underlying_type_name = "uint8_t",
    .values               = blend_mode_values,
};

auto get_enum_info(const Blend_mode*) -> const erhe::codegen::Enum_info&
{
    return blend_mode_enum_info;
}
```

#### Enum Field Serialization/Deserialization

Generated serialize/deserialize code for enum fields in structs:

```cpp
// Serialization: write enum as string
out += "\"blend_mode\":";
serialize_string(out, to_string(value.blend_mode));
out += ',';

// Deserialization: read string, convert to enum
if (version >= 4) {
    if (!obj["blend_mode"].get(val)) {
        std::string_view str;
        if (!val.get_string().get(str)) {
            from_string(str, out.blend_mode); // keeps default if unknown
        }
    }
}
```

---

## 4. Python Generator Architecture

```
src/erhe/codegen/
├── CMakeLists.txt              # CMake target: erhe_codegen (C++ runtime lib)
├── generate.py                 # Entry point: python generate.py <defs_dir> <output_dir>
├── erhe_codegen/
│   ├── __init__.py             # Exports: struct, field, enum, value, type constants, Vector, Array, StructRef, EnumRef
│   ├── types.py                # Type definitions (POD, composite, glm, enum refs)
│   ├── schema.py               # Struct/Field/Enum/EnumValue schema classes, validation, global registry
│   ├── emit_hpp.py             # Header code generation (structs + enums)
│   ├── emit_cpp.py             # Source code generation (serialize + deserialize)
│   ├── emit_enum.py            # Enum code generation (to_string, from_string, reflection)
│   └── emit_reflect.py         # Reflection table generation
├── include/
│   └── erhe_codegen/
│       ├── serialize_helpers.hpp   # Runtime helpers (serialize_string, serialize_array, etc.)
│       ├── field_info.hpp          # Field_info, Struct_info, Numeric_limits, Enum_info structs
│       └── migration.hpp           # Migration callback registration and execution
└── src/
    ├── serialize_helpers.cpp       # Runtime helper implementations
    └── migration.cpp               # Migration registry implementation
```

### 4.1 Generator Flow

```
1. Python definition files are imported/exec'd
2. Each enum() call registers an EnumSchema, each struct() call registers a StructSchema
3. Validation pass:
   a. All field added_in / removed_in within valid range
   b. Numeric limit options only on numeric types
   c. StructRef / EnumRef targets exist in the registry
   d. No duplicate field names within a struct
   e. No duplicate enumerator names or values within an enum
   f. Enum underlying_type is an integer type
4. For each EnumSchema:
   a. emit_hpp generates enum class + to_string/from_string + get_enum_info declarations
   b. emit_cpp generates to_string/from_string implementations + reflection table
5. For each StructSchema:
   a. emit_hpp generates the header (struct decl, deprecated fields, function decls)
   b. emit_cpp generates versioned serialization + deserialization
   c. emit_reflect generates the reflection tables (in same .cpp)
6. Write files to output directory
```

### 4.2 Command Line

```bash
python generate.py definitions/ generated/
```

---

## 5. Generated C++ Output

### 5.1 Generated Header (`material.hpp`)

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <simdjson.h>

#include <erhe_codegen/field_info.hpp>

struct Material
{
    static constexpr uint32_t current_version = 3;

    std::string              name{};                                    // v1+
    glm::vec4                albedo{0.5f, 0.5f, 0.5f, 1.0f};          // v1+
    float                    metallic{0.0f};                            // v1+
    float                    roughness{0.5f};                           // v1+
    glm::vec3                emissive{0.0f, 0.0f, 0.0f};              // v2+
    std::vector<std::string> texture_paths{};                           // v1+
    [[deprecated("removed in v3")]]
    float                    opacity{1.0f};                             // v2..v3
    std::array<float, 4>     uv_scales{1.0f, 1.0f, 1.0f, 1.0f};      // v3+
};

auto serialize  (const Material& value) -> std::string;
auto deserialize(simdjson::ondemand::object obj, Material& out) -> simdjson::error_code;

auto get_struct_info(const Material*) -> const erhe::codegen::Struct_info&;
auto get_fields     (const Material*) -> std::span<const erhe::codegen::Field_info>;
```

### 5.2 Generated Source (`material.cpp`)

Contains the implementations shown in sections 2.4, 2.5, and 3.2.

---

## 6. C++ Runtime Library (`erhe_codegen`)

A small static library providing:

- **`field_info.hpp`** — `Field_info`, `Numeric_limits`, `Enum_info`, `Enum_value_info`, `Struct_info` (section 3.1)
- **`migration.hpp/.cpp`** — `register_migration<T>()` and `run_migrations<T>()` (section 2.3)
- **`serialize_helpers.hpp/.cpp`** — Helper functions used by generated code:
  - `serialize_string(std::string& out, std::string_view value)`
  - `serialize_float(std::string& out, float value)`
  - `serialize_double(std::string& out, double value)`
  - `serialize_int(std::string& out, int64_t value)`
  - `serialize_uint(std::string& out, uint64_t value)`
  - `serialize_bool(std::string& out, bool value)`
  - `serialize_enum(std::string& out, std::string_view enum_string)` (wraps `serialize_string`)
  - `deserialize_field()` overloads for each scalar, glm, string type
  - Template helpers for `std::vector<T>` and `std::array<T, N>`
  - Enum serialize/deserialize is generated per-enum (uses `to_string`/`from_string`), not templated

### 6.1 Hard Limits Clamping

For fields with `hard_min` / `hard_max`, the generated deserialization code applies clamping after reading:

```cpp
// metallic: added_in=1, hard_min=0.0, hard_max=1.0
if (version >= 1) {
    if (!obj["metallic"].get(val)) {
        deserialize_field(val, out.metallic);
        if (out.metallic < 0.0f) out.metallic = 0.0f;
        if (out.metallic > 1.0f) out.metallic = 1.0f;
    }
}
```

---

## 7. CMake Integration

```cmake
# src/erhe/codegen/CMakeLists.txt

add_library(erhe_codegen STATIC
    src/serialize_helpers.cpp
    src/migration.cpp
    include/erhe_codegen/serialize_helpers.hpp
    include/erhe_codegen/field_info.hpp
    include/erhe_codegen/migration.hpp
)
target_include_directories(erhe_codegen PUBLIC include)
target_link_libraries(erhe_codegen PUBLIC simdjson glm)

function(erhe_codegen_generate)
    cmake_parse_arguments(ARG "" "TARGET;DEFINITIONS_DIR;OUTPUT_DIR" "" ${ARGN})

    file(GLOB DEFINITION_FILES "${ARG_DEFINITIONS_DIR}/*.py")

    set(GENERATOR_SCRIPT "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/generate.py")

    add_custom_command(
        OUTPUT ${ARG_OUTPUT_DIR}/generated_stamp.txt
        COMMAND ${Python3_EXECUTABLE} ${GENERATOR_SCRIPT}
            ${ARG_DEFINITIONS_DIR}
            ${ARG_OUTPUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch ${ARG_OUTPUT_DIR}/generated_stamp.txt
        DEPENDS ${DEFINITION_FILES} ${GENERATOR_SCRIPT}
        COMMENT "Generating C++ code from struct definitions"
    )

    add_custom_target(${ARG_TARGET}_codegen DEPENDS ${ARG_OUTPUT_DIR}/generated_stamp.txt)
    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_codegen)
endfunction()
```

---

## 8. Error Handling Strategy

- **Deserialization**: Missing fields keep their C++ default. Type mismatches return `simdjson::error_code`. Hard limit violations are clamped silently.
- **Missing `_version`**: Treated as version 1.
- **Future `_version`** (higher than current): Deserialize what is known, ignore unknown fields. simdjson on-demand naturally skips unrecognized keys.
- **Serialization**: Always succeeds (no I/O, just string building). Always writes `current_version`.
- **Enum deserialization**: Unknown enum strings leave the field at its default (not an error). This enables forward compatibility when new values are added.
- **Generator errors**: Python raises exceptions for:
  - `added_in` out of range
  - `removed_in` <= `added_in`
  - Numeric limit options on non-numeric types
  - Unknown types, duplicate field names, unresolved `StructRef` or `EnumRef`
  - Duplicate enumerator names or values within an enum
  - Non-integer `underlying_type` for enums

---

## 9. Implementation Milestones

### M1 — Scaffold
- [x] Create directory structure under `src/erhe/codegen/`
- [x] Create `CMakeLists.txt` with static library and generator function
- [x] Create Python package skeleton (`erhe_codegen/__init__.py`, `types.py`, `schema.py`)
- [x] Create `field_info.hpp` with `Field_info`, `Numeric_limits`, `Struct_info`

### M2 — POD Types + Versioning
- [x] Implement `types.py` with all scalar types
- [x] Implement `schema.py` (Struct/Field schema classes, version validation, global registry)
- [x] Implement `emit_hpp.py` — generate header with versioned struct + `[[deprecated]]` fields
- [x] Implement `emit_cpp.py` — generate versioned serialize/deserialize for scalar fields
- [x] Implement C++ runtime helpers for scalar types
- [x] Write a test definition and verify generated code compiles

### M3 — Enums
- [x] Implement `EnumSchema` / `EnumValueSchema` in `schema.py` *(done in M1)*
- [x] Add `enum()`, `value()`, `EnumRef()` to Python API *(done in M1)*
- [ ] Implement `emit_enum.py` — generate `enum class`, `to_string`, `from_string`
- [ ] Generate `Enum_value_info` / `Enum_info` reflection tables with `short_desc`, `long_desc`
- [ ] Generate `get_enum_info()` overload
- [x] Support `EnumRef("Name")` as a field type in structs (serialize as string, deserialize via `from_string`) *(emit_cpp.py handles this)*
- [x] Validate: no duplicate names/values, integer underlying type *(done in M1)*
- [ ] Test with enum definition + struct field using `EnumRef`

### M4 — Composite Types
- [x] Add `Vector(T)`, `Array(T, N)`, `StructRef("Name")` to type system *(done in M1)*
- [x] Generate versioned serialization/deserialization for vectors, arrays, nested structs *(done in M2)*
- [x] Nested structs carry their own `_version` in the JSON *(emit_cpp.py handles this)*
- [x] Add template helpers to C++ runtime *(done in M1/M2)*
- [ ] Test with nested struct definitions

### M5 — glm Types
- [x] Add `Vec2`, `Vec3`, `Vec4`, `Mat4` to type system *(done in M1)*
- [x] Implement serialize/deserialize as JSON arrays *(done in M2)*
- [x] Add C++ runtime helpers for glm types *(done in M1)*

### M6 — Reflection + Metadata
- [ ] Implement `emit_reflect.py` — generate `Field_info` tables with full metadata
- [ ] Generate `get_fields()` and `get_struct_info()` overloads
- [ ] Include version ranges, descriptions, numeric limits in reflection data
- [ ] Test reflection API

### M7 — Migration Callbacks
- [x] Implement `migration.hpp` / `migration.cpp` with `register_migration<T>()` and `run_migrations<T>()` *(done in M1)*
- [x] Implement type-erased `Migration_registry` with thread-safe callback storage *(done in M1)*
- [x] Update generated `deserialize()` to call `run_migrations()` when `_version` differs from `current_version` *(done in M2)*
- [ ] Test: register a migration callback, deserialize old version data, verify callback runs and transforms struct

### M8 — Hard Limit Clamping
- [x] Generate post-deserialization clamping code for fields with `hard_min` / `hard_max` *(done in M2)*
- [ ] Test with out-of-range values in JSON

### M9 — CMake Integration
- [x] Implement `erhe_codegen_generate()` CMake function *(done in M1)*
- [ ] Wire up to editor build as a proof-of-concept
- [ ] Verify incremental rebuilds work (re-generate only when definitions change)

---

## 10. Open Questions / Future Extensions

- **Optional fields**: `std::optional<T>` support — serialize as present/absent JSON keys?
- **ImGui editor generation**: Use reflection tables (descriptions, numeric limits) to auto-generate property editor UI?
- **Binary serialization**: Alternative to JSON for performance-critical paths?
- **Validation beyond clamping**: Generate validation functions (custom predicates, required fields)?
