#include "erhe_scene_renderer/gpu_cull_classifier.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pipeline.hpp"
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

auto c_str(const Occlusion_culling occlusion_culling) -> const char*
{
    switch (occlusion_culling) {
        case Occlusion_culling::off: return "off";
        case Occlusion_culling::on:  return "on";
        default:                     return "?";
    }
}

auto c_str(const Pvs_culling pvs_culling) -> const char*
{
    switch (pvs_culling) {
        case Pvs_culling::off: return "off";
        case Pvs_culling::on:  return "on";
        default:               return "?";
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
    , m_occlusion_words_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_occlusion",
            .binding_point = static_cast<int>(occlusion_words_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
    , m_occlusion_previous_words_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_occlusion_previous",
            .binding_point = static_cast<int>(occlusion_previous_words_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_pvs_database_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_pvs",
            .binding_point = static_cast<int>(pvs_database_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_pvs_culled_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_pvs_culled",
            .binding_point = static_cast<int>(pvs_culled_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    }
    , m_pvs_columns_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_columns",
            .binding_point = static_cast<int>(pvs_columns_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_pvs_pages_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_pvs_pages",
            .binding_point = static_cast<int>(pvs_pages_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
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
    , m_occlusion_view_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "occlusion_view",
            .binding_point = static_cast<int>(occlusion_view_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::uniform_block
        }
    }
    , m_occlusion_bounds_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "cull_bounds",
            .binding_point = static_cast<int>(occlusion_bounds_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_occlusion_draw_test_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "occlusion_test",
            .binding_point = static_cast<int>(occlusion_draw_test_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }
    , m_occlusion_draw_result_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "occlusion_result",
            .binding_point = static_cast<int>(occlusion_draw_result_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
    , m_occlusion_view_buffer{graphics_device, erhe::graphics::Buffer_target::uniform, "Gpu_cull_classifier occlusion view", occlusion_view_binding_point}
{
    using namespace erhe::graphics;

    // Every member is a vec4 / uvec4 / uvec2 array: std430, which the
    // compute capable GLSL versions use for storage blocks, lays them out
    // tightly (16 and 8 byte strides). cull_classify.comp documents the
    // contents.
    m_bounds_block.add_vec4("bounds", Shader_resource::unsized_array);
    m_view_header_offset           = m_views_block.add_uvec4("header"          )->get_offset_in_parent();
    m_view_occlusion_header_offset = m_views_block.add_uvec4("occlusion_header")->get_offset_in_parent();
    m_view_pvs_header_offset       = m_views_block.add_uvec4("pvs_header"      )->get_offset_in_parent();
    m_view_pvs_cells_offset        = m_views_block.add_uvec4("pvs_cells"       )->get_offset_in_parent();
    m_view_pvs_pages_offset        = m_views_block.add_uvec4("pvs_pages"       )->get_offset_in_parent();
    m_view_pvs_grid_offset         = m_views_block.add_vec4 ("pvs_grid"        )->get_offset_in_parent();
    m_view_data_offset   = m_views_block.add_vec4 ("views", Shader_resource::unsized_array)->get_offset_in_parent();
    m_masks_block.add_uvec2("masks", Shader_resource::unsized_array);
    m_occlusion_words_block         .add_uint("words", Shader_resource::unsized_array);
    m_occlusion_previous_words_block.add_uint("words", Shader_resource::unsized_array);
    m_pvs_database_block            .add_uint("words",   Shader_resource::unsized_array);
    m_pvs_culled_block              .add_uint("words",   Shader_resource::unsized_array);
    m_pvs_columns_block             .add_uint("columns", Shader_resource::unsized_array);
    m_pvs_pages_block               .add_uint("slots",   Shader_resource::unsized_array);
    // The occlusion test (occlusion_test.vert / .frag): the view block
    // (std140: per view a mat4 and a vec4, then a uvec4), the bounds as the
    // classifier reads them, and the two occlusion regions as uint words
    // (the uvec2 layout's bytes).
    m_occlusion_clip_from_world_offset = m_occlusion_view_block.add_mat4 ("clip_from_world", occlusion_max_view_count)->get_offset_in_parent();
    m_occlusion_eye_offset             = m_occlusion_view_block.add_vec4 ("eye",             occlusion_max_view_count)->get_offset_in_parent();
    m_occlusion_info_offset            = m_occlusion_view_block.add_uvec4("info"           )->get_offset_in_parent();
    m_occlusion_view_block_bytes       = m_occlusion_view_block.get_size_bytes();
    m_occlusion_bounds_block     .add_vec4("bounds", Shader_resource::unsized_array);
    m_occlusion_draw_test_block  .add_uint("words",  Shader_resource::unsized_array);
    m_occlusion_draw_result_block.add_uint("words",  Shader_resource::unsized_array);
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
                { .binding_point = masks_binding_point,  .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = occlusion_words_binding_point,          .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = occlusion_previous_words_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = pvs_database_binding_point,             .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = pvs_culled_binding_point,               .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = pvs_columns_binding_point,              .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute },
                { .binding_point = pvs_pages_binding_point,                .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::compute }
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
            .interface_blocks  = {
                &m_bounds_block, &m_views_block, &m_masks_block, &m_occlusion_words_block, &m_occlusion_previous_words_block,
                &m_pvs_database_block, &m_pvs_culled_block, &m_pvs_columns_block, &m_pvs_pages_block
            },
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
        return;
    }

    // The raster occlusion test: storage reads in the vertex stage, storage
    // writes and atomics in the fragment stage.
    const Device_info& info = graphics_device.get_info();
    if (
        !info.fragment_stores_and_atomics ||
        (info.max_vertex_shader_storage_blocks < 2) ||
        (info.max_fragment_shader_storage_blocks < 1)
    ) {
        log_startup->info("GPU occlusion culling: not supported on this device (needs fragment shader storage writes and atomics)");
        return;
    }
    m_occlusion_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                { .binding_point = occlusion_view_binding_point,        .type = Binding_type::uniform_buffer, .stage_flags = Shader_stage_flags::vertex   },
                { .binding_point = occlusion_bounds_binding_point,      .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::vertex   },
                { .binding_point = occlusion_draw_test_binding_point,   .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::vertex   },
                { .binding_point = occlusion_draw_result_binding_point, .type = Binding_type::storage_buffer, .stage_flags = Shader_stage_flags::fragment }
            },
            .debug_label       = "Gpu_cull_classifier occlusion test",
            .uses_texture_heap = false
        }
    );
    m_occlusion_shader_stages = std::make_unique<Reloadable_shader_stages>(
        graphics_device,
        Shader_stages_create_info{
            .name              = "occlusion_test",
            .interface_blocks  = { &m_occlusion_view_block, &m_occlusion_bounds_block, &m_occlusion_draw_test_block, &m_occlusion_draw_result_block },
            .no_vertex_input   = true,
            .shaders           = {
                { Shader_type::vertex_shader,   shader_path / std::filesystem::path{"occlusion_test.vert"} },
                { Shader_type::fragment_shader, shader_path / std::filesystem::path{"occlusion_test.frag"} }
            },
            .bind_group_layout = m_occlusion_bind_group_layout.get()
        }
    );
    graphics_device.get_shader_monitor().add(*m_occlusion_shader_stages);
    if (!m_occlusion_shader_stages->shader_stages.is_valid()) {
        log_startup->error("GPU occlusion culling: occlusion_test.vert / .frag failed to build; no occlusion test");
        m_occlusion_shader_stages.reset();
        return;
    }
    // The multiview variant (ERHE_MULTIVIEW: gl_ViewIndex picks the view),
    // for the occlusion test inside a multiview render pass (the headset).
    // Multiview is a Vulkan-only capability (Device_info reflects it only
    // there); the other backends build no multiview variant.
#if defined(ERHE_GRAPHICS_API_VULKAN)
    if (info.max_multiview_view_count >= occlusion_max_view_count) {
        m_occlusion_multiview_shader_stages = std::make_unique<Reloadable_shader_stages>(
            graphics_device,
            Shader_stages_create_info{
                .name              = "occlusion_test_multiview",
                .interface_blocks  = { &m_occlusion_view_block, &m_occlusion_bounds_block, &m_occlusion_draw_test_block, &m_occlusion_draw_result_block },
                .no_vertex_input   = true,
                .shaders           = {
                    { Shader_type::vertex_shader,   shader_path / std::filesystem::path{"occlusion_test.vert"} },
                    { Shader_type::fragment_shader, shader_path / std::filesystem::path{"occlusion_test.frag"} }
                },
                .bind_group_layout = m_occlusion_bind_group_layout.get(),
                .view_count        = static_cast<uint32_t>(occlusion_max_view_count)
            }
        );
        graphics_device.get_shader_monitor().add(*m_occlusion_multiview_shader_stages);
        if (!m_occlusion_multiview_shader_stages->shader_stages.is_valid()) {
            log_startup->error("GPU occlusion culling: multiview occlusion_test.vert / .frag failed to build; no occlusion test in multiview passes");
            m_occlusion_multiview_shader_stages.reset();
        }
    }
#endif
    // Depth test against the view's depth (the device's depth direction:
    // a box face at or in front of the stored depth passes), no depth
    // write, no color writes: the box only probes visibility. The vertex
    // shader emits only the three faces toward the camera (mirrored per
    // axis, so their winding varies): no face is culled.
    m_occlusion_pipeline = std::make_unique<Base_render_pipeline>(
        graphics_device,
        Base_render_pipeline_create_info{
            .debug_label       = erhe::utility::Debug_label{"Occlusion test"},
            .input_assembly    = Input_assembly_state::triangle,
            .rasterization     = Rasterization_state::cull_mode_none,
            .depth_stencil     = Depth_stencil_state{
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = graphics_device.get_reverse_depth() ? Compare_operation::greater_or_equal : Compare_operation::less_or_equal,
                .stencil_test_enable = false
            },
            .bind_group_layout = m_occlusion_bind_group_layout.get(),
            .color_blend       = &Color_blend_state::color_writes_disabled
        }
    );
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

auto Gpu_cull_classifier::is_occlusion_supported() const -> bool
{
    return is_supported() && (m_occlusion_pipeline != nullptr) && (m_occlusion_shader_stages != nullptr);
}

auto Gpu_cull_classifier::get_occlusion_bind_group_layout() const -> const erhe::graphics::Bind_group_layout*
{
    return m_occlusion_bind_group_layout.get();
}

auto Gpu_cull_classifier::get_occlusion_pipeline() -> erhe::graphics::Base_render_pipeline&
{
    ERHE_VERIFY(m_occlusion_pipeline);
    return *m_occlusion_pipeline;
}

auto Gpu_cull_classifier::get_occlusion_shader_stages(const std::size_t view_count) const -> const erhe::graphics::Shader_stages*
{
    ERHE_VERIFY(m_occlusion_shader_stages);
    ERHE_VERIFY((view_count >= 1) && (view_count <= occlusion_max_view_count));
    if (view_count == 1) {
        return &m_occlusion_shader_stages->shader_stages;
    }
    return m_occlusion_multiview_shader_stages ? &m_occlusion_multiview_shader_stages->shader_stages : nullptr;
}

auto Gpu_cull_classifier::get_occlusion_view_buffer() -> erhe::graphics::Ring_buffer_client&
{
    return m_occlusion_view_buffer;
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
