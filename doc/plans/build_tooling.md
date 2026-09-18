# Build tooling: make a stale VS build fail loudly

Status: proposed

Extends `doc/msvc_build_issues.md`, which describes how a Visual Studio
incremental build can link mixed-vintage objects into an ODR-violating
executable, and how to diagnose one. Nothing here is implemented; the
diagnosis recipe is all that stands today.

## Disable the IDE fast up-to-date check for heavyweight targets

`set_property(TARGET editor PROPERTY VS_GLOBAL_DisableFastUpToDateCheck true)`,
in `erhe_target_settings()` or for `editor` alone. MSBuild's tlog check still
runs, so builds stay incremental; this removes only the IDE-level shortcut that
provably lies. Highest value, lowest cost of the options here.

## An ODR canary for the highest-fan-out layout

Give `App_message_bus` a `std::size_t m_size_marker{0}` set to
`sizeof(App_message_bus)` in its constructor (compiled in
`app_message_bus.cpp`), and have `Editor` init verify
`ERHE_VERIFY(bus->m_size_marker == sizeof(App_message_bus))` (compiled in
`editor.cpp`). A mixed-layout executable then dies at launch with a clear
message instead of an undebuggable crash. This is the struct both observed
chimera crashes went through.

## A workflow rule in AGENTS.md

State that after changing a widely included editor header, or after git
operations with VS open, the VS IDE incremental build is not to be trusted:
use the cmake CLI build or a rebuild.

## Rejected

- Dropping `/MP`: the build-time cost far exceeds the risk removed.
- Abandoning the VS tree: it is needed for debugging.
