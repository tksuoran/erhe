[![Codacy Badge](https://app.codacy.com/project/badge/Grade/49fade7c78954f3a99a2d6ce84a9bc1a)](https://www.codacy.com/gh/tksuoran/erhe/dashboard?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=tksuoran/erhe&amp;utm_campaign=Badge_Grade)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

# erhe

erhe is a C++ library for modern OpenGL experiments.

-   Use direct state access (DSA)
-   Use persistently mapped buffers
-   Use multi draw indirect
-   Use latest OpenGL and GLSL versions
-   OpenGL state can be managed with pipelines a bit similar to Vulkan
-   Simple type safe(r) wrapper for GL API (see erhe::gl)
-   Supports Windows and Linux

erhe is evolution of RenderStack <https://github.com/tksuoran/RenderStack>

## erhe::components namespace

erhe::components namespace provides classes to manage components.
Components have connect() and initialize() methods which are
automatically called in correct order, based on declared
dependencies.

### erhe::components::Component

-   Provides services other component

-   Uses phased construction to make sure component initializations
    are done automatically in correct order

    -   C++ constructor
        -   Other components might not yet be even created
        -   Can do initialization which do not depend on other components

    -   connect():

        -   Other components can be fetched with get<> or require<>

        -   Other components have C++ constructors called, but they
            have not yet been initialized with Component::initialize()

        -   Components fetched with require<> are added as dependencies,
            and they will be initialized before Component::initialize()
            will be called.

    -   initialize():

        -   When initialize() is called for a component, then dependency components
            are guaranteed to be already initialized.

### erhe::components::Components

-   Maintains a set of components

-   Finds out the order which components must be initialized based on
    declared dependencies

### Usage

-   Construct all components, using Components::add(make_shared<>())

-   In each Component connect(), declare dependencies to other Components
    with Component::require<>()

-   Once all components have been added, call Components::initialize_components().
    This will call Component::connect() for all components, followed by
    Component::initialize() for each component, in order which respects
    all declared dependencies. If there are circular dependencies,
    initialize_components() will log and error and abort.

## erhe::geometry namespace

erhe::geometry namespace provides classes manipulating geometric, polygon
based 3D objects.

Geometry is collection of Points, Polygons, Corners and their attributes.

Arbitrary attributes can be associated with each Point, Polygon and Corner.

These classes are designed for manipulating 3D objects, not for rendering them.
See erhe::mesh how to render geometry objects.

Some features:

-  Catmull-Clark subdivision operation
-  Sqrt3 subdivision operation
-  Conway operators:
    -   Dual
    -   Ambo
    -   Truncate

## erhe::gl namespace

erhe::gl namespace provides python generated low level C++ wrappers for GL API.

## erhe::graphics namespace

erhe::graphics namespace provides classes basic 3D rendering with modern OpenGL.

## erhe::log namespace

erhe::log namespace provides classes to do very basic logging.

Also included are macros VERIFY(condition) and FATAL(format, ...) which
can be used in place of assert() and unrecoverable error.

## erhe::mesh namespace

erhe::mesh namespace provides classes to convert erhe::geometry::Geometry
to renderable vertex and index buffers.

## erhe::scene namespace

erhe::scene namespace provides classes for basic 3D scene graph.

## erhe::toolkit namespace

erhe::toolkit namespace provides windowing system abstraction, currently
using GLFW3, and some small helper functions.
