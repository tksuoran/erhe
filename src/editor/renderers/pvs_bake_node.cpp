#include "renderers/pvs_bake_node.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/pvs_disocclusion_pipelines.hpp"
#include "erhe_scene_renderer/scene_pass_resources.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <array>
#include <cstring>
#include <filesystem>

namespace editor {

namespace {

// The dummy depth attachment: neither tested nor written (the class
// comment says why there is one).
constexpr erhe::dataformat::Format c_pvs_depth_format = erhe::dataformat::Format::format_d32_sfloat;


// The bake trace's depth pass fragment counters (doc/plans/pvs.md "Bake"
// 7): one word per frame entry after the layered buffer's three regions.
constexpr std::size_t c_trace_entry_capacity = std::size_t{1} << 20;

// The static set the faces draw (doc/plans/pvs.md D3), beyond the tracked
// column draw_pvs_face() requires: content the color passes draw.
constexpr erhe::Item_filter c_pvs_face_filter{
    .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::content,
    .require_at_least_one_bit_set   = 0u,
    .require_all_bits_clear         = 0u,
    .require_at_least_one_bit_clear = 0u
};

} // anonymous namespace

Pvs_bake_node::Pvs_bake_node(
    erhe::rendergraph::Rendergraph& rendergraph,
    App_context&                    context,
    const uint32_t                  face_resolution,
    const uint32_t                  tile_size,
    const uint32_t                  layer_count,
    const Pvs_trace_mode            trace_mode,
    const std::span<const glm::vec4> column_bounds
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, erhe::utility::Debug_label{"PVS bake"}}
    , m_context             {context}
    , m_face_resolution     {face_resolution}
    , m_tile_size           {tile_size}
    , m_layer_count         {layer_count}
    , m_trace_mode          {trace_mode}
    , m_words_per_layer     {static_cast<std::size_t>(face_resolution) * static_cast<std::size_t>(face_resolution)}
    , m_words_per_region    {static_cast<std::size_t>(face_resolution) * static_cast<std::size_t>(face_resolution) * static_cast<std::size_t>(layer_count)}
    , m_viewport            {0, 0, static_cast<int>(face_resolution), static_cast<int>(face_resolution)}
    , m_primitive_buffer    {*context.graphics_device, context.program_interface->primitive_interface}
    , m_draw_indirect_buffer{*context.graphics_device, context.program_interface->config.max_draw_count}
{
    using namespace erhe::graphics;
    ERHE_VERIFY((face_resolution >= 1) && (layer_count >= 1));
    // The words are indexed with 32 bits (standard.frag erhe_pvs_write()).
    ERHE_VERIFY((3 * m_words_per_region) < (std::size_t{1} << 32));

    Device& device = *m_context.graphics_device;
    const erhe::scene_renderer::Pvs_layered_interface& layered_interface = m_context.program_interface->pvs_layered_interface;
    const std::size_t byte_count = layered_interface.get_words_offset() + (((3 * m_words_per_region) + c_trace_entry_capacity) * sizeof(uint32_t));
    m_layered_buffer = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = byte_count,
            .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst | Buffer_usage::transfer_src,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = erhe::utility::Debug_label{"PVS layered buffer"}
        }
    );
    // The bake's columns: their world bounds (constant for the bake) and the
    // cell's column words.
    ERHE_VERIFY(!column_bounds.empty() && ((column_bounds.size() % 2u) == 0u));
    m_column_count      = static_cast<uint32_t>(column_bounds.size() / 2u);
    m_column_word_count = (m_column_count + 31u) / 32u;
    m_column_bounds_data.assign(column_bounds.begin(), column_bounds.end());
    m_column_bounds = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = m_column_bounds_data.size() * sizeof(glm::vec4),
            .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = erhe::utility::Debug_label{"PVS bake column bounds"}
        }
    );
    m_column_words = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = static_cast<std::size_t>(m_column_word_count) * sizeof(uint32_t),
            .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst | Buffer_usage::transfer_src,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = erhe::utility::Debug_label{"PVS bake column words"}
        }
    );
    m_disocclusion_pipelines = std::make_unique<erhe::scene_renderer::Pvs_disocclusion_pipelines>(
        device,
        std::filesystem::path{"res"} / std::filesystem::path{"shaders"}
    );
    m_disocclusion_buffers = std::make_unique<erhe::scene_renderer::Pvs_disocclusion_buffers>(device, face_resolution, tile_size, layer_count);
    m_cell_timer         = std::make_unique<Gpu_timer>(device, "PVS bake cell");
    m_depth_timer        = std::make_unique<Gpu_timer>(device, "PVS bake depth pass");
    m_id_timer           = std::make_unique<Gpu_timer>(device, "PVS bake id pass");
    m_tiles_timer        = std::make_unique<Gpu_timer>(device, "PVS bake tiles");
    m_map_timer          = std::make_unique<Gpu_timer>(device, "PVS bake map");
    m_layer_loop_timer   = std::make_unique<Gpu_timer>(device, "PVS bake layer loop");
    m_vote_timer         = std::make_unique<Gpu_timer>(device, "PVS bake vote");
    m_depth_texture = std::make_unique<Texture>(
        device,
        Texture_create_info{
            .device      = device,
            .usage_mask  = Image_usage_flag_bit_mask::depth_stencil_attachment,
            .type        = Texture_type::texture_2d,
            .pixelformat = c_pvs_depth_format,
            .use_mipmaps = false,
            .width       = static_cast<int>(face_resolution),
            .height      = static_cast<int>(face_resolution),
            .debug_label = "PVS bake depth (unused)"
        }
    );
    Render_pass_descriptor render_pass_descriptor;
    render_pass_descriptor.depth_attachment.texture       = m_depth_texture.get();
    render_pass_descriptor.depth_attachment.clear_value[0] = device.get_reverse_depth() ? 0.0 : 1.0;
    render_pass_descriptor.depth_attachment.load_action   = Load_action::Clear;
    render_pass_descriptor.depth_attachment.store_action  = Store_action::Dont_care;
    render_pass_descriptor.depth_attachment.usage_before  = Image_usage_flag_bit_mask::depth_stencil_attachment;
    render_pass_descriptor.depth_attachment.layout_before = Image_layout::depth_stencil_attachment_optimal;
    render_pass_descriptor.depth_attachment.usage_after   = Image_usage_flag_bit_mask::depth_stencil_attachment;
    render_pass_descriptor.depth_attachment.layout_after  = Image_layout::depth_stencil_attachment_optimal;
    render_pass_descriptor.render_target_width            = static_cast<int>(face_resolution);
    render_pass_descriptor.render_target_height           = static_cast<int>(face_resolution);
    render_pass_descriptor.debug_label                    = "PVS bake face";
    m_render_pass = std::make_unique<Render_pass>(device, render_pass_descriptor);

    // No depth test or write, no color writes; single-sided primitives cull
    // their back faces, which the lists' double_sided / negative_determinant
    // adjust (the pipeline selection of the id pass, Id_renderer).
    const bool y_flip = device.get_info().coordinate_conventions.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled;
    m_pipeline = std::make_unique<Base_render_pipeline>(
        device,
        Base_render_pipeline_create_info{
            .debug_label       = erhe::utility::Debug_label{"PVS bake"},
            .input_assembly    = Input_assembly_state::triangle,
            .rasterization     = Rasterization_state::cull_mode_back_ccw.with_winding_flip_if(y_flip),
            .depth_stencil     = Depth_stencil_state::depth_test_disabled_stencil_test_disabled,
            .bind_group_layout = m_context.program_interface->bind_group_layout.get(),
            .color_blend       = &Color_blend_state::color_writes_disabled
        }
    );
    log_pvs->info(
        "PVS bake node: {} x {} faces, tiles of {}, {} layers, layered buffer {} MB, disocclusion work buffers {} MB",
        face_resolution, face_resolution, tile_size, layer_count, byte_count / (1024 * 1024),
        (m_disocclusion_buffers->get_closure_bytes() + m_disocclusion_buffers->get_column_masks_bytes() +
         m_disocclusion_buffers->get_tile_masks_bytes() + m_disocclusion_buffers->get_lists_bytes()) / (1024 * 1024)
    );
}


