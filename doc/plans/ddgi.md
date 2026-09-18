# DDGI follow-ups

Status: proposed

Extends [../ddgi.md](../ddgi.md), which describes the probe volume, the trace /
blend / relocate passes and the runtime sampling that exist today.

## Sky radiance for escaping rays

Probe rays that escape the scene take the scene ambient as sky radiance. Sample
the `sky_atmosphere` LUTs instead (the `sky_sample_*` helpers in
`lightmap_baker.cpp` already do this for the lightmap gather), so an atmosphere
sky lights the probe field.

## Infinite bounces

Sample the previous frame's irradiance field at ray hits, so the field feeds
back into the trace and light bounces more than once.

## Change-driven hysteresis reset

Light and geometry edits converge through the hysteresis blend alone. Reset
hysteresis for a few frames when the scene changes, driven by a hash over light
state and content transforms (the same tiering idea as `Lightmap_baker`).

## Authored and cascaded volumes

Node-attached volumes, cascaded / camera-scrolling volumes and per-scene volume
overrides, in place of the single auto-fitted scene-wide volume.

## Probe-cubemap fallback for Quest and OpenGL

A rasterized probe-cubemap path for backends without ray query, so DDGI is not
limited to `Device_info::use_ray_query`.

## Specular reuse

Reuse the probe field for indirect specular, not only diffuse.

## Share `Scene_tlas` with the lightmap baker

`Lightmap_baker` keeps its own copy of the acceleration structure code because
its instance records carry texcoord-2 addresses. Extend `Scene_tlas` to carry
those addresses and delete the copy.
