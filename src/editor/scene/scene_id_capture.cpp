#include "scene/scene_id_capture.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "renderers/id_renderer.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_utility/thread_roles.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cstring>

namespace editor {

namespace {

constexpr erhe::dataformat::Format c_id_color_format = erhe::dataformat::Format::format_8_vec4_unorm;
constexpr erhe::dataformat::Format c_id_depth_format = erhe::dataformat::Format::format_d32_sfloat;

}

// ---------------------------------------------------------------------------
// Scene_id_capture_node

Scene_id_capture_node::Scene_id_capture_node(
    erhe::rendergraph::Rendergraph&     rendergraph,
    App_context&                        context,
    const std::shared_ptr<Scene_root>&  scene_root,
    const erhe::scene::Camera_snapshot& camera,
    const int                           width,
    const int                           height,
    const Scene_id_capture_content      content
)
    : erhe::rendergraph::Rendergraph_node{rendergraph, erhe::utility::Debug_label{"Scene id capture"}}
    , m_context   {context}
    , m_scene_root{scene_root}
    , m_camera    {camera}
    , m_viewport  {0, 0, width, height}
    , m_content   {content}
{
    using namespace erhe::graphics;
    Device& device = *m_context.graphics_device;
    m_color_texture = std::make_unique<Texture>(
        device,
        Texture_create_info{
            .device      = device,
            .usage_mask  = Image_usage_flag_bit_mask::color_attachment | Image_usage_flag_bit_mask::transfer_src,
            .type        = Texture_type::texture_2d,
            .pixelformat = c_id_color_format,
            .use_mipmaps = false,
            .width       = width,
            .height      = height,
            .debug_label = "Scene id capture color"
        }
    );
    m_depth_texture = std::make_unique<Texture>(
        device,
        Texture_create_info{
            .device      = device,
            .usage_mask  = Image_usage_flag_bit_mask::depth_stencil_attachment | Image_usage_flag_bit_mask::transfer_src,
            .type        = Texture_type::texture_2d,
            .pixelformat = c_id_depth_format,
            .use_mipmaps = false,
            .width       = width,
            .height      = height,
            .debug_label = "Scene id capture depth"
        }
    );
    // The attachment layouts of Id_renderer's pick pass; the color clears to
    // id 0 (no surface), the depth to the far value.
    Render_pass_descriptor render_pass_descriptor;
    render_pass_descriptor.color_attachments[0].texture       = m_color_texture.get();
    render_pass_descriptor.color_attachments[0].clear_value   = {0.0, 0.0, 0.0, 0.0};
    render_pass_descriptor.color_attachments[0].load_action   = Load_action::Clear;
    render_pass_descriptor.color_attachments[0].store_action  = Store_action::Store;
    render_pass_descriptor.color_attachments[0].usage_before  = Image_usage_flag_bit_mask::transfer_src;
    render_pass_descriptor.color_attachments[0].layout_before = Image_layout::transfer_src_optimal;
    render_pass_descriptor.color_attachments[0].usage_after   = Image_usage_flag_bit_mask::transfer_src;
    render_pass_descriptor.color_attachments[0].layout_after  = Image_layout::transfer_src_optimal;
    render_pass_descriptor.depth_attachment.texture           = m_depth_texture.get();
    render_pass_descriptor.depth_attachment.clear_value[0]    = device.get_reverse_depth() ? 0.0 : 1.0;
    render_pass_descriptor.depth_attachment.load_action       = Load_action::Clear;
    render_pass_descriptor.depth_attachment.store_action      = Store_action::Dont_care;
    render_pass_descriptor.depth_attachment.usage_before      = Image_usage_flag_bit_mask::transfer_src;
    render_pass_descriptor.depth_attachment.layout_before     = Image_layout::transfer_src_optimal;
    render_pass_descriptor.depth_attachment.usage_after       = Image_usage_flag_bit_mask::transfer_src;
    render_pass_descriptor.depth_attachment.layout_after      = Image_layout::transfer_src_optimal;
    render_pass_descriptor.render_target_width                = width;
    render_pass_descriptor.render_target_height               = height;
    render_pass_descriptor.debug_label                        = "Scene id capture";
    m_render_pass = std::make_unique<Render_pass>(device, render_pass_descriptor);
}

Scene_id_capture_node::~Scene_id_capture_node() noexcept = default;

void Scene_id_capture_node::fail(const std::string& error)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_record.error = error;
    m_record.state = Scene_id_capture_state::failed;
}