Pvs_bake_node::~Pvs_bake_node() noexcept = default;

auto Pvs_bake_node::is_disocclusion_supported() const -> bool
{
    return m_disocclusion_pipelines && m_disocclusion_pipelines->is_supported();
}

void Pvs_bake_node::submit(Pvs_cell_job&& job)
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    ERHE_VERIFY(!m_pending_job.has_value());
    ERHE_VERIFY(job.scene_root && (job.columns != nullptr) && !job.faces.empty());
    ERHE_VERIFY(!job.readback_layer.has_value() || (job.faces.size() == 1));
    m_pending_job = std::move(job);
    set_enabled(true);
}

auto Pvs_bake_node::has_pending_job() const -> bool
{
    return m_pending_job.has_value();
}

void Pvs_bake_node::cancel_pending_job()
{
    m_pending_job.reset();
}

void Pvs_bake_node::take_completed(std::vector<Pvs_cell_record>& out)
{
    const std::lock_guard<std::mutex> lock{m_record_mutex};
    std::size_t taken = 0;
    while ((taken < m_records.size()) && m_context.graphics_device->is_frame_completed(m_records[taken].frame)) {
        out.push_back(std::move(m_records[taken]));
        ++taken;
    }
    m_records.erase(m_records.begin(), m_records.begin() + static_cast<std::ptrdiff_t>(taken));
}

