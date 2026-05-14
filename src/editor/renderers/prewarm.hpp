#pragma once

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
// Today this covers the `standard` shader's variant cache. The shadow
// depth pass, material- and brush-preview pipelines, and the
// Vulkan-backend pipeline-cache warmup
// (Device::warmup_render_pipeline) are tracked as follow-ups in
// quest_fixes.md E3 and doc/prewarm.md.
void prewarm_all(App_context& context);

} // namespace editor
