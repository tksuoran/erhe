# Geometry spreadsheet window: remaining work

Status: in progress

This plan extends `doc/editor/geometry_spreadsheet.md` with the large-mesh
performance check of the window.

## 1. Requirements

- R8 Performance, measured on a mesh with 1,000,000 vertices and ~6,000,000
  corners:
  - a steady-state frame with the window open does work proportional to the
    visible cells only, and does no heap allocation;
  - row caches are rebuilt only on change events;
  - a frame with the window hidden costs nothing.

## 2. Phases

Each phase ends with a build of every target listed in `AGENTS.md`
"Building", a headless MCP verification, and one commit.

1. **Performance check (R8).** Load or create a mesh with 1,000,000
   vertices and open the window on the Corner tab:
   - use Tracy to confirm that the window's zone time is flat when scrolling
     from the top of the table to the bottom;
   - use Tracy to confirm that no allocations show in steady-state frames;
   - confirm that a hidden window records no zone.

   Record the measured numbers in `doc/editor/geometry_spreadsheet.md`.

## 3. Documentation on landing

- The measured numbers go into `doc/editor/geometry_spreadsheet.md`
  section 3, and this plan is deleted.