auto Pvs_bake_node::has_frames_in_flight() const -> bool
{
    {
        const std::lock_guard<std::mutex> lock{m_record_mutex};
        if (!m_records.empty()) {
            return true;
        }
    }
    // A slot prepared for a frame the render phase has not recorded yet.
    for (const Frame_slot& slot : m_frame_slots) {
        if (slot.job.has_value() && !m_context.graphics_device->is_frame_completed(slot.frame_index)) {
            return true;
        }
    }
    return false;
}

auto Pvs_bake_node::get_gpu_times() const -> Pvs_bake_gpu_times
{
    return Pvs_bake_gpu_times{
        .cell         = m_cell_timer->last_result(),
        .depth_pass   = m_depth_timer->last_result(),
        .id_pass      = m_id_timer->last_result(),
        .tiles        = m_tiles_timer->last_result(),
        .layer_loop   = m_layer_loop_timer->last_result(),
        .map          = m_map_timer->last_result(),
        .vote         = m_vote_timer->last_result()
    };
}

void Pvs_bake_node::prepare_rendergraph_node(const uint64_t frame_index)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    const std::size_t frames_in_flight = m_context.graphics_device->get_number_of_frames_in_flight();
    ERHE_VERIFY(frames_in_flight > 0);
    if (m_frame_slots.size() != frames_in_flight) {
        m_frame_slots.resize(frames_in_flight);
    }
    Frame_slot& slot = m_frame_slots[frame_index % frames_in_flight];
    slot.frame_index = frame_index;
    slot.job.reset();
    slot.cull_views.clear();

    if (!m_pending_job.has_value()) {
        set_enabled(false);
        return;
    }
    // The frame draws the job once its draw lists publish the job's column
    // table (the scene's database is set at the next flush); until then
    // the job waits.
    const erhe::scene_renderer::Draw_list_scene* draw_list_scene = m_pending_job->scene_root->get_draw_list_scene();
    if ((draw_list_scene == nullptr) || (draw_list_scene->get_pvs_database().get() != m_pending_job->columns)) {
        return;
    }
    slot.job = std::move(m_pending_job);
    m_pending_job.reset();
}

