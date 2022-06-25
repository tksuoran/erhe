[![Codacy Badge](https://app.codacy.com/project/badge/Grade/49fade7c78954f3a99a2d6ce84a9bc1a)](https://www.codacy.com/gh/tksuoran/erhe/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tksuoran/erhe&amp;utm_campaign=Badge_Grade)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

# erhe

erhe is a C++ library for modern OpenGL experiments.

-   Uses direct state access (DSA)
-   Uses bindless textures (when supported by driver)
-   Uses persistently mapped buffers
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
-   `scripts\configure_msbuild.bat`
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

| Option                          | Description                | Recognized values         |
| :---                            | :---                       | :---                      |
| ERHE_AUDIO_LIBRARY              | Audio library              | miniaudio, none           |
| ERHE_FONT_RASTERIZATION_LIBRARY | Font rasterization library | freetype, none            |
| ERHE_GLTF_LIBRARY               | GLTF library               | cgltf, none               |
| ERHE_GUI_LIBRARY                | GUI library                | imgui, none               |
| ERHE_PHYSICS_LIBRARY            | Physics library            | bullet, jolt, none        |
| ERHE_PNG_LIBRARY                | PNG loading library        | mango, none               |
| ERHE_PROFILE_LIBRARY            | Profile library            | superluminal, tracy, none |
| ERHE_RAYTRACE_LIBRARY           | Raytrace library           | embree, bvh, none         |
| ERHE_SVG_LIBRARY                | SVG loading library        | lunasvg, none             |
| ERHE_TEXT_LAYOUT_LIBRARY        | Text layout library        | harfbuzz, freetype, none  |
| ERHE_WINDOW_LIBRARY             | Window library             | glfw, none                |
| ERHE_XR_LIBRARY                 | XR library                 | OpenXR, none              |

Main purposes of these configuration options are

-   Allow faster build times by disabling features that are not used (during development)
-   Allow to choose different physics and raytrace backends

### ERHE_PHYSICS_LIBRARY

The main physics backend is currently `bullet`. The `jolt` physics backend is less complete,
and requires more work before it is usable.

### ERHE_RAYTRACE_LIBRARY

The main raytrace backend is currently `embree`. Even the `embree` backend is incomplete,
causing performance issues when creating larger scenes.

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

# erhe

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

## erhe::components namespace

`erhe::component`s namespace provides classes to manage components.
Components can depend on each other. Components declare what other
components they need for their initialization. With this information,
components are automatically initialized in correct order, also in
parallel if so configured.


### erhe::components::Component

-   Provides services other component

-   Uses phased construction to make sure component initializations
    are done automatically in correct order

    -   C++ constructor
        -   Other components might not yet be constructed
        -   Components can be constructed in any order
        -   Can do initialization which do not depend on other components

    -   `declare_required_components()`:

        -   This method is called when all components have been constructed

        -   Other components can be fetched with `require<>`

        -   Other components have C++ constructors called, but they
            have not yet been initialized with `Component::initialize()`
            and the order in which `declare_required_components()` is undefined.

        -   Components fetched with `require<>` are added as dependencies,
            and they will be initialized before `Component::initialize()`
            will be called.

    -   `initialize()`:

        -   When `initialize()` is called for a component, then dependency components
            are guaranteed to be already initialized.

            Call to `initialize()` may happen from a worker thread.

            Within `initialize()` it is valid to call `get<>()` for components
            that have been declared as dependency. Calling `get<>()` for any
            other component is fatal error.

    -   `post_initialize()`

        -   When `post_initialize()` is called for a component, all components are
            guaranteed to have been initialized. The order of calls to
            `post_initialize()` is not defined.

            This is the earliest point where a component is free to call `get<>()`
            to obtain access to any other component.

            Calls to `post_initialize()` will happen in the main thread.

### erhe::components::Components

-   Maintains a set of components

-   Finds out the order which components must be initialized based on
    declared dependencies

### Usage

-   Construct all components, using `Components::add(make_shared<>())`

-   In each Component `declare_required_components()`, declare what other
    componenss are needed to initialize the componen by using
    `Component::require<>()`

-   Once all components have been added, call `Components::initialize_components()`.
    This will call `Component::declare_required_components()` for all components,
    followed by `Component::initialize()` for each component, in order which
    respects all declared dependencies. If there are circular dependencies,
    `initialize_components()` will log and error and abort.

    After all components have been initialized, `Component::post_initialize()`
    will be called to all components.

## erhe::geometry namespace

`erhe::geometry` namespace provides classes manipulating geometric, polygon
based 3D objects.

Geometry is collection of `Point`s, `Polygon`s, `Corner`s and their attributes.

Arbitrary attributes can be associated with each `Point`, `Polygon` and `Corner`.

These classes are designed for manipulating 3D objects, not for rendering them.
See `erhe::mesh` how to render geometry objects.

Some features:

-  Catmull-Clark subdivision operation
-  Sqrt3 subdivision operation
-  Conway operators:
    -   Dual
    -   Ambo
    -   Truncate
    -   Gyro

## erhe::gl namespace

`erhe::gl` namespace provides python generated low level C++ wrappers for GL API.

Some features:

-  Strongly typed C++ enums
-  Optional API call logging, with enum and bitfield values shown as human readable strings
-  Queries for checking GL extension and command support
-  Helper functions to map enum values to/from zero based integers, to help with ImGui, hashing, serialization

## erhe::graphics namespace

`erhe::graphics` namespace provides classes basic 3D rendering with modern OpenGL.

Currently, erhe uses OpenGL as graphics API. The `erhe::graphics` builds a vulkan-like
abstraction on top of OpenGL:

-  `erhe::graphics::Pipeline` capsulates all relevant GL state.

## erhe::log namespace

`erhe::log` namespace provides helpers / wrappers for spdlog logging/

## erhe::primitive namespace

`erhe::primitive` namespace provides classes to convert `erhe::geometry::Geometry`
to renderable vertex and index buffers.

## erhe::scene namespace

`erhe::scene` namespace provides classes for basic 3D scene graph.

Warning: `erhe::physics` is in early, experimental stages.

## erhe::toolkit namespace

`erhe::toolkit` namespace provides windowing system abstraction, currently
using GLFW3, and some small helper functions.

Also included are macros `VERIFY(condition)` and `FATAL(format, ...)` which
can be used in place of `assert()` and unrecoverable error.

## erhe::physics namespace

`erhe::physics` namespace provides minimal abstraction / wrappers for Bullet / Jolt.
The Bullet physics backend is more complete. The Jolt physics backend is barely started.
Both will need more work.

Warning: `erhe::physics` is in early, experimental stages.

## erhe::application namespace

`erhe::application` contains code that can be shared with multiple applications. Features
contained in this library may eventually end up elsewhere.

Some features:

-  Commands that can be bound to input events (key, mouse events)
-  OpenGL context provided for multi-threaded loading of OpenGL resources
-  ImGui window wrapper/abstraction, including custom ImGui backend / renderer using erhe
-  Shader hot-reloading support `Shader_monitor`

Warning: `erhe::application` is in early, experimental stages.
