# Procedural sky verification

Status: proposed

Extends [../procedural_sky.md](../procedural_sky.md), which describes the
atmosphere sky mode that exists today. Both items are verification gaps, not
missing code.

## Metal runtime verification

The Metal backend implements storage-image compute (`setTexture` at the raw
binding point, the bind-group-layout mirroring, `MTL::TextureUsageShaderWrite`),
so the atmosphere is wired on Metal, but it has not been run on an M-series Mac.
Build `build_xcode_metal`, set Sky Mode = 1 and walk the verification list in
the current document. A Tier-1 Metal GPU additionally needs the
readonly / writeonly qualifier work described under "Risks and tuning knobs"
there.

## Multiview (Quest) verification

The atmosphere shader is compiled with the session `view_count` and the camera
UBO is written per view, but per-eye correctness has not been confirmed on
device. Run the headset paths and check that both eyes agree on the sun position
and horizon.
