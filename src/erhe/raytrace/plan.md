# Raytrace Backends: Implementation Status

## Completed

### Embree 4 backend (resurrected)
- Updated from Embree 3.13.3 to 4.4.0 (static lib, internal tasking, no TBB)
- Fixed all interface mismatches (return types, const-correctness, buffer API)
- Deleted `Embree_buffer` class; uses `rtcSetSharedGeometryBuffer` with `Cpu_buffer` + 16-byte tail padding
- Auto-commits geometry after `set_mask`/`enable`/`disable`; sets default mask `0xFFFFFFFF`
- All 27 unit tests pass

### tinybvh backend (new)
- Single-header library with BVH disk caching
- TLAS scene-level acceleration via `Tinybvh_scene::s_use_tlas` (default: on)
- Fixed direction normalization: tinybvh normalizes ray direction internally, backend compensates by scaling t values
- All 27 unit tests pass

### Multi-level instance nesting
- Fixed in both bvh and tinybvh backends: `intersect_instance()` now traverses sub-instances
- Hierarchy tests verify nested translation, rotation+translation, scale propagation, three-level nesting

### Unit tests
- 27 tests across 5 suites (geometry, hierarchy, masking, instance, scene)
- `scripts/test_raytrace_all_backends.bat` runs all backends (bvh, tinybvh, embree, none)

## Potential future work

### Embree performance
- Add dirty flag to scene -- skip `rtcCommitScene` when nothing changed
- Use `RTC_BUILD_QUALITY_REFIT` for instances (cheaper than full rebuild for transform-only changes)
- Batch mask/enable changes instead of auto-committing on each state change

### bvh backend scene acceleration
- Currently O(N) linear scan of instances; no scene-level BVH
- Would need custom BVH over instance AABBs (library doesn't provide TLAS)

### Settings integration
- Expose `Tinybvh_scene::s_use_tlas` as an editor setting via codegen serialization