void Scene_id_capture_node::rearm(const erhe::scene::Camera_snapshot& camera)
{
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);
    {
        // Under m_mutex: with frame pipelining the render phase of a frame
        // that still names the node may execute it concurrently; it reads the
        // camera and the record under the same lock.
        const std::lock_guard<std::mutex> lock{m_mutex};
        ERHE_VERIFY(
            (m_record.state == Scene_id_capture_state::complete) ||
            (m_record.state == Scene_id_capture_state::in_flight) ||
            (m_record.state == Scene_id_capture_state::failed)
        );
        m_camera = camera;
        m_record.state = Scene_id_capture_state::rendering;
        m_record.error.clear();
        m_record.frame = 0;
        m_record.buffer.reset();
        m_record.id_ranges.clear();
    }
    set_enabled(true);
}

void Scene_id_capture_node::prepare_rendergraph_node(const uint64_t frame_index)
{
    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_main_thread_in_phase(erhe::utility::Frame_phase::m2_prepare);

    const bool recorded = with_record(
        [](const Scene_id_capture_record& record) -> bool {
            return record.state != Scene_id_capture_state::rendering;
        }
    );
    if (recorded) {
        // One frame is all a capture needs; later frames skip the node.
        set_enabled(false);
        return;
    }

    const std::size_t frames_in_flight = m_context.graphics_device->get_number_of_frames_in_flight();
    ERHE_VERIFY(frames_in_flight > 0);
    if (m_frame_slots.size() != frames_in_flight) {
        m_frame_slots.resize(frames_in_flight);
    }
    Frame_slot& slot = m_frame_slots[frame_index % frames_in_flight];
    slot.frame_index   = frame_index;
    slot.scene_root.reset();
    slot.pass.prepared = false;

    if (m_context.id_renderer == nullptr) {
        fail("the id renderer is not available");
        set_enabled(false);
        return;
    }
    std::shared_ptr<Scene_root> scene_root = m_scene_root.lock();
    if (!scene_root) {
        fail("the scene was closed");
        set_enabled(false);
        return;
    }

    const Scene_layers& layers = scene_root->layers();
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> all_spans[] = {
        layers.content()->meshes,
        layers.rendertarget()->meshes,
        layers.bone()->meshes
    };
    const std::size_t span_count = (m_content == Scene_id_capture_content::viewport) ? std::size(all_spans) : 1;
    // Bucketing reads the meshes' Buffer_mesh variants, which a worker may
    // be replacing (commit_geometry_buffer_mesh, under this mutex).
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};
    m_context.id_renderer->prepare(
        Id_renderer::Prepare_parameters{
            .mesh_spans      = std::span<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>{all_spans, span_count},
            .material_set    = &scene_root->get_material_set(),
            .skinning_filter = Id_renderer::Skinning_filter::all,
            .scan            = std::nullopt
        },
        slot.pass
    );
    slot.scene_root = std::move(scene_root);
}

