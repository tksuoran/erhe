---
description: "Project Foundations - C++ implementation"
---

# Project Foundations - C++

$include: ./project-foundations.md

---

## 1. Zero-Tolerance Warnings

```cmake
# CMakeLists.txt - warnings are errors, project-wide
if (MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror
        -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast)
endif()
```

```yaml
# .clang-tidy - static analysis as part of the build
Checks: >
    bugprone-*,cppcoreguidelines-*,performance-*,modernize-*,readability-*,
    -modernize-use-trailing-return-type
WarningsAsErrors: '*'
HeaderFilterRegex: '.*'
```

```bash
# CI - zero tolerance
cmake --build build 2>&1 | grep -E "warning|error" && exit 1
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp')
```

`-Werror` plus `/WX` makes warnings fail the build on every compiler. `clang-tidy` with `WarningsAsErrors` catches design issues (dangling, narrowing conversions, missing `override`). Never silence a warning with a cast - fix the cause.

> **Audit note:** verify this from the *active* compiler-config file(s) the default build
> uses (e.g. `cmake/<Compiler>.cmake`), not from a grep count across all `*.cmake`. A count
> includes vendored trees, commented-out lines, and non-default toolchains -- it will
> report `-Werror` as present everywhere when it is enabled on one compiler only. Confirm
> per compiler (MSVC `/WX`, Clang/GCC `-Werror`), and remember suppressions
> (`-Wno-sign-conversion`, etc.) can hide real defects that LSAI `diagnostics` still sees.

---

## 2. Central Dependency Management

```cmake
# cmake/dependencies.cmake - ONE place pins every version
include(cmake/CPM.cmake)

CPMAddPackage("gh:fmtlib/fmt#11.0.2")
CPMAddPackage("gh:gabime/spdlog@1.14.1")
CPMAddPackage("gh:catchorg/Catch2@3.7.1")
CPMAddPackage(NAME glm GITHUB_REPOSITORY g-truc/glm GIT_TAG 1.0.1)
```

```cmake
# Each target links by name - version is defined once, above
target_link_libraries(my_lib PUBLIC fmt::fmt spdlog::spdlog glm::glm)
```

One version per dependency for the whole build tree. Pin by tag/commit (never a moving branch). The lockfile equivalent is the explicit `GIT_TAG` - committed and reviewed. Upgrading a dependency is a single-line change in `dependencies.cmake`. (vcpkg `versions` / Conan `conanfile` lockfiles are equivalent strategies.)

---

## 3. Versioning Strategy

```cmake
# Embed version + git hash at configure time
execute_process(COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
configure_file(version.hpp.in ${CMAKE_BINARY_DIR}/generated/version.hpp @ONLY)
```

```cpp
// version.hpp.in
namespace app {
inline constexpr const char* version = "@PROJECT_VERSION@";
inline constexpr const char* git_hash = "@GIT_HASH@";
}
```

```cpp
// at startup - the running binary reports exactly what it is
SPDLOG_INFO("starting {} v{} ({})", app_name, app::version, app::git_hash);
```

The binary logs its version + git hash at startup. Debug traces are useless without knowing which build produced them. CMake `project(... VERSION x.y.z)` is the single source.

---

## 4. Structured Error Handling

```cpp
// Typed, value-based errors - no opaque strings, no exceptions for control flow
enum class Error_code { not_found, validation, unauthorized, io_failure };

class Error {
public:
    Error(Error_code code, std::string context) : m_code{code}, m_context{std::move(context)} {}
    [[nodiscard]] auto code() const -> Error_code { return m_code; }
    [[nodiscard]] auto context() const -> const std::string& { return m_context; }
private:
    Error_code  m_code;
    std::string m_context;
};

// std::expected<T, E> (C++23) or tl::expected for C++20
auto find_user(User_id id) -> std::expected<User, Error>
{
    if (!exists(id)) {
        return std::unexpected{Error{Error_code::not_found, std::format("user {}", id.value)}};
    }
    return load(id);
}
```

Use `std::expected<T, E>` (C++23) / `tl::expected` (C++20) for recoverable failures, typed exceptions for truly exceptional paths. Errors carry a typed code plus context - consumers branch on the code, not on parsed message text. Never `throw std::runtime_error("something went wrong")`.

---

## 5. Test Configuration (Fast/Slow Split)

```cmake
# ctest labels split fast unit tests from slow integration
add_test(NAME unit_geometry COMMAND geometry_tests)
set_tests_properties(unit_geometry PROPERTIES LABELS "fast")

add_test(NAME integration_db COMMAND db_integration_tests)
set_tests_properties(integration_db PROPERTIES LABELS "slow")
```

```bash
ctest -L fast               # default dev loop - seconds
ctest -L slow               # integration, on demand
ctest --output-on-failure   # CI
```

Fast unit tests run in the default loop. Slow tests (integration, GPU, filesystem) are labeled and excluded by default. One slow test must never block the unit suite.

---

## 6. Immutable DTOs

