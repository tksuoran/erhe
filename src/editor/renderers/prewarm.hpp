#pragma once

#include <functional>
#include <string_view>

namespace editor {

class App_context;

// Init-time prewarm of the GPU resources first-frame rendering would
// otherwise trigger lazily. Walks every Scene_root + its content
// library, computes the same Standard_variant_keys the runtime forward
// path would, and drives Standard_shader_variants::get_or_compile so
// the glslang -> SPIR-V -> vkCreateShaderModule cost lands in the init
// phase. The trailing m_graphics_device->wait_idle() at the end of
// editor init absorbs any GPU work the prewarm enqueues.
//
// Intended call site: editor.cpp's init flow, between
// run_startup_script() and the close+submit+wait_idle block. Calling
// from elsewhere is safe but the savings only matter at first-frame.
//
// Today this covers the `standard` shader's variant cache, the shadow
// depth pass, material- and brush-preview pipelines, and the
// driver-level VkPipeline cache warmup for the OpenXR multiview color
// pass (via Forward_renderer::Warmup_target). Single-view (desktop
// mirror, main viewport offscreen) pipeline-cache warmup is still a
// follow-up because the matching render passes are constructed lazily
// on first rendergraph execute and do not exist at this insertion
// point; the desktop path also lacks the 30 s OS interstitial pressure
// that motivates the Quest warmup. See doc/prewarm.md.
// init_message, when non-empty, is invoked once per Scene_root just
// before its forward-path prewarm_standard_variants() call, with the
// scene name as argument. Editor::Editor() routes this through
// Init_status_display so the user sees per-scene progress on the
// loading screen.
void prewarm_all(
    App_context&                                 context,
    const std::function<void(std::string_view)>& init_message = {}
);

} // namespace editor
