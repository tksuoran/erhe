#ifndef ERHE_TEMPORAL_HISTORY_GLSL
#define ERHE_TEMPORAL_HISTORY_GLSL

// Hysteresis of one item of a round-robin temporal integrator after a
// history reset (editor::Temporal_history in
// src/editor/renderers/indirect_diffuse.hpp, doc/editor/ddgi.md "History
// reset"). history = (reset origin item, items traced since the reset
// before this dispatch, item count, active). The item item_index is traced
// at dispatch slot slot; it was first traced after the reset at sequence
// position (item_index - origin) mod count, so this is its k-th trace since
// the reset, and the blend weight kept from its history is
// min(hysteresis, k / (k + 1)): 0 for the first trace (the stale history is
// replaced), then the running mean of the traces since the reset.
float temporal_history_hysteresis(uvec4 history, uint item_index, uint slot, float hysteresis)
{
    if (history.w == 0u) {
        return hysteresis;
    }
    uint  count    = history.z;
    uint  first    = (item_index + count - history.x) % count;
    uint  sequence = history.y + slot;
    uint  k        = (sequence >= first) ? ((sequence - first) / count) : 0u;
    float k_f      = float(k);
    return min(hysteresis, k_f / (k_f + 1.0));
}

#endif // ERHE_TEMPORAL_HISTORY_GLSL
