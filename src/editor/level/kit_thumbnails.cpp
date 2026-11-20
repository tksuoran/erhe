#include "level/kit_thumbnails.hpp"

#include "level/kit_shape_library.hpp"
#include "level/level_kits.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "background_apply_meter.hpp"
#include "brushes/brush.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "graphics/thumbnails.hpp"
#include "preview/brush_preview.hpp"
#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/scoped_worker_context.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_task/task.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <imgui/imgui.h>
#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>

namespace editor {

auto c_str(const Kit_thumbnail_state state) -> const char*
{
    switch (state) {
        case Kit_thumbnail_state::none:     return "none";
        case Kit_thumbnail_state::queued:   return "queued";
        case Kit_thumbnail_state::building: return "building";
        case Kit_thumbnail_state::current:  return "current";
        case Kit_thumbnail_state::failed:   return "failed";
    }
    return "?";
}

// One shape's build input, captured by value on the main thread when it is queued (D6.2).
class Kit_thumbnail_build_request
{
public:
    std::size_t                   shape_index{0};
    Kit_parameter_values          values;
    std::string                   mesh_key;
    erhe::primitive::Build_info   build_info;
    erhe::primitive::Normal_style normal_style{erhe::primitive::Normal_style::corner_normals};
};

// Where the builds run: tasks on the background executor, or update() on a device without worker contexts (D6.6).
enum class Kit_thumbnail_build_mode : unsigned int {
    background_tasks = 0,
    main_thread
};

// The pending and finished builds, co-owned by Kit_thumbnails and the build tasks in flight (D6.4); every member
// behind m_mutex.
class Kit_thumbnail_builds final : public std::enable_shared_from_this<Kit_thumbnail_builds>
{
public:
    Kit_thumbnail_builds(tf::Executor& executor, erhe::graphics::Device& device, Kit_thumbnail_build_mode mode);
    Kit_thumbnail_builds(const Kit_thumbnail_builds&) = delete;
    auto operator=(const Kit_thumbnail_builds&) -> Kit_thumbnail_builds& = delete;
    Kit_thumbnail_builds(Kit_thumbnail_builds&&) = delete;
    auto operator=(Kit_thumbnail_builds&&) -> Kit_thumbnail_builds& = delete;

    // Main thread: replaces the shape's pending request, puts it first and starts tasks up to num_workers() in flight.
    void request(std::unique_ptr<Kit_thumbnail_build_request> request);
    // Main thread: swaps the build starts and the finished builds into the (cleared) out vectors.
    void take_finished(std::vector<Kit_thumbnail_build_start>& out_starts, std::vector<Kit_thumbnail_build_result>& out_results);
    // Main thread, main_thread mode: the most recent pending request, or null.
    [[nodiscard]] auto take_pending() -> std::unique_ptr<Kit_thumbnail_build_request>;
    void stop();

    [[nodiscard]] auto get_mode         () const -> Kit_thumbnail_build_mode;
    [[nodiscard]] auto get_pending_count() const -> std::size_t;
    [[nodiscard]] auto get_counts       () const -> Background_work_counts;

    // Any thread: the request's part primitives with their renderable meshes; no brush, collision shape or BVH.
    [[nodiscard]] static auto build(const Kit_thumbnail_build_request& request, erhe::graphics::Device* worker_context_device) -> Kit_thumbnail_build_result;

private:
    void spawn_tasks(std::size_t task_count);
    void work();

