#include "erhe_scene_renderer/gpu_cull_classifier.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::scene_renderer {

auto c_str(const Cull_classification classification) -> const char*
{
    switch (classification) {
        case Cull_classification::cpu: return "cpu";
        case Cull_classification::gpu: return "gpu";
        default:                       return "?";
    }
}

Gpu_cull_classifier::Gpu_cull_classifier(erhe::graphics::Device& graphics_device, const std::filesystem::path& shader_path)
    : m_graphics_device{graphics_device}
    , m_device_capable {is_device_capable(graphics_device)}
    , m_bounds_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_bounds",
            .binding_point = static_cast<int>(bounds_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_views_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_views",
            .binding_point = static_cast<int>(views_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_masks_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_masks",
            .binding_point = static_cast<int>(masks_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
    , m_view_buffer    {graphics_device, erhe::graphics::Buffer_target::storage,      "Gpu_cull_classifier views",         views_binding_point}
    , m_mask_buffer    {graphics_device, erhe::graphics::Buffer_target::storage,      "Gpu_cull_classifier masks"}
    , m_readback_buffer{graphics_device, erhe::graphics::Buffer_target::transfer_dst, "Gpu_cull_classifier mask readback"}
{
    using namespace erhe::graphics;

    // Every member is a vec4 / uvec4 / uvec2 array: std430, which the
    // compute capable GLSL versions use for storage blocks, lays them out
    // tightly (16 and 8 byte strides). cull_classify.comp documents the
    // contents.
    m_bounds_block.add_vec4("bounds", Shader_resource::unsized_array);
    m_view_header_offset = m_views_block.add_uvec4("header"                             )->get_offset_in_parent();
    m_view_data_offset   = m_views_block.add_vec4 ("views", Shader_resource::unsized_array)->get_offset_in_parent();
    m_masks_block.add_uvec2("masks", Shader_resource::unsized_array);

    if (!m_device_capable) {
        log_startup->info("GPU culling classification: not supported on this device (needs native multi-draw indirect and Record_indexing::first_instance); the CPU path classifies");
        return;
    }

    m_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                { .binding_point = bounds_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = views_binding_point,  .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = masks_binding_point,  .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute }
            },
            .debug_label       = "Gpu_cull_classifier",
            .uses_texture_heap = false
        }
    );

    m_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name              = "cull_classify",
            .defines           = { { "ERHE_CULL_CLASSIFY_GROUP_SIZE", fmt::format("{}", workgroup_size) } },
            .interface_blocks  = { &m_bounds_block, &m_views_block, &m_masks_block },
            .shaders           = { { Shader_type::compute_shader, shader_path / std::filesystem::path{"cull_classify.comp"} } },
            .bind_group_layout = m_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_shader_stages);
    if (!m_shader_stages->shader_stages.is_valid()) {
        log_startup->error("GPU culling classification: cull_classify.comp failed to build; the CPU path classifies");
        return;
    }

    m_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "cull_classify",
            .shader_stages     = &m_shader_stages->shader_stages,
            .bind_group_layout = m_bind_group_layout.get()
        }
    );
    if (!m_pipeline->is_valid()) {
        log_startup->error("GPU culling classification: the cull_classify compute pipeline is not valid; the CPU path classifies");
        m_pipeline.reset();
    }
}

Gpu_cull_classifier::~Gpu_cull_classifier() noexcept = default;

auto Gpu_cull_classifier::is_device_capable(const erhe::graphics::Device& graphics_device) -> bool
{
    const erhe::graphics::Device_info& info = graphics_device.get_info();
    return
        !info.emulate_multi_draw_indirect &&
        (info.record_indexing == erhe::graphics::Record_indexing::first_instance) &&
        (info.max_compute_shader_storage_blocks >= 3);
}

auto Gpu_cull_classifier::is_supported() const -> bool
{
    return m_device_capable && (m_pipeline != nullptr);
}

void Gpu_cull_classifier::add_pending_scene(Draw_list_scene& draw_list_scene)
{
    erhe::utility::Thread_roles::assert_render_thread();
    m_pending_scenes.push_back(&draw_list_scene);
}

void Gpu_cull_classifier::record_pending(erhe::graphics::Command_buffer& command_buffer)
{
    erhe::utility::Thread_roles::assert_render_thread();
    if (m_pending_scenes.empty()) {
        return;
    }
    ERHE_PROFILE_FUNCTION();
    for (Draw_list_scene* draw_list_scene : m_pending_scenes) {
        draw_list_scene->finish_gpu_cull_frame(command_buffer);
    }
    m_pending_scenes.clear();
}

auto Gpu_cull_classifier::get_device() -> erhe::graphics::Device&
{
    return m_graphics_device;
}

auto Gpu_cull_classifier::get_bind_group_layout() const -> const erhe::graphics::Bind_group_layout*
{
    return m_bind_group_layout.get();
}

auto Gpu_cull_classifier::get_pipeline() const -> const erhe::graphics::Compute_pipeline&
{
    ERHE_VERIFY(m_pipeline);
    return *m_pipeline;
}

auto Gpu_cull_classifier::get_view_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_view_buffer;
}

auto Gpu_cull_classifier::get_mask_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_mask_buffer;
}

auto Gpu_cull_classifier::get_readback_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_readback_buffer;
}

} // namespace erhe::scene_renderer
