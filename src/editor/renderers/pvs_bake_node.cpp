#include "renderers/pvs_bake_node.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
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
#include "erhe_scene_renderer/scene_pass_resources.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <array>
#include <cstring>

namespace editor {

namespace {

// The dummy depth attachment: neither tested nor written (the class
// comment says why there is one).
constexpr erhe::dataformat::Format c_pvs_depth_format = erhe::dataformat::Format::format_d32_sfloat;

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
    const uint32_t                  layer_count
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, erhe::utility::Debug_label{"PVS bake"}}
    , m_context             {context}
    , m_face_resolution     {face_resolution}
    , m_layer_count         {layer_count}
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
    const std::size_t byte_count = layered_interface.get_words_offset() + (3 * m_words_per_region * sizeof(uint32_t));
    m_layered_buffer = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count               = byte_count,
            .usage                             = Buffer_usage::storage | Buffer_usage::transfer_dst | Buffer_usage::transfer_src,
            .required_memory_property_bit_mask = Memory_property_flag_bit_mask::device_local,
            .debug_label                       = erhe::utility::Debug_label{"PVS layered buffer"}
        }
    );
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
        "PVS bake node: {} x {} faces, {} layers, layered buffer {} MB",
        face_resolution, face_resolution, layer_count, byte_count / (1024 * 1024)
    );
}

Pvs_bake_node::~Pvs_bake_node() noexcept = default;

void Pvs_bake_node::submit(Pvs_face_job&& job)
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    ERHE_VERIFY(!m_pending_job.has_value());
    ERHE_VERIFY(job.scene_root && (job.columns != nullptr));
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

void Pvs_bake_node::take_completed(std::vector<Pvs_face_record>& out)
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
    slot.cull_view = erhe::scene_renderer::Cull_view::none;

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
    // The face's frustum as the frame's culling view: entries wholly
    // outside it are not drawn.
    const Pvs_face_job&                       job         = slot.job.value();
    const erhe::graphics::Device_info&        info        = device.get_info();
    const erhe::math::Depth_range             depth_range = info.coordinate_conventions.native_depth_range;
    const glm::mat4 clip_from_world =
        job.camera.projection().clip_from_node_transform(m_viewport, device.get_reverse_depth(), depth_range, info.coordinate_conventions).get_matrix() *
        job.camera.node_from_world();
    const float clip_z_min = (depth_range == erhe::math::Depth_range::zero_to_one) ? 0.0f : -1.0f;
    const std::array<glm::vec4, 6> planes = erhe::math::extract_frustum_planes(clip_from_world, clip_z_min, 1.0f);
    erhe::scene_renderer::Draw_list_scene* draw_list_scene = job.scene_root->get_draw_list_scene();
    ERHE_VERIFY(draw_list_scene != nullptr);
    slot.cull_view = draw_list_scene->add_cull_view(frame_index, erhe::scene_renderer::Draw_purpose::color, std::span<const glm::vec4>{planes});
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
    const Pvs_face_job&                    job             = slot.job.value();
    erhe::scene_renderer::Draw_list_scene* draw_list_scene = job.scene_root->get_draw_list_scene();
    ERHE_VERIFY(draw_list_scene != nullptr);
    ERHE_VERIFY(m_context.forward_renderer != nullptr);

    const erhe::scene_renderer::Pvs_layered_interface& layered_interface = m_context.program_interface->pvs_layered_interface;
    const std::size_t words_offset = layered_interface.get_words_offset();
    const std::size_t region_bytes = m_words_per_region * sizeof(uint32_t);
    const std::size_t byte_count   = words_offset + (3 * region_bytes);

    // The faces of earlier frames (their passes' fragment accesses and a
    // readback's copy) -> this face's clear.
    command_buffer.buffer_barrier(
        *m_layered_buffer,
        Usage::storage_read | Usage::storage_write | Usage::fragment_shader | Usage::transfer_src,
        Usage::transfer_dst,
        Barrier_scope::earlier_submissions
    );
    {
        // The header (Pvs_layered_interface): info, depth_range.
        std::array<std::byte, 32> header{};
        const std::array<uint32_t, 4> info{
            m_face_resolution,
            m_layer_count,
            static_cast<uint32_t>(m_words_per_layer),
            static_cast<uint32_t>(m_words_per_region)
        };
        const std::array<float, 4> depth_range{job.z_near, job.z_far, 0.0f, 0.0f};
        std::memcpy(header.data() + layered_interface.get_info_offset(),        info.data(),        sizeof(info));
        std::memcpy(header.data() + layered_interface.get_depth_range_offset(), depth_range.data(), sizeof(depth_range));
        command_buffer.upload_to_buffer(*m_layered_buffer, 0, header.data(), header.size());
    }
    {
        // depth and id_min to 0xffffffff, id_max to 0.
        Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
        blit.fill_buffer(m_layered_buffer.get(), words_offset, 2 * region_bytes, 0xffu);
        blit.fill_buffer(m_layered_buffer.get(), words_offset + (2 * region_bytes), region_bytes, 0x00u);
    }
    command_buffer.buffer_barrier(*m_layered_buffer, Usage::transfer_dst, Usage::storage_read | Usage::storage_write | Usage::fragment_shader);

    erhe::scene_renderer::Scene_pass_resources& pass_resources = m_context.forward_renderer->get_pass_resources();
    const erhe::scene_renderer::Camera_view_input view{.camera = &job.camera, .viewport = m_viewport, .pixel_scale = 1.0f};
    const erhe::scene::Layer_id content_layers[] = { job.scene_root->layers().content()->id };
    const auto draw_pass = [&](const erhe::scene_renderer::Pvs_sub_variant sub_variant) -> erhe::scene_renderer::Draw_statistics {
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
                .cull_view            = slot.cull_view,
                .frame_index          = frame_index
            }
        );
        pass_resources.end_pass(pass_state, encoder);
        return statistics;
    };

    Pvs_face_record record{
        .frame          = frame_index,
        .cell           = job.cell,
        .face           = job.face,
        .sample         = job.sample,
        .z_near         = job.z_near,
        .z_far          = job.z_far,
        .readback_layer = job.readback_layer
    };
    record.depth_entries = draw_pass(erhe::scene_renderer::Pvs_sub_variant::depth).entry_count;
    // The depth pass's atomics -> the id pass's compare and atomics.
    command_buffer.buffer_barrier(*m_layered_buffer, Usage::storage_write | Usage::fragment_shader, Usage::storage_read | Usage::storage_write | Usage::fragment_shader);
    record.id_entries = draw_pass(erhe::scene_renderer::Pvs_sub_variant::id).entry_count;

    if (job.readback_layer.has_value()) {
        // pvs_debug_face: the layer's depth and id_min words -> the host. A
        // diagnostic path: it allocates.
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
    }

    const std::lock_guard<std::mutex> lock{m_record_mutex};
    m_records.push_back(std::move(record));
}

} // namespace editor