    tf::Executor&                                            m_executor;
    erhe::graphics::Device&                                  m_device;
    const Kit_thumbnail_build_mode                           m_mode;
    mutable std::mutex                                       m_mutex;
    std::deque<std::unique_ptr<Kit_thumbnail_build_request>> m_pending;     // front = most recently requested
    std::vector<Kit_thumbnail_build_start>                   m_starts;
    std::vector<Kit_thumbnail_build_result>                  m_results;
    std::size_t                                              m_in_flight    {0};
    std::size_t                                              m_max_in_flight{1};
    bool                                                     m_stopped      {false};
};

Kit_thumbnail_builds::Kit_thumbnail_builds(tf::Executor& executor, erhe::graphics::Device& device, const Kit_thumbnail_build_mode mode)
    : m_executor     {executor}
    , m_device       {device}
    , m_mode         {mode}
    , m_max_in_flight{std::max(static_cast<std::size_t>(executor.num_workers()), std::size_t{1})}
{
}

auto Kit_thumbnail_builds::get_mode() const -> Kit_thumbnail_build_mode
{
    return m_mode;
}

void Kit_thumbnail_builds::request(std::unique_ptr<Kit_thumbnail_build_request> request)
{
    ERHE_VERIFY(request);
    std::size_t tasks_to_start = 0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (m_stopped) {
            return;
        }
        for (std::deque<std::unique_ptr<Kit_thumbnail_build_request>>::iterator i = m_pending.begin(); i != m_pending.end(); ) {
            if ((*i)->shape_index == request->shape_index) {
                i = m_pending.erase(i);
            } else {
                ++i;
            }
        }
        m_pending.push_front(std::move(request));
        if ((m_mode == Kit_thumbnail_build_mode::background_tasks) && (m_in_flight < m_max_in_flight)) {
            tasks_to_start = std::min(m_max_in_flight - m_in_flight, m_pending.size());
            m_in_flight += tasks_to_start;
        }
    }
    spawn_tasks(tasks_to_start);
}

void Kit_thumbnail_builds::spawn_tasks(const std::size_t task_count)
{
    if (task_count == 0) {
        return;
    }
    const std::shared_ptr<Kit_thumbnail_builds> self = shared_from_this();
    for (std::size_t i = 0; i < task_count; ++i) {
        erhe::task::spawn(
            m_executor,
            [self]() -> void
            {
                self->work();
            }
        );
    }
}

void Kit_thumbnail_builds::work()
{
    for (;;) {
        std::unique_ptr<Kit_thumbnail_build_request> request;
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            if (m_stopped || m_pending.empty()) {
                ERHE_VERIFY(m_in_flight > 0);
                --m_in_flight;
                return;
            }
            request = std::move(m_pending.front());
            m_pending.pop_front();
            m_starts.push_back(Kit_thumbnail_build_start{.shape_index = request->shape_index, .mesh_key = request->mesh_key});
        }
        Kit_thumbnail_build_result result = build(*request.get(), &m_device);
        {
            const std::lock_guard<std::mutex> lock{m_mutex};
            if (!m_stopped) {
                m_results.push_back(std::move(result));
                continue;
            }
        }
        // Stopped at shutdown: the primitives are released here, before the executor drain returns.
    }
}

auto Kit_thumbnail_builds::build(const Kit_thumbnail_build_request& request, erhe::graphics::Device* const worker_context_device) -> Kit_thumbnail_build_result
{
    ERHE_PROFILE_FUNCTION();
    const Kit_shape_definition& shape = *request.values.get_shape();
    Kit_thumbnail_build_result result{.shape_index = request.shape_index, .mesh_key = request.mesh_key, .part_primitives = {}, .problem = {}};
    result.problem = request.values.validate();
    if (!result.problem.empty()) {
        return result;
    }
    // Several slots make several parts, one primitive each (D2.1); one slot is the whole geometry, as make_brush().
    std::vector<std::shared_ptr<erhe::geometry::Geometry>> part_geometries;
    if (shape.material_slots.size() > 1) {
        result.problem = make_kit_part_geometries(request.values, part_geometries);
        if (!result.problem.empty()) {
            return result;
        }
    } else {
        part_geometries.push_back(make_kit_geometry(request.values));
    }
    std::vector<std::shared_ptr<erhe::primitive::Primitive>> primitives;
    primitives.reserve(part_geometries.size());
    for (const std::shared_ptr<erhe::geometry::Geometry>& geometry : part_geometries) {
        if (!geometry || (geometry->get_mesh().facets.nb() == 0)) {
            result.problem = "the geometry has no facets";
            return result;
        }
        std::shared_ptr<erhe::primitive::Primitive> primitive = std::make_shared<erhe::primitive::Primitive>(geometry);
        bool renderable_mesh_ok = false;
        if (worker_context_device != nullptr) {
            // D3.3: the pool context is held for the GPU allocation only.
            const erhe::graphics::Scoped_worker_context worker_context{*worker_context_device};
            renderable_mesh_ok = primitive->make_renderable_mesh(request.build_info, request.normal_style);
        } else {
            renderable_mesh_ok = primitive->make_renderable_mesh(request.build_info, request.normal_style);
        }
        if (!renderable_mesh_ok) {
            result.problem = "no renderable mesh";
            return result;
        }
        primitives.push_back(std::move(primitive));
    }
    result.part_primitives = std::move(primitives);
    return result;
}

