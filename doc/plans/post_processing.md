# Post-processing follow-ups

Status: proposed

Extends [../post_processing.md](../post_processing.md), which describes the
bloom mip chain and the render-pass synchronization the passes rely on.

## Debug per-mip barrier tracker

A debug-only per-mip state tracker on `Texture_impl` that asserts when a mip is
sampled without a barrier since its last write would catch a missing barrier at
the point it is introduced, instead of as a driver-dependent artifact. It is a
validation aid only: barrier placement stays driven by the attachment
descriptors' `usage_before` / `usage_after`, never by the tracker's state.