void Pvs_bake_node::pre_execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    static_cast<void>(command_buffer);
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    erhe::graphics::Device& device      = *m_context.graphics_device;
    const uint64_t          frame_index = device.get_frame_index();
    if (m_frame_slots.empty()) {
        return;
    }
    Frame_slot& slot = m_frame_slots[frame_index % m_frame_slots.size()];
    if ((slot.frame_index != frame_index) || !slot.job.has_value()) {
        return;
    }
    // Each face's frustum as a culling view of the frame: entries wholly
    // outside it are not drawn by that face.
    const Pvs_cell_job&                    job             = slot.job.value();
    const erhe::graphics::Device_info&     info            = device.get_info();
    const erhe::math::Depth_range          depth_range     = info.coordinate_conventions.native_depth_range;
    const float                            clip_z_min      = (depth_range == erhe::math::Depth_range::zero_to_one) ? 0.0f : -1.0f;
    erhe::scene_renderer::Draw_list_scene* draw_list_scene = job.scene_root->get_draw_list_scene();
    ERHE_VERIFY(draw_list_scene != nullptr);
    slot.cull_views.clear();
    for (const Pvs_face_view& face : job.faces) {
        const glm::mat4 clip_from_world =
            face.camera.projection().clip_from_node_transform(m_viewport, device.get_reverse_depth(), depth_range, info.coordinate_conventions).get_matrix() *
            face.camera.node_from_world();
        const std::array<glm::vec4, 6> planes = erhe::math::extract_frustum_planes(clip_from_world, clip_z_min, 1.0f);
        slot.cull_views.push_back(
            draw_list_scene->add_cull_view(frame_index, erhe::scene_renderer::Draw_purpose::color, std::span<const glm::vec4>{planes})
        );
    }
}