void Kit_thumbnail_builds::take_finished(std::vector<Kit_thumbnail_build_start>& out_starts, std::vector<Kit_thumbnail_build_result>& out_results)
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (!m_starts.empty()) {
        std::swap(m_starts, out_starts);
    }
    if (!m_results.empty()) {
        std::swap(m_results, out_results);
    }
}

auto Kit_thumbnail_builds::take_pending() -> std::unique_ptr<Kit_thumbnail_build_request>
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    if (m_stopped || m_pending.empty()) {
        return {};
    }
    std::unique_ptr<Kit_thumbnail_build_request> request = std::move(m_pending.front());
    m_pending.pop_front();
    return request;
}

void Kit_thumbnail_builds::stop()
{
    std::vector<Kit_thumbnail_build_result> results;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        m_stopped = true;
        m_pending.clear();
        m_starts.clear();
        std::swap(m_results, results);
    }
    // The finished builds' primitives are released outside the lock, on the main thread, while mesh memory is alive.
}

auto Kit_thumbnail_builds::get_pending_count() const -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return m_pending.size();
}

auto Kit_thumbnail_builds::get_counts() const -> Background_work_counts
{
    const std::lock_guard<std::mutex> lock{m_mutex};
    return Background_work_counts{.queued = m_pending.size(), .running = m_in_flight, .ready = m_results.size()};
}

Kit_thumbnails::Kit_thumbnails(
    Level_kits&             level_kits,
    App_context&            context,
    App_message_bus&        app_message_bus,
    erhe::graphics::Device& graphics_device,
    tf::Executor&           background_executor
)
    : m_level_kits     {level_kits}
    , m_context        {context}
    , m_builds         {
        std::make_shared<Kit_thumbnail_builds>(
            background_executor,
            graphics_device,
            graphics_device.supports_worker_contexts()
                ? Kit_thumbnail_build_mode::background_tasks
                : Kit_thumbnail_build_mode::main_thread
        )
    }
    , m_background_work{*context.executor_status, "kit_thumbnails", [this]() { return m_builds->get_counts(); }}
{
    const std::size_t shape_count = level_kits.get_library().get_shapes().size();
    m_thumbnails.resize(shape_count);
    m_requested_mesh_keys.resize(shape_count);
    // As Thumbnails: the preset selects the shader variants the thumbnails draw with.
    m_graphics_settings_subscription = app_message_bus.graphics_settings.subscribe(
        [this](Graphics_settings_message& message) {
            on_graphics_settings_changed(message.graphics_preset);
        }
    );
}

void Kit_thumbnails::on_graphics_settings_changed(const Graphics_preset_entry* const graphics_preset)
{
    if (graphics_preset != nullptr) {
        std::string key = make_thumbnail_preset_key(*graphics_preset);
        if (key == m_rendered_preset_key) {
            return;
        }
        m_rendered_preset_key = std::move(key);
    }
    for (std::size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        const Kit_thumbnail_state state = m_thumbnails[i].state;
        if ((state == Kit_thumbnail_state::current) || (state == Kit_thumbnail_state::failed)) {
            queue(i);
        }
    }
}

auto Kit_thumbnails::get_size_pixels() const -> int
{
    if (m_texture) {
        return m_texture->get_width();
    }
    return (m_context.editor_settings != nullptr) ? m_context.editor_settings->thumbnails.size_pixels : 0;
}

Kit_thumbnails::~Kit_thumbnails() noexcept
{
    m_builds->stop();
}

void Kit_thumbnails::stop()
{
    m_builds->stop();
}

auto Kit_thumbnails::get_lengths() const -> const std::optional<Kit_lengths>&
{
    return m_lengths;
}

auto Kit_thumbnails::get_thumbnails() const -> const std::vector<Kit_thumbnail>&
{
    return m_thumbnails;
}

auto Kit_thumbnails::get_queued_count() const -> std::size_t
{
    return m_builds->get_pending_count();
}

auto Kit_thumbnails::get_render_count() const -> std::uint64_t
{
    return m_render_count;
}