```cpp
// Value type, immutable after construction - const members, no setters
class User_dto {
public:
    User_dto(std::int64_t id, std::string name, std::string email)
        : m_id{id}, m_name{std::move(name)}, m_email{std::move(email)} {}

    [[nodiscard]] auto id   () const -> std::int64_t        { return m_id; }
    [[nodiscard]] auto name () const -> const std::string&  { return m_name; }
    [[nodiscard]] auto email() const -> const std::string&  { return m_email; }

    auto operator<=>(const User_dto&) const = default;   // value equality
private:
    std::int64_t m_id;
    std::string  m_name;
    std::string  m_email;
};
```

DTOs are constructed once and never mutated. No setters. `operator<=>`/`operator==` for value equality. Pass by `const&`, return by value or `const&`. Prefer aggregates with `const`-correct accessors over public mutable fields.

---

## 7. Dependency Injection with Proper Registration

```cpp
// Constructor injection - dependencies are explicit, passed as references
class User_service {
public:
    User_service(User_repository& repo, Clock& clock) : m_repo{repo}, m_clock{clock} {}
private:
    User_repository& m_repo;
    Clock&           m_clock;
};

// main.cpp IS the composition root - readable top to bottom
int main()
{
    Config           config = Config::load("config.toml");
    Postgres_pool    pool   {config.database_url};
    Postgres_user_repo user_repo{pool};
    System_clock     clock;
    User_service     user_service{user_repo, clock};
    // ... wire the rest, then run
}
```

`main()` is the composition root - one readable sequence of constructions. No DI framework: constructor parameters declare every dependency. Each component receives the collaborators it needs as explicit reference/pointer arguments (never reaching into a global service locator during construction).

---

## 8. Centralized Configuration with Validation

```cpp
// All tunables in config files, validated at load - fail fast, no hidden defaults
class Config {
public:
    static auto load(const std::filesystem::path& path) -> Config
    {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error{std::format("config not found: {}", path.string())};
        }
        Config config = parse_toml(path);
        config.validate();   // missing required key -> throw with clear message at startup
        return config;
    }
private:
    void validate() const;   // every required field checked here
};
```

All configurable values live in config files (TOML/JSON), loaded and validated at startup. A missing required value aborts immediately with a clear message - never a silent in-code default like `"./data"` that breaks when the working directory changes.

---

## 9. Structured File Logging

```cpp
// spdlog to rotating files - NOT std::cout / printf
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

void setup_logging()
{
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/app.log", 1024 * 1024 * 5, 3);
    auto logger = std::make_shared<spdlog::logger>("app", file_sink);
    logger->set_level(spdlog::level::info);   // level from config, not recompiled
    spdlog::set_default_logger(logger);
}

SPDLOG_INFO("user {} retrieved in {} ms", id.value, elapsed_ms);
```

