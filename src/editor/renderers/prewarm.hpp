#pragma once

#include <functional>
#include <string_view>

namespace editor {

class App_context;

// Init-time prewarm of the GPU resources first-frame rendering would
// otherwise trigger lazily. Walks every Scene_root + its content
// library, computes the Shader_keys the runtime forward path would,
// and resolves them through Shader_variant_cache so the
// glslang -> SPIR-V -> vkCreateShaderModule cost lands in the init
// phase. The trailing m_graphics_device->wait_idle() at the end of
// editor init absorbs any GPU work the prewarm enqueues.
//
// Intended call site: editor.cpp's init flow, between
// run_startup_script() and the close+submit+wait_idle block.
//
// Currently disabled (the body is a stub). See doc/prewarm.md for the
// design intent; re-enabling waits on the wider variant / mesh-memory
// rework. init_message, when non-empty, is invoked once per Scene_root
// with the scene name so Init_status_display can show per-scene
// progress on the loading screen.
void prewarm_all(
    App_context&                                 context,
    const std::function<void(std::string_view)>& init_message = {}
);

} // namespace editor
