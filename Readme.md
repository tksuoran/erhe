[![Codacy Badge](https://app.codacy.com/project/badge/Grade/49fade7c78954f3a99a2d6ce84a9bc1a)](https://www.codacy.com/gh/tksuoran/erhe/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tksuoran/erhe&amp;utm_campaign=Badge_Grade)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

# erhe

erhe is a C++ library for modern OpenGL experiments.

-   Uses direct state access (DSA)
-   Uses bindless textures (when supported by driver)
-   Uses persistently mapped buffers (can be disabled)
-   Uses multi draw indirect
-   Uses latest OpenGL and GLSL versions
-   Uses multiple threads and OpenGL contexts
-   Uses abstraction for OpenGL pipeline state a bit similar to Vulkan (see `erhe::graphics`)
-   Simple type safe(r) wrapper for GL API (see `erhe::gl`)
-   Supports Windows and Linux

erhe is evolution of RenderStack <https://github.com/tksuoran/RenderStack>

![screenshot](https://github.com/tksuoran/erhe/wiki/images/12.png)

## Building

### Dependencies

All dependencies of erhe are either included directly in `src/` for
small libraries, or git pulled from their repositories using CMake
`fetchcontent` during CMake configure step.

### Windows Requirements

-   C++ compiler. Visual Studio 2022 with msbuild has been tested.
    Ninjabuild GCC and clang may also work, and some older versions
    of Visual Studio.

-   Python 3

-   CMake

-   Optional: ninja from https://github.com/ninja-build/ninja/releases

### Linux Requirements

-   Recent enough CMake
    -   Ubuntu 22.04 and 20.04 have been tested to good
    -   Ubuntu 18.04 has too old version of CMake
    -   https://apt.kitware.com/ may help to get recent CMake
-   New enough C++ compiler
    -   clang-10 or newer is ok
    -   GCC-9 or newer are ok
    -   GCC-8 or older is not currently support
-   python 3
-   packages such `xorg-dev`

For IDE:

-  Visual Studio Code with CMake and C++ extensions is supported
-  CLion is supported

### Build steps for Visual Studio (Windows)

-   `git clone https://github.com/tksuoran/erhe`
-   In *x64 native tools command prompt for vs 2022*, cd to the *erhe* directory
-   `scripts\configure_vs2022.bat`
-   Open solution from the *build* directory with Visual Studio
-   Build solution, or editor executable

### Build steps for CLion (Windows and Linux)

erhe has initial CLion support.

Currently, CLion does not fully support CMake presets. Enable `Debug` profile only.
If you want to make a release build, edit settings for that profile, instead of trying
to use the other CMake preset profiles.

-  Get from VCS: URL: `https://github.com/tksuoran/erhe`
-  Clone
-  Keep `Debug` CMake profile enabled, do not enable other profiles
-  Either default toolchain `MinGW` or `Visual Studio`
-  Wait for CMake configure to complete. It will say `[Finished]` in CMake tab
-  Build Project or Build 'editor'
-  Run `editor`

For IDE:

-  Visual Studio Code with CMake and C++ extensions is supported
-  CLion is supported

#### Build steps for Visual Studio Code (Windows and Linux)

-   `git clone https://github.com/tksuoran/erhe`
-   Open erhe folder in Visual Studio code
-   Execute command: *CMake: Select Configure Preset*
-   Execute command: *CMake: Configure*
-   Execute command: *CMake: Build*

## Configuration

There are several configuration options that can be set when configuring
erhe with CMake:

| Option                          | Description                | Recognized values               |
| :---                            | :---                       | :---                            |
| ERHE_AUDIO_LIBRARY              | Audio library              | miniaudio, none                 |
| ERHE_FONT_RASTERIZATION_LIBRARY | Font rasterization library | freetype, none                  |
| ERHE_GLTF_LIBRARY               | GLTF library               | cgltf, fastgltf, none           |
| ERHE_GUI_LIBRARY                | GUI library                | imgui, none                     |
| ERHE_PHYSICS_LIBRARY            | Physics library            | bullet, jolt, none              |
| ERHE_PNG_LIBRARY                | PNG loading library        | mango, none                     |
| ERHE_PROFILE_LIBRARY            | Profile library            | nvtx, superluminal, tracy, none |
| ERHE_RAYTRACE_LIBRARY           | Raytrace library           | embree, bvh, none               |
| ERHE_SVG_LIBRARY                | SVG loading library        | lunasvg, none                   |
| ERHE_TEXT_LAYOUT_LIBRARY        | Text layout library        | harfbuzz, freetype, none        |
| ERHE_WINDOW_LIBRARY             | Window library             | glfw, none                      |
| ERHE_XR_LIBRARY                 | XR library                 | OpenXR, none                    |

Main purposes of these configuration options are

-   Allow faster build times by disabling features that are not used (during development)
-   Allow to choose different physics and raytrace backends

### ERHE_PHYSICS_LIBRARY

The main physics backend is currently `jolt`. The `bullet` physics backend has been
rotting for a while, it would require some work to get it back to working.

### ERHE_RAYTRACE_LIBRARY

The main raytrace backend is currently `bvh`. Even the `bvh` backend is incomplete,
causing performance issues when creating larger scenes. The `embree` raytrace backend
has been rotting for a while, it would require some work to get it back to working.

erhe (editor) can be configured to use raytrace for mouse picking models from 3D viewports.
By default, and when raytrace backend is set to `none`, mouse picking uses GPU rendering
based, where GPU renders ID buffer (unique color per object and triangle) and the image
is read back to the CPU.

### ERHE_PROFILE_LIBRARY

The main profile library is Tracy.

Superluminal was briefly tested, but support for it is likely rotten.

### ERHE_WINDOW_LIBRARY

Only `glfw` is currently supported as window library in erhe.

Disabling window library removes ability to show desktop window.
Such configuration can still be useful if OpenXR is used.

### ERHE_XR_LIBRARY

Only `openxr` is currently supported as XR library in erhe.

Disabling xr library removes ability to enable headset rendering in erhe editor.

### ERHE_SVG_LIBRARY

Only `lunasvg` is current supported as SVG loading library in erhe.
Disabling SVG library removes erhe editor scene node icons.

### ERHE_PNG_LIBRARY

Disabling PNG library removes erhe window icon.

Current implementation uses mango (subset) and spng for loading PNG files.

### ERHE_GLTF_LIBRARY

Disabling GLTF library removes capability to parse GLTF files in erhe editor.

### ERHE_AUDIO_LIBRARY

Currently, audio library is only used with some code (VR theremin) that is currently not functional.
`miniaudio` can be enabled, but at the moment it is best to use `none`.

### ERHE_FONT_RASTERIZATION_LIBRARY

Currently, only `freetype` is supported. Disabling font rasterization library removes
native text rendering in erhe. ImGui content is not affected.

### ERHE_TEXT_LAYOUT_LIBRARY

Currently, only `harfbuzz` is supported. Freetype as text layout support is rotten
but might be resurrected.

Disabling font layout library removes native text rendering in erhe. ImGui content is not affected.

# erhe executables

## editor

Editor is a sandbox like experimentation executable with a random set of functionality

-   Scene is (mostly) procedurally generated
-   A primitive GLTF parser can load GLTF file content
-   A primitive obj parser can load OBJ file content
-   ImGui is used extensively for user interface
-   Content can be viewed with OpenXR compatible headset (if enabled from `erhe.ini`)
-   Scene nodes can be manipulated with a basic translation / rotation gizmo
-   When physics backend is enabled, scene models can interact physically
-   Scene model geometries can be manipulated with operations such as Catmull-Clark
-   Scene models can be created using a brush tool (must have selected brush *and* material)

# erhe libraries

Major libraries
## erhe::geometry

`erhe::geometry` provides classes manipulating geometric, polygon based 3D objects.

Geometry is collection of `Point`s, `Polygon`s, `Corner`s and their attributes.

Arbitrary attributes can be associated with each `Point`, `Polygon` and `Corner`.

These classes are designed for manipulating 3D objects, not for rendering them.
See `erhe::scene` and `erhe::scene_renderer` how to render geometry objects.

Some features:

-  Catmull-Clark subdivision operation
-  Sqrt3 subdivision operation
-  Conway operators:
    -   Dual
    -   Ambo
    -   Truncate
    -   Gyro

## erhe::gl

`erhe::gl` provides python generated low level C++ wrappers for OpenGL API.

Some features:

-  Strongly typed C++ enums
-  Optional API call logging, with enum and bitfield values shown as human readable strings
-  Queries for checking GL extension and command support
-  Helper functions to map enum values to/from zero based integers (to help with ImGui, hashing, serialization)

## erhe::graphics

`erhe::graphics` provides classes basic 3D rendering with modern OpenGL.

Currently, erhe uses OpenGL as graphics API. The `erhe::graphics` builds a Vulkan-like
abstraction on top of OpenGL:

-  `erhe::graphics::Pipeline` capsulates all relevant GL state.

## erhe::imgui

`erhe::imgui` provides custom ImGui backend and helper classes to manage
and implement ImGui Windows.

## erhe::log

`erhe::log` provides helpers / wrappers for spdlog logging.

## erhe::primitive

`erhe::primitive` provides classes to convert `erhe::geometry::Geometry`
to renderable (or raytraceable) vertex and index buffers.

## erhe::renderer

`erhe::renderer` provides classes to assist rendering generic 3D content.

-  `Buffer_writer` keeps track of range of graphics `Buffer` that is being written to.
-  `Multi_buffer` keeps dedicated versions of a graphics buffer for each frame in flight.
-  `Line_renderer` can be used to draw debug lines
-  `Text_renderer` can be used to draw 2D text (labels) into 3D viewport


## erhe::item

`erhe::item` provides base object classes for "entities".

-  `Item` has name, source asset path, flags, unique id
-  `Hierarchy` extends `Item` by adding pointer to parent, and vector of children

## erhe::scene

`erhe::scene` provides classes for basic 3D scene graph.

-  `Node` extends `Item` by adding 3D transformation and glTF-like attachment points for Camera, Light, Mesh
-  `Camera` is like glTF Camera, which can be attached to `Node`
-  `Light` is like glTF Light, which can be attached to `Node`
-  `Mesh` is like glTF Mesh (containing a number of `Primitive`s), which can be attached to Node
-  `Scene` is collection of `Node`s

## erhe::rendergraph

`erhe::rendergraph` provides classes for arranging rendering passes into a graph

-   `Rendergraph_node` lists a number of inputs (dependencies) and outputs.
    A `Rendergraph_node` can be executed as part of `Rendergraph`.

-   `Rendergraph` is a collection of `Rendergraph_node`s.
    `Rendergraph` can be executed, this will execute rendergraph nodes in order
    that is based on connections between nodes in the graph.

## erhe::scene_renderer

`erhe::scene_renderer` provides classes for rendering `erhe::scene` content.
It uses and extends functionality from `erhe::renderer`.

## erhe::window namespace

`erhe::toolkit` provides windowing system abstraction, currently
using GLFW3.

## erhe::verify

`erhe::verify` provides a simple `VERIFY(condition)` and `FATAL(format, ...)` macros,
which can be used in place of `assert()` and unrecoverable error.

## erhe::physics

`erhe::physics` provides minimal abstraction / wrappers for Jolt / Bullet physics libraries.
The Jolt physics backend is more complete. The Bullet physics backend has been rotting for some time.

## erhe

## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.