void Pvs_bake_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;
    using Usage = Buffer_usage_flag_bit_mask;

    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    Device&        device      = *m_context.graphics_device;
    const uint64_t frame_index = device.get_frame_index();
    if (m_frame_slots.empty()) {
        return;
    }
    Frame_slot& slot = m_frame_slots[frame_index % m_frame_slots.size()];
    if ((slot.frame_index != frame_index) || !slot.job.has_value()) {
        return; // the job waits for its columns (prepare_rendergraph_node())
    }
    const Pvs_cell_job&                    job             = slot.job.value();
    erhe::scene_renderer::Draw_list_scene* draw_list_scene = job.scene_root->get_draw_list_scene();
    ERHE_VERIFY(draw_list_scene != nullptr);
    ERHE_VERIFY(m_context.forward_renderer != nullptr);
    ERHE_VERIFY(slot.cull_views.size() == job.faces.size());
    const bool bake_cell = !job.readback_layer.has_value();
    ERHE_VERIFY(!bake_cell || is_disocclusion_supported());

    const erhe::scene_renderer::Pvs_layered_interface& layered_interface = m_context.program_interface->pvs_layered_interface;
    const std::size_t words_offset = layered_interface.get_words_offset();
    const std::size_t region_bytes = m_words_per_region * sizeof(uint32_t);
    const std::size_t byte_count   = words_offset + (3 * region_bytes) + (c_trace_entry_capacity * sizeof(uint32_t));
    const bool        traced       = !job.trace_columns.empty();

    constexpr uint64_t layered_accesses =
        Usage::storage_read | Usage::storage_write | Usage::fragment_shader | Usage::compute_shader | Usage::transfer_src;
    constexpr uint64_t compute_access = Usage::storage_read | Usage::storage_write | Usage::compute_shader;

    Pvs_cell_record record{
        .frame          = frame_index,
        .cell           = job.cell,
        .face           = job.faces.front().face,
        .z_near         = job.faces.front().z_near,
        .z_far          = job.faces.front().z_far,
        .readback_layer = job.readback_layer
    };

    // A traced cell: the column of every frame-local entry (the id the
    // trace's id pass writes).
    if (traced) {
        draw_list_scene->get_published_pvs_entry_columns(frame_index, record.entry_columns);
    }
    const uint32_t column_word_count = m_column_word_count;
    record.column_word_count = column_word_count;

    std::optional<Scoped_gpu_timer> cell_timer_scope{};
    if (bake_cell) {
        cell_timer_scope.emplace(*m_cell_timer, command_buffer);
        // An earlier cell's column words (its passes and its copy) -> this
        // cell's clear.
        command_buffer.buffer_barrier(*m_column_words, compute_access | Usage::transfer_src, Usage::transfer_dst, Barrier_scope::earlier_submissions);
        {
            Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
            blit.fill_buffer(m_column_words.get(), 0, static_cast<std::size_t>(column_word_count) * sizeof(uint32_t), 0x00u);
        }
        command_buffer.buffer_barrier(*m_column_words, Usage::transfer_dst, compute_access);
    }

    if (!m_header_written) {
        // The layered buffer's header (Pvs_layered_interface: info, trace)
        // is constant for the bake. upload_to_buffer() may write it at
        // record time (a host copy on Metal), so it is written once, here,
        // in the node's first recorded frame: no frame used the buffer
        // before. Nothing per face may be written this way: every frame in
        // flight shares the buffer (the face's depth range comes from its
        // camera block).
        std::array<std::byte, 32> header{};
        const std::array<uint32_t, 4> info{
            m_face_resolution,
            m_layer_count,
            static_cast<uint32_t>(m_words_per_layer),
            static_cast<uint32_t>(m_words_per_region)
        };
        const std::array<uint32_t, 4> trace{
            (m_trace_mode == Pvs_trace_mode::on) ? 1u : 0u,
            static_cast<uint32_t>(c_trace_entry_capacity),
            0u,
            0u
        };
        std::memcpy(header.data() + layered_interface.get_info_offset(),  info.data(),  sizeof(info));
        std::memcpy(header.data() + layered_interface.get_trace_offset(), trace.data(), sizeof(trace));
        command_buffer.upload_to_buffer(*m_layered_buffer, 0, header.data(), header.size());
        // The column bounds are constant for the bake too.
        command_buffer.upload_to_buffer(*m_column_bounds, 0, m_column_bounds_data.data(), m_column_bounds_data.size() * sizeof(glm::vec4));
        command_buffer.buffer_barrier(*m_column_bounds, Usage::transfer_dst, Usage::storage_read | Usage::compute_shader);
        m_header_written = true;
    }

    erhe::scene_renderer::Scene_pass_resources& pass_resources = m_context.forward_renderer->get_pass_resources();
    const erhe::scene::Layer_id content_layers[] = { job.scene_root->layers().content()->id };

    for (std::size_t face_index = 0, face_end = job.faces.size(); face_index < face_end; ++face_index) {
        const Pvs_face_view&                    face      = job.faces[face_index];
        const erhe::scene_renderer::Cull_view   cull_view = slot.cull_views[face_index];
        const bool                              timed     = (face_index == 0);

        // The earlier faces' accesses (their passes, a readback's copy) ->
        // this face's clear.
        command_buffer.buffer_barrier(*m_layered_buffer, layered_accesses, Usage::transfer_dst, Barrier_scope::earlier_submissions);
        {
            // depth and id_min to 0xffffffff, id_max to 0.
            Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
            blit.fill_buffer(m_layered_buffer.get(), words_offset, 2 * region_bytes, 0xffu);
            blit.fill_buffer(m_layered_buffer.get(), words_offset + (2 * region_bytes), region_bytes, 0x00u);
            if (m_trace_mode == Pvs_trace_mode::on) {
                blit.fill_buffer(m_layered_buffer.get(), words_offset + (3 * region_bytes), c_trace_entry_capacity * sizeof(uint32_t), 0x00u);
            }
        }
        command_buffer.buffer_barrier(*m_layered_buffer, Usage::transfer_dst, Usage::storage_read | Usage::storage_write | Usage::fragment_shader);

        const erhe::scene_renderer::Camera_view_input view{.camera = &face.camera, .viewport = m_viewport, .pixel_scale = 1.0f};
        const auto draw_pass = [&](const erhe::scene_renderer::Pvs_sub_variant sub_variant, Gpu_timer* timer) -> erhe::scene_renderer::Draw_statistics {
            std::optional<Scoped_gpu_timer> timer_scope{};
            if (timer != nullptr) {
                timer_scope.emplace(*timer, command_buffer);
            }
            Render_command_encoder encoder = device.make_render_command_encoder(command_buffer);
            Scoped_render_pass     scoped_render_pass{*m_render_pass, command_buffer};
            encoder.set_viewport_rect(m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);
            encoder.set_scissor_rect (m_viewport.x, m_viewport.y, m_viewport.width, m_viewport.height);
            const erhe::scene_renderer::Base_render_parameters base{
                .render_encoder  = encoder,
                .render_pass     = m_render_pass.get(),
                .viewport        = m_viewport,
                .views           = std::span<const erhe::scene_renderer::Camera_view_input>{&view, 1},
                .material_source = &draw_list_scene->get_material_set(),
                .frame_number    = frame_index,
                .reverse_depth   = device.get_reverse_depth(),
                .depth_range     = device.get_info().coordinate_conventions.native_depth_range,
                .conventions     = device.get_info().coordinate_conventions,
                .debug_label     = erhe::scene_renderer::c_str(sub_variant)
            };
            erhe::scene_renderer::Scene_pass_resources::Pass_state pass_state = pass_resources.begin_pass(base, glm::uvec4{0u}, std::span<const glm::vec4>{}, nullptr);
            encoder.set_buffer(Buffer_target::storage, m_layered_buffer.get(), 0, byte_count, pvs_layered_buffer_binding_point);
            const erhe::scene_renderer::Draw_statistics statistics = draw_list_scene->draw_pvs_face(
                erhe::scene_renderer::Draw_pvs_face_parameters{
                    .render_encoder       = encoder,
                    .render_pass          = m_render_pass.get(),
                    .base_render_pipeline = *m_pipeline,
                    .primitive_buffer     = m_primitive_buffer,
                    .draw_indirect_buffer = m_draw_indirect_buffer,
                    .filter               = c_pvs_face_filter,
                    .layers               = content_layers,
                    .sub_variant          = sub_variant,
                    .cull_view            = cull_view,
                    .frame_index          = frame_index
                }
            );
            pass_resources.end_pass(pass_state, encoder);
            return statistics;
        };

        record.depth_entries += draw_pass(erhe::scene_renderer::Pvs_sub_variant::depth, (timed && bake_cell) ? m_depth_timer.get() : nullptr).entry_count;
        // The depth pass's atomics -> the id pass's compare and atomics.
        command_buffer.buffer_barrier(*m_layered_buffer, Usage::storage_write | Usage::fragment_shader, Usage::storage_read | Usage::storage_write | Usage::fragment_shader);
        // The id pass: only the trace and pvs_debug_face read the ids (the
        // bounds vote needs none).
        if (traced || !bake_cell) {
            record.id_entries += draw_pass(erhe::scene_renderer::Pvs_sub_variant::id, (timed && bake_cell) ? m_id_timer.get() : nullptr).entry_count;
        }

        if (job.readback_layer.has_value()) {
            // pvs_debug_face: the layer's depth and id_min words -> the
            // host. A diagnostic path: it allocates.
            const uint32_t    layer       = std::min(job.readback_layer.value(), m_layer_count - 1);
            const std::size_t layer_bytes = m_words_per_layer * sizeof(uint32_t);
            record.readback_layer = layer;
            record.readback = std::make_unique<Buffer>(
                device,
                Buffer_create_info{
                    .capacity_byte_count                    = 2 * layer_bytes,
                    .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                    .usage                                  = Buffer_usage::transfer_dst,
                    .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read,
                    .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_cached | Memory_property_flag_bit_mask::host_persistent,
                    .debug_label                            = erhe::utility::Debug_label{"PVS debug face readback"}
                }
            );
            command_buffer.buffer_barrier(*m_layered_buffer, Usage::storage_write | Usage::fragment_shader, Usage::transfer_src);
            {
                Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
                const std::size_t layer_offset = static_cast<std::size_t>(layer) * layer_bytes;
                blit.copy_from_buffer(m_layered_buffer.get(), words_offset + layer_offset,                record.readback.get(), 0,           layer_bytes);
                blit.copy_from_buffer(m_layered_buffer.get(), words_offset + region_bytes + layer_offset, record.readback.get(), layer_bytes, layer_bytes);
            }
            command_buffer.buffer_barrier(*record.readback, Usage::transfer_dst, Usage::host_read);
            continue;
        }

        if (traced && job.trace_depth_face.has_value() && (face.face == job.trace_depth_face.value()) && (face.sample == 0u)) {
            // The trace's depth dump. A diagnostic path: it allocates.
            record.trace_depth_face = face.face;
            record.trace_depth = std::make_unique<Buffer>(
                device,
                Buffer_create_info{
                    .capacity_byte_count                    = region_bytes,
                    .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                    .usage                                  = Buffer_usage::transfer_dst,
                    .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read,
                    .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_cached | Memory_property_flag_bit_mask::host_persistent,
                    .debug_label                            = erhe::utility::Debug_label{"PVS trace depth dump"}
                }
            );
            command_buffer.buffer_barrier(*m_layered_buffer, Usage::storage_write | Usage::fragment_shader, Usage::transfer_src);
            {
                Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
                blit.copy_from_buffer(m_layered_buffer.get(), words_offset, record.trace_depth.get(), 0, region_bytes);
            }
            command_buffer.buffer_barrier(*record.trace_depth, Usage::transfer_dst, Usage::host_read);
            command_buffer.buffer_barrier(*m_layered_buffer, Usage::transfer_src, Usage::storage_read | Usage::compute_shader);
        }

        // The raster passes' writes -> the disocclusion passes' reads.
        command_buffer.buffer_barrier(*m_layered_buffer, Usage::storage_write | Usage::fragment_shader, Usage::storage_read | Usage::compute_shader);
        // Member scratch: the layers vector keeps its capacity across faces.
        erhe::scene_renderer::Pvs_disocclusion_parameters& parameters = m_face_parameters;
        erhe::scene_renderer::make_pvs_disocclusion_parameters(face.geometry, m_column_count, parameters);
        const erhe::scene_renderer::Pvs_disocclusion_timers timers{
            .tiles        = m_tiles_timer.get(),
            .layer_loop   = m_layer_loop_timer.get(),
            .map          = m_map_timer.get(),
            .vote         = m_vote_timer.get()
        };
        m_disocclusion_pipelines->record_face(
            command_buffer,
            *m_disocclusion_buffers,
            *m_layered_buffer,
            byte_count,
            parameters,
            *m_column_bounds,
            m_column_bounds_data.size() * sizeof(glm::vec4),
            *m_column_words,
            timed ? &timers : nullptr
        );
        if (traced) {
            record_trace(command_buffer, job, face, parameters, record, byte_count);
        }
    }

    if (bake_cell) {
        // The cell's column words -> the host. An offline path: it allocates.
        const std::size_t column_bytes = static_cast<std::size_t>(column_word_count) * sizeof(uint32_t);
        record.column_words = std::make_unique<Buffer>(
            device,
            Buffer_create_info{
                .capacity_byte_count                    = column_bytes,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::transfer_dst,
                .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read,
                .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_cached | Memory_property_flag_bit_mask::host_persistent,
                .debug_label                            = erhe::utility::Debug_label{"PVS bake column words readback"}
            }
        );
        command_buffer.buffer_barrier(*m_column_words, Usage::storage_write | Usage::compute_shader, Usage::transfer_src);
        {
            Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
            blit.copy_from_buffer(m_column_words.get(), 0, record.column_words.get(), 0, column_bytes);
        }
        command_buffer.buffer_barrier(*record.column_words, Usage::transfer_dst, Usage::host_read);
        cell_timer_scope.reset();
    }

    const std::lock_guard<std::mutex> lock{m_record_mutex};
    m_records.push_back(std::move(record));
}