Logs go to files via `spdlog` (wrapped behind the project's own logging abstraction). Libraries depend on the abstraction, only the entry point configures sinks. Levels live in config, changeable without recompiling. Never `std::cout`/`printf` - it is lost under redirected stdio and not searchable.

---

## 10. Zero-Dependency Core Module

```
src/
  core/        -> only the standard library + project abstractions
  domain/      -> depends on core
  infra/       -> depends on domain + third-party libs
  app/         -> depends on domain + infra
```

```cmake
add_library(core INTERFACE)               # or STATIC with std-only sources
target_link_libraries(core INTERFACE)     # NO third-party deps
target_compile_features(core INTERFACE cxx_std_20)
```

The core target holds contracts: abstract interfaces, value types/DTOs, enums, error types. It links nothing external (only the standard library). Implementations depend on core, never the reverse - swapping an implementation leaves core untouched.

---

## 11. Interface-First Design

```cpp
// Abstract interface at the boundary; implementation hidden behind it
class IUser_repository {
public:
    virtual ~IUser_repository() = default;
    [[nodiscard]] virtual auto find_by_id(User_id id) -> std::expected<User, Error> = 0;
    virtual auto save(const User& user) -> std::expected<void, Error> = 0;
};

// Or a C++20 concept when static polymorphism fits and virtual dispatch is unwanted
template <typename T>
concept User_repository = requires (T repo, User_id id) {
    { repo.find_by_id(id) } -> std::same_as<std::expected<User, Error>>;
};
```

Every significant component is reached through an interface (abstract base class for dynamic dispatch, a `concept` for static). The interface is the boundary; implementations sit behind it. This is what makes components mockable and replaceable.

---

## 12. Internal/Private by Default

```cpp
// Anonymous namespace - internal linkage, invisible outside the TU
namespace {
auto hash_password(std::string_view pw) -> std::string { /* ... */ }
}

// PIMPL - hide implementation details and dependencies from the header
class User_service {
public:
    User_service();
    ~User_service();
    [[nodiscard]] auto get_user(User_id id) -> std::expected<User, Error>;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;   // header exposes nothing internal
};
```

Members are `private` by default; only the necessary surface is public. Helpers live in anonymous namespaces (internal linkage). PIMPL keeps implementation details and heavy includes out of headers. A small public surface = freedom to refactor internals without breaking consumers.

---

## 13. Test Module per Production Module

```
src/
  geometry/        geometry/CMakeLists.txt   -> add_library(geometry)
  geometry/tests/  geometry/tests/...        -> add_executable(geometry_tests)
  scene/           ...                        -> add_library(scene)
  scene/tests/                                -> add_executable(scene_tests)
```

```cmake
add_executable(geometry_tests test_mesh.cpp test_operators.cpp)
target_link_libraries(geometry_tests PRIVATE geometry Catch2::Catch2WithMain)
add_test(NAME geometry COMMAND geometry_tests)
```

Each production library has exactly one test target. A failing test names the broken library. Tests link only that library's public interface; internal-only tests live in the same target so they can reach internals.

---

## 14. Convention Over Configuration

```cmake
# A helper macro so adding a library is one call, not boilerplate per target
function(add_project_library name)
    add_library(${name} ${ARGN})
    target_compile_features(${name} PUBLIC cxx_std_20)
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(${name} PROPERTIES FOLDER "libs/${name}")
endfunction()
```

Conventions drive wiring: a uniform target-creation helper, a directory layout that mirrors the module graph, consistent header/namespace naming. Adding a component means following the layout, not editing five unrelated files. (Source files are still listed explicitly - never `file(GLOB)`.)

---

## 15. Deterministic Build Output

```bash
# Out-of-source, one known build directory per configuration
cmake -S . -B build/debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/debug
```

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
```

Builds are out-of-source into a known, per-configuration directory. No artifacts beside the sources, no stale objects bleeding between configs. A clean build is reproducible: same inputs (pinned deps, fixed toolchain) -> same output. Wipe a config by deleting its build dir.

---

## 16. Black Box Composition

```cpp
// One public header is the contract; everything else is private to the target
// scene/scene.hpp  <- the only header consumers include
namespace scene {
class Scene;        // forward-declared / public types only
}
```

```cmake
target_include_directories(scene
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include   # public contract
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)      # hidden internals
```

Each CMake target is a black box: a public include directory holds the contract, private sources hold the internals. Consumers see only the public header. `PUBLIC`/`PRIVATE` link and include scopes enforce isolation at configure time, so one component cannot reach into another's internals.

---

## Verification Commands

```bash
# Configure with warnings-as-errors and sanitizers
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

# Build - must be 0 warnings / 0 errors
cmake --build build

# Run all tests; fast subset for the dev loop
ctest --test-dir build --output-on-failure
ctest --test-dir build -L fast

# Static analysis
clang-tidy -p build $(git ls-files '*.cpp')
cppcheck --enable=all --inconclusive --std=c++20 src/

# Formatting gate
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp')

# Find missing/extra includes
include-what-you-use -p build

# Address/UB sanitizer run (build configured above)
ctest --test-dir build   # ASan/UBSan abort on first violation

# Thread sanitizer (separate build - TSan is incompatible with ASan)
cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread" && ctest --test-dir build-tsan
```

```bash
# Verify tests actually RUN in CI (not just that they exist):
grep -rE 'ctest|BUILD_TESTS' .github/workflows/    # a ctest step must be present
```

## Auditing with LSAI (how to gather the evidence without unreliable text counts)

When **auditing** an existing project (not authoring), get the evidence through the
language server, scoped to own code. Several foundations are easy to "measure" with a
grep count that is in fact misleading -- here is how to check them properly:

- **#1 warnings-as-errors**: read the active `cmake/<Compiler>.cmake` files and confirm
  `-Werror`/`/WX` *per compiler*. A grep *count* over all `*.cmake` counts vendored and
  commented-out lines and reports the flag as universal when it is one-compiler-only.

- **RAII / ownership (the `new`/`delete` check)**: do **not** judge this from a raw
  `new`-vs-`delete` text count. A ratio like "238 `new` vs 17 `delete`" is unreliable
  (placement-new, vendored code, and macros all skew it) and reading `.cpp`/`.hpp` to
  count is forbidden by the audit's BASE_RULE anyway. Instead:
  1. LSAI `search` for the project's owning types (`unique_ptr`, `shared_ptr`,
     `make_unique`, `make_shared`) to confirm smart pointers are the norm.
  2. LSAI `outline` / `info` on the public interface types -- verify factory methods and
     accessors return/accept **owning smart pointers**, not raw owning pointers
     (`T*` that the caller must `delete`).
  3. Let LSAI `diagnostics` surface the actual misuse (`-Wunsafe-buffer-usage`,
     leaks under ASan) -- the few raw `new`/`delete` sites it flags are the real
     spot-check targets, not an aggregate ratio.

- **#4 typed errors / #11 interfaces / #12 visibility**: LSAI `search`/`outline`/`info`
  for the error type, `I*` interfaces, and member accessibility -- do not eyeball headers.

- **Code defects in general**: LSAI `diagnostics` (see methodology section 5b), filtered
  to own code with the `-Wc++98-compat` noise removed.
