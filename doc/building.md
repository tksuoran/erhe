# Building erhe

## Dependencies

All dependencies are either included directly in `src/` for small libraries, or fetched from their repositories using CMake CPM during the configure step.

## Platform Requirements

### Windows

-   C++ compiler: Visual Studio 2026 recommended with msbuild (tested); ninja with GCC or clang may also work. Visual Studio 2022 likely still also works but is no longer actively maintained.
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

### Visual Studio (Windows)

```bat
git clone https://github.com/tksuoran/erhe
cd erhe
scripts\configure_vs2026_opengl.bat
```

Open the solution from the `build` directory with Visual Studio and build the `editor` target.

### CMake Presets

```bash
cmake --preset OpenGL_Debug    # Debug OpenGL build
cmake --preset OpenGL_Release  # Release OpenGL build
```

Note: While Vulkan CMake preset exists, Vulkan backend is in early stages and is nowhere near usable.

Build output goes to `build/<presetName>/`.

```bash
cmake --build build/<presetName> --target editor
```

### Visual Studio Code (Windows and Linux)

1.  `git clone https://github.com/tksuoran/erhe`
2.  Open the `erhe` folder in Visual Studio Code
3.  Execute command: *CMake: Select Configure Preset*
4.  Execute command: *CMake: Configure*
5.  Execute command: *CMake: Build*

### CLion (Windows and Linux)

At the time of writing, CLion does not fully support CMake presets. Enable the `Debug` profile only. Edit that profile's settings if you want a release build.

Note for Windows: It is much better to build with toolchains other than mingw, which has extremely slow linking times. Most likely you should setup Visual Studio toolchain in CLion before attempting to build erhe on Windows. 

1.  Get from VCS: URL `https://github.com/tksuoran/erhe`
2.  Clone
3.  Keep `Debug` CMake profile enabled, do not enable other profiles
4.  Select `Visual Studio` or `MinGW` toolchain
5.  Wait for CMake configure to complete (`[Finished]` in CMake tab)
6.  Build Project or Build `editor`
7.  Run `editor`

### Linux (command line)

```bash
git clone https://github.com/tksuoran/erhe
cd erhe
cmake -B build
cmake --build build --target editor
```

### macOS (command line)

```bash
git clone https://github.com/tksuoran/erhe
cd erhe
cmake --preset OpenGL_Debug
cmake --build build/OpenGL_Debug --target editor
```

## CMake Configuration Options

| Option | Description | Recognized values |
| :--- | :--- | :--- |
| `ERHE_GRAPHICS_LIBRARY` | Graphics backend | `opengl`, `none` (headless) |
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
