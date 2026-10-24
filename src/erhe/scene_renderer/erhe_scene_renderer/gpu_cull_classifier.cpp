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
    , m_compact_templates_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_templates",
            .binding_point = static_cast<int>(compact_templates_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_compact_jobs_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_jobs",
            .binding_point = static_cast<int>(compact_jobs_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_compact_masks_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_masks",
            .binding_point = static_cast<int>(compact_masks_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_compact_commands_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_commands",
            .binding_point = static_cast<int>(compact_commands_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
    , m_compact_counts_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_counts",
            .binding_point = static_cast<int>(compact_counts_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
    , m_compact_bits_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_bits",
            .binding_point = static_cast<int>(compact_bits_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_job_buffer     {graphics_device, erhe::graphics::Buffer_target::storage, "Gpu_cull_classifier jobs", compact_jobs_binding_point}
    , m_bits_buffer    {graphics_device, erhe::graphics::Buffer_target::storage, "Gpu_cull_classifier pass bits"}
    , m_command_buffer {graphics_device, erhe::graphics::Buffer_target::storage, "Gpu_cull_classifier commands"}
    , m_count_buffer   {graphics_device, erhe::graphics::Buffer_target::storage, "Gpu_cull_classifier counts"}
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
    // cull_compact.comp documents these; std430 again (uvec4 and uint
    // arrays, 16 and 4 byte strides).
    m_compact_templates_block.add_uvec4("templates", Shader_resource::unsized_array);
    m_job_header_offset = m_compact_jobs_block.add_uvec4("header"                                )->get_offset_in_parent();
    m_job_filter_offset = m_compact_jobs_block.add_uvec4("filter_masks", 2 * max_job_filters          )->get_offset_in_parent();
    m_job_data_offset   = m_compact_jobs_block.add_uvec4("jobs",   Shader_resource::unsized_array)->get_offset_in_parent();
    m_compact_masks_block.add_uvec2("masks", Shader_resource::unsized_array);
    m_compact_commands_block.add_uint("commands", Shader_resource::unsized_array);
    m_compact_counts_block.add_uint("counts", Shader_resource::unsized_array);
    m_compact_bits_block.add_uint("words", Shader_resource::unsized_array);

    if (!m_device_capable) {
        log_startup->info("GPU culling: not supported on this device (needs native multi-draw indirect with a GPU-read draw count and Record_indexing::first_instance); the CPU path classifies and writes the draw commands");
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
        return;
    }

    m_compact_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                { .binding_point = compact_templates_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = compact_jobs_binding_point,      .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = compact_masks_binding_point,     .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = compact_commands_binding_point,  .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = compact_counts_binding_point,    .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = compact_bits_binding_point,      .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute }
            },
            .debug_label       = "Gpu_cull_classifier compaction",
            .uses_texture_heap = false
        }
    );
    // The two passes of cull_compact.comp, one bind group layout.
    const auto make_compact_pass = [&](
        const char*                                                pass_name,
        const char*                                                pass_define,
        std::unique_ptr<erhe::graphics::Reloadable_shader_stages>& stages,
        std::unique_ptr<erhe::graphics::Compute_pipeline>&         pipeline
    ) -> bool {
        stages = std::make_unique<Reloadable_shader_stages>(
            graphics_device,
            Shader_stages_create_info{
                .name              = pass_name,
                .defines           = {
                    { "ERHE_CULL_COMPACT_GROUP_SIZE", fmt::format("{}", compact_workgroup_size) },
                    { "ERHE_CULL_COMPACT_PASS",       pass_define }
                },
                .interface_blocks  = { &m_compact_templates_block, &m_compact_jobs_block, &m_compact_masks_block, &m_compact_commands_block, &m_compact_counts_block, &m_compact_bits_block },
                .shaders           = { { Shader_type::compute_shader, shader_path / std::filesystem::path{"cull_compact.comp"} } },
                .bind_group_layout = m_compact_bind_group_layout.get()
            }
        );
        graphics_device.get_shader_monitor().add(*stages);
        if (!stages->shader_stages.is_valid()) {
            log_startup->error("GPU culling: cull_compact.comp ({}) failed to build; the CPU path classifies and writes the draw commands", pass_name);
            return false;
        }
        pipeline = std::make_unique<Compute_pipeline>(
            graphics_device,
            Compute_pipeline_data{
                .name              = pass_name,
                .shader_stages     = &stages->shader_stages,
                .bind_group_layout = m_compact_bind_group_layout.get()
            }
        );
        if (!pipeline->is_valid()) {
            log_startup->error("GPU culling: the {} compute pipeline is not valid; the CPU path classifies and writes the draw commands", pass_name);
            pipeline.reset();
            return false;
        }
        return true;
    };
    if (
        !make_compact_pass("cull_compact_bits", "0", m_compact_bits_shader_stages, m_compact_bits_pipeline) ||
        !make_compact_pass("cull_compact",      "1", m_compact_shader_stages,      m_compact_pipeline)
    ) {
        m_compact_bits_pipeline.reset();
        m_compact_pipeline.reset();
        m_pipeline.reset();
    }
}

Gpu_cull_classifier::~Gpu_cull_classifier() noexcept = default;

auto Gpu_cull_classifier::is_device_capable(const erhe::graphics::Device& graphics_device) -> bool
{
    const erhe::graphics::Device_info& info = graphics_device.get_info();
    return
        !info.emulate_multi_draw_indirect &&
        info.use_draw_indirect_count &&
        (info.record_indexing == erhe::graphics::Record_indexing::first_instance) &&
        (info.max_compute_shader_storage_blocks >= 6);
}

auto Gpu_cull_classifier::is_supported() const -> bool
{
    return m_device_capable && (m_pipeline != nullptr) && (m_compact_bits_pipeline != nullptr) && (m_compact_pipeline != nullptr);
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

auto Gpu_cull_classifier::get_compact_bind_group_layout() const -> const erhe::graphics::Bind_group_layout*
{
    return m_compact_bind_group_layout.get();
}

auto Gpu_cull_classifier::get_compact_bits_pipeline() const -> const erhe::graphics::Compute_pipeline&
{
    ERHE_VERIFY(m_compact_bits_pipeline);
    return *m_compact_bits_pipeline;
}

auto Gpu_cull_classifier::get_compact_pipeline() const -> const erhe::graphics::Compute_pipeline&
{
    ERHE_VERIFY(m_compact_pipeline);
    return *m_compact_pipeline;
}

auto Gpu_cull_classifier::get_job_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_job_buffer;
}

auto Gpu_cull_classifier::get_bits_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_bits_buffer;
}

auto Gpu_cull_classifier::get_command_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_command_buffer;
}

auto Gpu_cull_classifier::get_count_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_count_buffer;
}

} // namespace erhe::scene_renderer