void Kit_thumbnails::on_level_lengths_changed()
{
    m_lengths_changed = true;
}

auto Kit_thumbnails::get_shape_index(const Kit_shape_definition& shape) const -> std::size_t
{
    const std::vector<Kit_shape_definition>& shapes = m_level_kits.get_library().get_shapes();
    ERHE_VERIFY((&shape >= shapes.data()) && (&shape < (shapes.data() + shapes.size())));
    return static_cast<std::size_t>(&shape - shapes.data());
}

auto Kit_thumbnails::get_mesh_key(const Kit_shape_definition& shape) const -> std::string
{
    ERHE_VERIFY(m_lengths.has_value());
    return Level_kits::get_mesh_key(make_default_values(shape, m_lengths.value().module, m_lengths.value().wall_height));
}

auto Kit_thumbnails::get_material(const std::string& palette_name) -> std::shared_ptr<erhe::primitive::Material>
{
    const std::map<std::string, std::shared_ptr<erhe::primitive::Material>>::iterator i = m_materials.find(palette_name);
    if (i != m_materials.end()) {
        return i->second;
    }
    const Kit_palette_material* const palette = m_level_kits.get_library().find_palette_material(palette_name);
    if (palette == nullptr) {
        return {};
    }
    std::shared_ptr<erhe::primitive::Material> material = make_palette_material(*palette);
    m_context.brush_preview->add_material(material);
    m_materials.emplace(palette_name, material);
    return material;
}

void Kit_thumbnails::follow_scene(const std::shared_ptr<Scene_root>& scene_root)
{
    ERHE_VERIFY(scene_root);
    // A per-draw identity check: the last hovered viewport's scene changes without a message; lengths edits notify.
    const bool same_scene = !m_scene.owner_before(scene_root) && !scene_root.owner_before(m_scene) && !m_scene.expired();
    if (same_scene && !m_lengths_changed && m_lengths.has_value()) {
        return;
    }
    m_scene           = scene_root;
    m_lengths_changed = false;
    apply_lengths(m_level_kits.get_lengths(*scene_root));
}

void Kit_thumbnails::apply_lengths(const Kit_lengths& lengths)
{
    if (
        m_lengths.has_value() &&
        (m_lengths.value().module == lengths.module) &&
        (m_lengths.value().wall_height == lengths.wall_height)
    ) {
        return;
    }
    m_lengths = lengths;
    // Only the thumbnails whose defaults these lengths change render again; they show their image until then. A queued
    // or building shape whose request has other lengths is requested again; its older build is released unrendered.
    const std::vector<Kit_shape_definition>& shapes = m_level_kits.get_library().get_shapes();
    for (std::size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        const Kit_thumbnail_state state = m_thumbnails[i].state;
        if (state == Kit_thumbnail_state::none) {
            continue;
        }
        const bool in_progress = (state == Kit_thumbnail_state::queued) || (state == Kit_thumbnail_state::building);
        const std::string& wanted_key = in_progress ? m_requested_mesh_keys[i] : m_thumbnails[i].mesh_key;
        if (get_mesh_key(shapes[i]) != wanted_key) {
            queue(i);
        }
    }
    log_scene->trace("Kit thumbnails: lengths {} / {}, {} queued", lengths.module, lengths.wall_height, get_queued_count());
}

void Kit_thumbnails::queue(const std::size_t shape_index)
{
    ERHE_VERIFY(m_lengths.has_value());
    ERHE_VERIFY(m_context.mesh_memory != nullptr);
    const Kit_shape_definition& shape   = m_level_kits.get_library().get_shapes()[shape_index];
    const Kit_lengths&          lengths = m_lengths.value();
    std::unique_ptr<Kit_thumbnail_build_request> request = std::make_unique<Kit_thumbnail_build_request>(
        Kit_thumbnail_build_request{
            .shape_index  = shape_index,
            .values       = make_default_values(shape, lengths.module, lengths.wall_height),
            .mesh_key     = {},
            .build_info   = erhe::primitive::Build_info{
                .primitive_types = {.fill_triangles = true},
                .buffer_info     = m_context.mesh_memory->make_primitive_buffer_info()
            },
            .normal_style = get_kit_normal_style(shape)
        }
    );
    request->mesh_key = Level_kits::get_mesh_key(request->values);
    m_requested_mesh_keys[shape_index] = request->mesh_key;
    m_thumbnails[shape_index].state    = Kit_thumbnail_state::queued;
    m_builds->request(std::move(request));
}