void Scene_id_capture_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    ERHE_PROFILE_FUNCTION();
    erhe::utility::Thread_roles::assert_render_thread();
    erhe::utility::Thread_roles::assert_frame_phase(erhe::utility::Frame_phase::r_render);

    Device&        device      = *m_context.graphics_device;
    const uint64_t frame_index = device.get_frame_index();

    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_record.state != Scene_id_capture_state::rendering) {
        return; // recorded (or failed) in an earlier frame
    }
    // The prepare phase of every frame that names the node, while nothing
    // is recorded, prepares the frame's slot (or fails the record).
    ERHE_VERIFY(!m_frame_slots.empty());
    const Frame_slot& slot = m_frame_slots[frame_index % m_frame_slots.size()];
    ERHE_VERIFY((slot.frame_index == frame_index) && slot.pass.prepared && slot.scene_root);

    // Joint UBO/SSBO for skinned meshes, as App_rendering::render_id().
    erhe::scene_renderer::Joint_buffer*               joint_buffer{nullptr};
    std::span<const erhe::scene_renderer::Joint_pose> joint_poses = slot.scene_root->get_render_joint_poses(frame_index);
    if ((m_context.forward_renderer != nullptr) && !joint_poses.empty()) {
        joint_buffer = &m_context.forward_renderer->get_joint_buffer();
    }

    m_context.id_renderer->render_image(
        Id_renderer::Image_render_parameters{
            .command_buffer = command_buffer,
            .render_pass    = *m_render_pass,
            .viewport       = m_viewport,
            .camera         = m_camera,
            .reverse_depth  = device.get_reverse_depth(),
            .depth_range    = device.get_info().coordinate_conventions.native_depth_range,
            .conventions    = device.get_info().coordinate_conventions,
            .joint_buffer   = joint_buffer,
            .joint_poses    = joint_poses
        },
        slot.pass
    );

    const std::size_t bytes_per_row = static_cast<std::size_t>(m_viewport.width) * erhe::dataformat::get_format_size_bytes(c_id_color_format);
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(m_viewport.height);
    m_record.buffer = std::make_unique<Buffer>(
        device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst,
            .required_memory_property_bit_mask      = Memory_property_flag_bit_mask::host_read | Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     = Memory_property_flag_bit_mask::host_coherent | Memory_property_flag_bit_mask::host_persistent,
            .debug_label                            = erhe::utility::Debug_label{"Scene id capture readback"}
        }
    );
    {
        Blit_command_encoder blit = device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            Texture_location{.texture = m_color_texture.get(), .slice = 0, .level = 0, .origin = glm::ivec3{0, 0, 0}},
            glm::ivec3{m_viewport.width, m_viewport.height, 1},
            Buffer_texel_location{.buffer = m_record.buffer.get(), .offset = 0, .bytes_per_row = static_cast<std::uintptr_t>(bytes_per_row), .bytes_per_image = static_cast<std::uintptr_t>(byte_count)}
        );
    }
    // Transfer writes -> host reads once the frame's fence has signalled.
    command_buffer.memory_barrier(Memory_barrier_mask::client_mapped_buffer_barrier_bit);

    // The table the read-back ids resolve against, copied with the image:
    // the slot is prepared again for a later frame.
    m_record.id_ranges.assign(slot.pass.id_ranges.begin(), slot.pass.id_ranges.end());
    m_record.frame = frame_index;
    m_record.state = Scene_id_capture_state::in_flight;
}

// ---------------------------------------------------------------------------
// Scene_id_capture

Scene_id_capture::Scene_id_capture(App_context& context, const Scene_id_capture_create_info& create_info)
    : m_context{context}
    , m_width  {create_info.width}
    , m_height {create_info.height}
{
    ERHE_VERIFY(m_context.rendergraph != nullptr);
    ERHE_VERIFY(m_context.graphics_device != nullptr);
    ERHE_VERIFY((m_width > 0) && (m_height > 0));
    m_node = erhe::rendergraph::make_rendergraph_node<Scene_id_capture_node>(
        *m_context.rendergraph,
        m_context,
        create_info.scene_root,
        create_info.camera,
        m_width,
        m_height,
        create_info.content
    );
    log_render->info("Scene id capture: created node for {} x {}", m_width, m_height);
}

Scene_id_capture::~Scene_id_capture() noexcept
{
    // The deleter unregisters the node; a frame still to be rendered keeps
    // it retired until then.
    m_node.reset();
    log_render->info("Scene id capture: destroyed node");
}

auto Scene_id_capture::poll() -> Scene_id_capture_state
{
    if ((m_state == Scene_id_capture_state::complete) || (m_state == Scene_id_capture_state::failed)) {
        return m_state;
    }
    m_node->with_record(
        [this](Scene_id_capture_record& record) {
            m_state = record.state;
            if (record.state == Scene_id_capture_state::failed) {
                m_error = record.error;
                return;
            }
            if ((record.state != Scene_id_capture_state::in_flight) || !m_context.graphics_device->is_frame_completed(record.frame)) {
                return;
            }
            m_frame_index = record.frame;
            decode(*record.buffer, record.id_ranges);
            record.buffer.reset();
        }
    );
    return m_state;
}

void Scene_id_capture::rearm(const erhe::scene::Camera_snapshot& camera)
{
    ERHE_VERIFY((m_state == Scene_id_capture_state::complete) || (m_state == Scene_id_capture_state::failed));
    m_node->rearm(camera);
    m_state             = Scene_id_capture_state::rendering;
    m_error.clear();
    m_frame_index       = 0;
    m_covered_pixels    = 0;
    m_unresolved_pixels = 0;
    m_entries.clear();
}