void Pvs_bake_node::record_trace(
    erhe::graphics::Command_buffer&                          command_buffer,
    const Pvs_cell_job&                                      job,
    const Pvs_face_view&                                     face,
    const erhe::scene_renderer::Pvs_disocclusion_parameters& parameters,
    Pvs_cell_record&                                         record,
    const std::size_t                                        layered_byte_count
)
{
    using namespace erhe::graphics;
    using Usage = Buffer_usage_flag_bit_mask;
    Device& device = *m_context.graphics_device;

    // The trace table: per frame entry its slot (the index of its column in
    // trace_columns), then four zeroed counters per slot.
    const uint32_t entry_count = static_cast<uint32_t>(std::min(record.entry_columns.size(), c_trace_entry_capacity));
    const uint32_t slot_count  = static_cast<uint32_t>(job.trace_columns.size());
    std::vector<uint32_t> table(1u + entry_count + (4u * slot_count), 0u);
    table[0] = entry_count;
    for (uint32_t entry = 0; entry < entry_count; ++entry) {
        uint32_t slot = 0xffffffffu;
        for (uint32_t i = 0; i < slot_count; ++i) {
            if (job.trace_columns[i] == record.entry_columns[entry]) {
                slot = i;
                break;
            }
        }
        table[1u + entry] = slot;
    }
    const std::size_t trace_bytes = table.size() * sizeof(uint32_t);
    std::unique_ptr<Buffer> trace_buffer = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = trace_bytes,
            .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst | Buffer_usage::transfer_src,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = erhe::utility::Debug_label{"PVS bake trace"}
        }
    );
    command_buffer.upload_to_buffer(*trace_buffer, 0, table.data(), trace_bytes);
    command_buffer.buffer_barrier(*trace_buffer, Usage::transfer_dst, Usage::storage_read | Usage::storage_write | Usage::compute_shader);
    m_disocclusion_pipelines->record_trace(command_buffer, *m_disocclusion_buffers, *m_layered_buffer, layered_byte_count, parameters, *trace_buffer, trace_bytes);

    // Everything the face wrote -> the copies into the readback.
    const erhe::scene_renderer::Pvs_disocclusion_buffers& buffers = *m_disocclusion_buffers;
    constexpr uint64_t compute_access = Usage::storage_read | Usage::storage_write | Usage::compute_shader;
    command_buffer.buffer_barrier(*trace_buffer,             compute_access, Usage::transfer_src);
    command_buffer.buffer_barrier(*m_layered_buffer,         Usage::storage_read | Usage::storage_write | Usage::fragment_shader | Usage::compute_shader, Usage::transfer_src);
    command_buffer.buffer_barrier(*m_column_words,           compute_access, Usage::transfer_src);
    command_buffer.buffer_barrier(buffers.get_closure(),     compute_access, Usage::transfer_src);
    command_buffer.buffer_barrier(buffers.get_tile_masks(),  compute_access, Usage::transfer_src);
    command_buffer.buffer_barrier(buffers.get_column_masks(),compute_access, Usage::transfer_src);

    Pvs_face_trace trace{
        .face             = face.face,
        .sample           = face.sample,
        .entry_count      = entry_count,
        .column_word_count = parameters.get_column_word_count(),
        .tile_count       = parameters.get_tile_count(),
        .tiles            = parameters.get_tiles()
    };
    const std::size_t counter_words = 4u * static_cast<std::size_t>(slot_count);
    const std::size_t column_words  = 2u * static_cast<std::size_t>(parameters.get_tiles_per_layer());
    trace.word_count = counter_words + entry_count + trace.column_word_count + trace.tile_count + (2u * static_cast<std::size_t>(trace.tile_count)) + column_words;
    trace.readback = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count                    = trace.word_count * sizeof(uint32_t),
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_cached | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = erhe::utility::Debug_label{"PVS bake trace readback"}
        }
    );
    {
        const erhe::scene_renderer::Pvs_layered_interface& layered_interface = m_context.program_interface->pvs_layered_interface;
        const std::size_t counter_offset = layered_interface.get_words_offset() + (3 * m_words_per_region * sizeof(uint32_t));
        Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
        std::size_t offset = 0;
        const auto copy = [&](const Buffer* source, const std::size_t source_offset, const std::size_t words) {
            if (words > 0) {
                blit.copy_from_buffer(source, source_offset, trace.readback.get(), offset, words * sizeof(uint32_t));
            }
            offset += words * sizeof(uint32_t);
        };
        copy(trace_buffer.get(),            (1u + static_cast<std::size_t>(entry_count)) * sizeof(uint32_t), counter_words);
        copy(m_layered_buffer.get(),        counter_offset, entry_count);
        copy(m_column_words.get(),          0, trace.column_word_count);
        copy(&buffers.get_closure(),        0, trace.tile_count);
        copy(&buffers.get_tile_masks(),     0, 2u * static_cast<std::size_t>(trace.tile_count));
        copy(&buffers.get_column_masks(),   0, column_words);
    }
    command_buffer.buffer_barrier(*trace.readback, Usage::transfer_dst, Usage::host_read);
    // The trace buffer lives with the record, released once its frame
    // completed.
    trace.trace_buffer = std::move(trace_buffer);
    record.traces.push_back(std::move(trace));
}

} // namespace editor
