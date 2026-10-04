# erhe_version

Stability: stable

## Purpose

The build's identity: the project version of the root `CMakeLists.txt`
(`project(VERSION)`) and the source tree's `git describe --tags --always --dirty`,
compiled into one translation unit that is regenerated at every build. The
editor logs `get_description()` as its first startup line, beside the
dependency commits CMake captures (`ERHE_GEOGRAM_GIT_COMMIT`,
`ERHE_BVH_GIT_COMMIT`), so every `logs/log.txt` names the code that wrote it.

## Public API

`erhe_version/version.hpp`, namespace `erhe::version`:

- `get_project_version()` - `"1.0"`, the `project(VERSION)` value.
- `get_git_describe()` - the `git describe` output, e.g. `414c4e285` on an
  untagged commit, `v1.0-12-g414c4e2-dirty` past a tag with uncommitted
  changes; `"unknown"` when the tree is not a git checkout or git is not
  found.
- `get_description()` - `"erhe <project version> (<git describe>)"`.

## How it is generated

`src/erhe/version/generate_version.cmake` is a CMake script that writes
`<build>/src/erhe/version/version.cpp` from the three inputs
(`ERHE_VERSION_OUTPUT`, `ERHE_PROJECT_VERSION`, `ERHE_SOURCE_DIR`). The
library's `CMakeLists.txt` runs it once at configure time, so the file
exists when the generator runs, and the `erhe_version_stamp` custom target
runs it at every build (`BYPRODUCTS version.cpp`, `erhe_version` depends on
it). The script stages the content and copies it over the output with
`configure_file(... COPYONLY)`, which leaves the file untouched when the
content is unchanged, so a build at the same commit recompiles nothing and a
new commit recompiles one translation unit.

## Dependencies

None.