auto Kit_thumbnails::request_all() -> std::size_t
{
    ERHE_VERIFY(m_lengths.has_value());
    std::size_t count = 0;
    for (std::size_t i = 0, end = m_thumbnails.size(); i < end; ++i) {
        if (m_thumbnails[i].state == Kit_thumbnail_state::none) {
            queue(i);
            ++count;
        }
    }
    return count;
}

auto Kit_thumbnails::draw(const Kit_shape_definition& shape, const float size) -> bool
{
    const std::size_t shape_index = get_shape_index(shape);
    Kit_thumbnail&    thumbnail   = m_thumbnails[shape_index];
    if (thumbnail.state == Kit_thumbnail_state::none) {
        queue(shape_index);
    }
    // An image button: hover and press feedback, and a press on it never drags the window. Until its image exists an
    // invisible button of the same size and id stands in (a framed Button would shift the row's text baseline).
    const float side = std::floor(size);
    if (!thumbnail.has_image || !m_texture) {
        return ImGui::InvisibleButton(shape.id.c_str(), ImVec2{side, side});
    }
    const bool                   layer_views    = !m_layer_views.empty();
    erhe::imgui::Imgui_renderer& imgui_renderer = *m_context.imgui_renderer;
    // No frame padding, so both stand-ins are the same size; the hover and press colors show through the transparent
    // background of the image.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
    const bool clicked = imgui_renderer.image_button(
        erhe::imgui::Draw_texture_parameters{
            .texture_reference = layer_views ? m_layer_views[shape_index] : m_texture,
            .id                = ImGui::GetID(shape.id.c_str()),
            .width             = static_cast<int>(side),
            .height            = static_cast<int>(side),
            .uv0               = imgui_renderer.get_rtt_uv0(),
            .uv1               = imgui_renderer.get_rtt_uv1(),
            .filter            = erhe::graphics::Filter::linear,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::linear,
            .array_layer       = layer_views ? -1 : static_cast<int>(shape_index),
            .debug_label       = "Kit_thumbnails::draw()"
        }
    );
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return clicked;
}

void Kit_thumbnails::make_texture(erhe::graphics::Command_buffer& command_buffer)
{
    erhe::graphics::Device& device      = *m_context.graphics_device;
    const int               layer_count = static_cast<int>(m_thumbnails.size());
    const int               size_pixels = get_size_pixels();
    m_texture = std::make_shared<erhe::graphics::Texture>(
        device,
        erhe::graphics::Texture_create_info{
            .device            = device,
            .usage_mask        =
                erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
                erhe::graphics::Image_usage_flag_bit_mask::sampled |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_src |
                erhe::graphics::Image_usage_flag_bit_mask::transfer_dst,
            .type              = erhe::graphics::Texture_type::texture_2d_array,
            .pixelformat       = erhe::dataformat::Format::format_8_vec4_unorm,
            .use_mipmaps       = true,
            .width             = size_pixels,
            .height            = size_pixels,
            .array_layer_count = layer_count,
            .debug_label       = "Kit thumbnails"
        }
    );
    // A layer is sampled only after its render; the transition keeps the unrendered ones valid to bind.
    command_buffer.transition_texture_layout(*m_texture, erhe::graphics::Image_layout::shader_read_only_optimal);
    if (!device.get_info().use_texture_view) {
        return;
    }
    // As Thumbnails: a single-layer view per shape, sampled as sampler2D.
    m_layer_views.reserve(m_thumbnails.size());
    for (int layer = 0; layer < layer_count; ++layer) {
        erhe::graphics::Texture_create_info view_info = erhe::graphics::Texture_create_info::make_view(device, m_texture);
        view_info.type                  = erhe::graphics::Texture_type::texture_2d;
        view_info.usage_mask            =
            erhe::graphics::Image_usage_flag_bit_mask::color_attachment |
            erhe::graphics::Image_usage_flag_bit_mask::sampled;
        view_info.view_base_level       = 0;
        view_info.array_layer_count     = 0;
        view_info.use_mipmaps           = true;
        view_info.level_count           = m_texture->get_level_count();
        view_info.view_base_array_layer = layer;
        view_info.debug_label           = erhe::utility::Debug_label{fmt::format("Kit thumbnail layer {}", layer)};
        m_layer_views.push_back(std::make_shared<erhe::graphics::Texture>(device, view_info));
    }
}

