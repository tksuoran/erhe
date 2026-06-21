# cpp-project.md -- optional per-project settings (C++)

> OPTIONAL. The x-* commands work without this file (they fall back to generic
> CMake/CTest commands). Fill it in to make every command use YOUR project's real
> build/test/navigation commands instead of the generic ones. One place, edited once.
>
> This is a stripped-down stand-in for a full workflow's settings file: only the
> fields the x-* commands actually read.

## Build & test
@build:        cmake --build build            # how to build (e.g. scripts\build_android.bat quest)
@configure:    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
@test:         ctest --test-dir build --output-on-failure
@test-fast:    ctest --test-dir build -L fast
@test-target:  ctest --test-dir build -R       # prefix for filtering one target by name

## Quality gates
@lint:         clang-tidy -p build $(git ls-files '*.cpp')
@format-check: clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp')
@asan:         ctest --test-dir build-asan     # build configured with -fsanitize=address,undefined

## Code navigation
@code-nav:          lsai                        # OWN code: LSAI (mcp__lsai__*) or clangd LSP; "grep" = last-resort fallback only
@code-nav-external: xmp4                        # THIRD-PARTY libs: xmp4 (mcp__*xmp4*); "none" if unavailable -> use official docs
@compile-db:        build                       # dir holding compile_commands.json (clangd --compile-commands-dir=<here>)

## Runtime / logs
@run:          # how to launch the app (project-specific, e.g. scripts\install_android.bat quest + adb am start)
@logs:         # where the app writes its log file (spdlog sink path) / how to read device logs
@platform:     # e.g. Windows + MSVC (x64 Native Tools), Linux, Android/Quest

## Conventions
@methodology:  tddab                           # tddab | tdd | manual
@tasks:        docs/tasks                       # where plans / task docs live (if any)
