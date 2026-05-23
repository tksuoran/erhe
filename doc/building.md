# Building erhe

## Dependencies

Most dependencies are fetched from their repositories using CMake CPM during the configure step.
Exceptionally, a few dependencies are directly included in `src/`.

## Platform Requirements

### Windows

-   Visual Studio 2026
-   Python 3
-   CMake
-   Optional: [ninja](https://github.com/ninja-build/ninja/releases)

### Linux

-   CMake (recent version; https://apt.kitware.com/ may help on Ubuntu)
-   C++ compiler: clang or GCC (not older than a couple of years)
-   Python 3
-   Packages:
    -   Ubuntu: `libwayland-dev libxkbcommon-dev xorg-dev`
    -   Arch: `gcc ninja cmake git less openssh libx11 libxrandr libxi libxinerama libxcursor mesa wayland wayland-protocols python`
-   Ubuntu 24.04 and archlinux has been tested

### macOS

-   Xcode command line tools (provides clang and system headers)
-   CMake
-   Python 3

## Build Steps

### Visual Studio 2026 (Windows)

From an x64 Native Tools Command Prompt:

```bat
git clone https://github.com/tksuoran/erhe
cd erhe
scripts\configure_vs2026_opengl.bat
devenv build_vs2026_opengl\erhe.slnx
```

In Visual Studio press F5 to build and run editor.

### Visual Studio Code (Windows and Linux)

1.  `git clone https://github.com/tksuoran/erhe`
2.  Open the `erhe` folder in Visual Studio Code
3.  Run one of the wrapper scripts from `scripts/` from a terminal in
    the project root (e.g. `scripts\configure_vs2026_opengl.bat` on
    Windows, `scripts/configure_xcode_opengl.sh` on macOS,
    `scripts/configure_ninja_linux.sh` on Linux). The wrappers
    encode the project's intended configure flow; do not invoke
    `cmake --preset` directly.
4.  Open the generated build directory and use the *CMake: Build*
    command, or build from a terminal:
    `cmake --build <build-dir> --target editor`.

### CLion (Windows and Linux)

Use the `scripts/` wrapper scripts to configure, then open the
resulting build directory in CLion. Do not use CLion's own preset
support directly -- the wrappers encode the project's intended
configure flow.

Note for Windows: It is much better to build with toolchains other
than mingw, which has extremely slow linking times. Most likely you
should setup Visual Studio toolchain in CLion before attempting to
build erhe on Windows.

### Linux (command line)

```bash
git clone https://github.com/tksuoran/erhe
cd erhe
scripts/configure_ninja_linux.sh
cmake --build build --target editor
```

### macOS (command line)

```bash
git clone https://github.com/tksuoran/erhe
cd erhe
scripts/configure_xcode_metal.sh
cmake --build build_xcode_metal --target editor --config Debug
```

### Android

Android support is in early stages and barely functional. No further support is planned for the moment.

```bat
git clone https://github.com/tksuoran/erhe
cd erhe
scripts\build_android.bat
scripts\run_android.bat
```

### Meta Quest

Support for Meta Quest is work in progress.

```bat
git clone https://github.com/tksuoran/erhe
cd erhe
scripts\build_android.bat
```

Important: when launching the editor on a Quest device over adb,
run the install step first (`scripts\install_android.bat quest`)
while the user can keep their hands free. **Only after the APK is on
the device** should you prompt the user to put the headset on and
pick up the Touch controllers, then launch with
`scripts\install_android.bat quest run`. Quest's
"Controllers Required" dialog blocks the immersive app from coming
to the foreground until controllers are detected as in-hand;
launching while the headset is off the user's head wastes the
attempt and the launch must be retried.

## CMake Configuration Options

| Option | Description | Recognized values |
| :--- | :--- | :--- |
| `ERHE_GRAPHICS_API` | Graphics backend | `opengl`, `vulkan`, `metal` (macOS only) `none` (headless) |
| `ERHE_WINDOW_LIBRARY` | Window library | `sdl`, `none` (headless) |
| `ERHE_PHYSICS_LIBRARY` | Physics library | `jolt`, `none` |
| `ERHE_RAYTRACE_LIBRARY` | Raytrace library | `bvh`, `tinybvh`, `embree`, `none` |
| `ERHE_PROFILE_LIBRARY` | Profiler integration | `tracy`, `none` |
| `ERHE_XR_LIBRARY` | XR library | `openxr`, `none` |
| `ERHE_AUDIO_LIBRARY` | Audio library | `miniaudio`, `none` |
| `ERHE_FONT_RASTERIZATION_LIBRARY` | Font rasterization | `freetype`, `none` |
| `ERHE_GLTF_LIBRARY` | glTF library | `fastgltf`, `none` |
| `ERHE_GUI_LIBRARY` | GUI library | `imgui`, `none` |
| `ERHE_SVG_LIBRARY` | SVG loading | `plutosvg`, `none` |
| `ERHE_TEXT_LAYOUT_LIBRARY` | Text layout | `harfbuzz`, `freetype`, `none` |
| `ERHE_USE_ASAN` | AddressSanitizer | `ON`, `OFF` |
| `ERHE_USE_PRECOMPILED_HEADERS` | Precompiled headers (faster builds) | `ON`, `OFF` |

These options allow faster build times by disabling unused features and selecting different backends.

### Notes on Specific Options

**ERHE_PHYSICS_LIBRARY** -- The main backend is `jolt`. Set to `none` to disable physics.

**ERHE_RAYTRACE_LIBRARY** -- The main backend is `bvh`, used for mouse picking in 3D viewports. When set to `none`, mouse picking uses GPU ID buffer rendering instead.

**ERHE_PROFILE_LIBRARY** -- The main profiler is Tracy. Superluminal support exists but is likely stale.

**ERHE_WINDOW_LIBRARY** -- Only `sdl` and `none` are supported.

**ERHE_XR_LIBRARY** -- Disabling removes headset rendering from the editor.

**ERHE_SVG_LIBRARY** -- Disabling removes scene node icons in the editor.

**ERHE_GLTF_LIBRARY** -- Disabling removes glTF import and export and scene serialization.

**ERHE_AUDIO_LIBRARY** -- Currently only used by non-functional experimental code. Best left as `none`.

**ERHE_FONT_RASTERIZATION_LIBRARY** / **ERHE_TEXT_LAYOUT_LIBRARY** -- Disabling either removes native text rendering (ImGui content is not affected).

## Windows Build Scripts

From an x64 Native Tools Command Prompt:

-   `scripts\configure_vs2026_opengl.bat` -- Standard build on Windows using Visual Studio 2026
-   `scripts\configure_vs2026_opengl_asan.bat` -- with AddressSanitizer
-   `scripts\configure_vs2026_opengl_no_tracy.bat` -- without Tracy profiler
