# cpp-project.md -- per-project settings (C++), filled for erhe

> The x-* commands read these values and use them instead of the generic
> CMake/CTest defaults. One place, edited once.

## Build & test
@build:        cmake --build build_ninja_win_vulkan --target editor   # incremental editor build (Ninja/MSVC)
@configure:    scripts\configure_ninja_win_clang.bat                  # emits build_ninja_win_clang/compile_commands.json for clangd/LSAI
@test:         ctest --test-dir build_ninja_win_vulkan --output-on-failure   # needs -DERHE_BUILD_TESTS=ON; GoogleTest, ~8 libs covered
@test-fast:    ctest --test-dir build_ninja_win_vulkan                # no fast/slow CTest labels exist yet
@test-target:  ctest --test-dir build_ninja_win_vulkan -R             # prefix for filtering one target by name

## Quality gates
@lint:         clang-tidy -p build_ninja_win_clang $(git ls-files 'src/erhe/*.cpp' 'src/editor/*.cpp')   # no committed .clang-tidy config
@format-check: clang-format --dry-run --Werror $(git ls-files 'src/erhe/*.cpp' 'src/erhe/*.hpp')          # no committed .clang-format config
@asan:         scripts\configure_vs2026_vulkan_asan.bat               # then build + run editor; ERHE_USE_ASAN

## Code navigation
@code-nav:          lsai                          # OWN code: LSAI (mcp__lsai__*); grep only for macros / non-C++
@code-nav-external: xmp4                          # THIRD-PARTY libs: xmp4 (mcp__*xmp4*)
@compile-db:        build_ninja_win_clang         # dir holding compile_commands.json (clang-cl)

## Runtime / logs
@run:          build_ninja_win_vulkan\src\editor\editor.exe   # launch from repo root (needs config/, res/, writes logs/)
@logs:         logs/log.txt                                   # spdlog file sink (also logs/vulkan.txt, logs/openxr.txt)
@platform:     Windows + MSVC (x64 Native Tools), Vulkan default. Live debugging via Visual Studio / VS-MCP, NOT gdb/lldb. clangd/LSAI must be launched from the x64 Native Tools prompt so it resolves the STL + Windows SDK headers.

## Conventions
@methodology:  tddab                             # tddab | tdd | manual
@tasks:        tasks                             # where plan / task / audit docs live