void Kit_thumbnails::update(erhe::graphics::Command_buffer& command_buffer)
{
    if (m_context.brush_preview == nullptr) {
        return;
    }
    m_builds->take_finished(m_taken_starts, m_taken_results);
    // D6.6: without worker contexts one shape a frame is built here, on the main thread.
    if (m_builds->get_mode() == Kit_thumbnail_build_mode::main_thread) {
        const std::unique_ptr<Kit_thumbnail_build_request> request = m_builds->take_pending();
        if (request) {
            m_taken_results.push_back(Kit_thumbnail_builds::build(*request.get(), nullptr));
        }
    }
    for (const Kit_thumbnail_build_start& start : m_taken_starts) {
        Kit_thumbnail& thumbnail = m_thumbnails[start.shape_index];
        if ((thumbnail.state == Kit_thumbnail_state::queued) && (m_requested_mesh_keys[start.shape_index] == start.mesh_key)) {
            thumbnail.state = Kit_thumbnail_state::building;
        }
    }
    m_taken_starts.clear();
    if (m_taken_results.empty()) {
        return;
    }
    ERHE_PROFILE_FUNCTION();
    const Background_apply_meter::Scope apply_scope{*m_context.background_apply_meter, "kit_thumbnails"};
    for (const Kit_thumbnail_build_result& result : m_taken_results) {
        apply_result(command_buffer, result);
    }
    // Every primitive is released here: the rendered ones stay referenced by the preview mesh until its next render.
    m_taken_results.clear();
}

void Kit_thumbnails::apply_result(erhe::graphics::Command_buffer& command_buffer, const Kit_thumbnail_build_result& result)
{
    const Kit_shape_definition& shape     = m_level_kits.get_library().get_shapes()[result.shape_index];
    Kit_thumbnail&              thumbnail = m_thumbnails[result.shape_index];
    const bool in_progress = (thumbnail.state == Kit_thumbnail_state::queued) || (thumbnail.state == Kit_thumbnail_state::building);
    if (!in_progress || (m_requested_mesh_keys[result.shape_index] != result.mesh_key)) {
        // Superseded lengths, or a duplicate build of an image already rendered: released unrendered (D6.3).
        return;
    }
    thumbnail.mesh_key = result.mesh_key;
    if (result.part_primitives.empty()) {
        thumbnail.state     = Kit_thumbnail_state::failed;
        thumbnail.has_image = false;
        log_scene->warn("Kit thumbnail '{}': the defaults make no geometry{}", shape.id, result.problem.empty() ? std::string{} : (": " + result.problem));
        return;
    }
    if (!m_texture) {
        make_texture(command_buffer);
    }

    m_level_kits.get_default_slot_materials(shape, m_slot_names);
    m_part_materials.clear();
    for (std::size_t part = 0, end = result.part_primitives.size(); part < end; ++part) {
        const bool has_slot = (part < m_slot_names.size()) && !m_slot_names[part].empty();
        m_part_materials.push_back(has_slot ? get_material(m_slot_names[part]) : std::shared_ptr<erhe::primitive::Material>{});
    }
    // A three-quarter view: the piece turned so its +x and +z faces face the preview camera.
    const glm::quat orientation = glm::angleAxis(glm::radians(-35.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const bool      layer_views = !m_layer_views.empty();
    // Uploads first: render_parts_preview() flushes mesh memory into this command buffer before its draw.
    m_context.brush_preview->render_parts_preview(
        command_buffer,
        layer_views ? m_layer_views[result.shape_index] : m_texture,
        layer_views ? 0u : static_cast<unsigned int>(result.shape_index),
        result.part_primitives,
        m_part_materials,
        orientation,
        nullptr
    );
    thumbnail.state     = Kit_thumbnail_state::current;
    thumbnail.has_image = true;
    ++thumbnail.render_count;
    ++m_render_count;
    if (m_lengths.has_value()) {
        log_scene->trace("Kit thumbnail '{}' rendered at {} / {}", shape.id, m_lengths.value().module, m_lengths.value().wall_height);
    }
}

} // namespace editor