void Scene_id_capture::decode(
    erhe::graphics::Buffer&                                              readback_buffer,
    const std::vector<erhe::scene_renderer::Primitive_buffer::Id_range>& id_ranges
)
{
    using Id_range = erhe::scene_renderer::Primitive_buffer::Id_range;

    const std::size_t bytes_per_pixel = erhe::dataformat::get_format_size_bytes(c_id_color_format);
    const std::size_t pixel_count     = static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    const std::size_t byte_count      = pixel_count * bytes_per_pixel;
    const std::span<std::byte> mapped = readback_buffer.map_bytes(0, byte_count);
    readback_buffer.invalidate(0, byte_count);
    if (mapped.size() < byte_count) {
        readback_buffer.unmap();
        m_error = "readback buffer could not be mapped";
        m_state = Scene_id_capture_state::failed;
        return;
    }

    // The ranges are appended in increasing offset order (Id_renderer::
    // prepare), so a pixel's range is found by binary search. Pixels per
    // range, then merged per (mesh, primitive).
    std::vector<std::size_t> range_pixels(id_ranges.size(), 0);
    m_covered_pixels    = 0;
    m_unresolved_pixels = 0;
    std::size_t last_range = id_ranges.size();
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::byte* pixel = mapped.data() + (i * bytes_per_pixel);
        const uint32_t   id    =
            (static_cast<uint32_t>(pixel[0]) << 24u) |
            (static_cast<uint32_t>(pixel[1]) << 16u) |
            (static_cast<uint32_t>(pixel[2]) <<  8u) |
             static_cast<uint32_t>(pixel[3]);
        if (id == 0) {
            continue;
        }
        ++m_covered_pixels;
        if (
            (last_range < id_ranges.size()) &&
            (id >= id_ranges[last_range].offset) &&
            (id < (id_ranges[last_range].offset + id_ranges[last_range].length))
        ) {
            ++range_pixels[last_range];
            continue;
        }
        const std::vector<Id_range>::const_iterator upper = std::upper_bound(
            id_ranges.begin(),
            id_ranges.end(),
            id,
            [](const uint32_t value, const Id_range& range) -> bool { return value < range.offset; }
        );
        if (upper == id_ranges.begin()) {
            ++m_unresolved_pixels;
            continue;
        }
        const std::size_t range_index = static_cast<std::size_t>(std::distance(id_ranges.begin(), upper)) - 1;
        const Id_range&   range       = id_ranges[range_index];
        if (id >= (range.offset + range.length)) {
            ++m_unresolved_pixels;
            continue;
        }
        ++range_pixels[range_index];
        last_range = range_index;
    }
    readback_buffer.unmap();

    m_entries.clear();
    for (std::size_t range_index = 0, end = id_ranges.size(); range_index < end; ++range_index) {
        if (range_pixels[range_index] == 0) {
            continue;
        }
        const Id_range& range = id_ranges[range_index];
        std::shared_ptr<erhe::scene::Mesh> mesh = range.lock_mesh();
        if (!mesh) {
            // Destroyed since the frame the ids were rendered in.
            m_unresolved_pixels += range_pixels[range_index];
            continue;
        }
        m_entries.push_back(
            Scene_id_capture_entry{
                .mesh            = std::move(mesh),
                .primitive_index = range.index_of_gltf_primitive_in_mesh,
                .pixel_count     = range_pixels[range_index]
            }
        );
    }
    std::sort(
        m_entries.begin(),
        m_entries.end(),
        [](const Scene_id_capture_entry& lhs, const Scene_id_capture_entry& rhs) -> bool {
            if (lhs.mesh->get_id() != rhs.mesh->get_id()) {
                return lhs.mesh->get_id() < rhs.mesh->get_id();
            }
            return lhs.primitive_index < rhs.primitive_index;
        }
    );
    // One record per (mesh, primitive) is the rule of Id_renderer::prepare;
    // merge anyway so an entry is listed once.
    std::size_t write = 0;
    for (std::size_t read = 0; read < m_entries.size(); ++read) {
        if (
            (write > 0) &&
            (m_entries[write - 1].mesh == m_entries[read].mesh) &&
            (m_entries[write - 1].primitive_index == m_entries[read].primitive_index)
        ) {
            m_entries[write - 1].pixel_count += m_entries[read].pixel_count;
            continue;
        }
        if (write != read) {
            m_entries[write] = std::move(m_entries[read]);
        }
        ++write;
    }
    m_entries.resize(write);
    m_state = Scene_id_capture_state::complete;
}

} // namespace editor
