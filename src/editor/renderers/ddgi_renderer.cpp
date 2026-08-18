#include "renderers/ddgi_renderer.hpp"

#include "config/generated/ddgi_config.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

// Trace dispatch shape (phase 3): one workgroup row per probe, this many
// rays per workgroup. The configured ray count is rounded up to it.
constexpr int c_trace_workgroup_size = 32;

// The octahedral tiles carry a 1-texel border on every side so bilinear
// interpolation inside a tile never samples a neighbouring probe.
constexpr int c_border_texels = 1;

constexpr erhe::dataformat::Format c_irradiance_format = erhe::dataformat::Format::format_16_vec4_float;
constexpr erhe::dataformat::Format c_distance_format   = erhe::dataformat::Format::format_16_vec2_float;
constexpr erhe::dataformat::Format c_probe_data_format = erhe::dataformat::Format::format_16_vec4_float;
constexpr erhe::dataformat::Format c_ray_data_format   = erhe::dataformat::Format::format_16_vec4_float;

[[nodiscard]] auto round_up(const int value, const int multiple) -> int
{
    return ((value + multiple - 1) / multiple) * multiple;
}

} // anonymous namespace

auto Ddgi_renderer::Grid::operator==(const Grid& other) const -> bool
{
    return (counts == other.counts) && (origin == other.origin) && (spacing == other.spacing);
}

Ddgi_renderer::Ddgi_renderer(
    erhe::graphics::Device&            graphics_device,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    const Ddgi_config&                 config
)
    : m_graphics_device{graphics_device}
    , m_mesh_memory    {mesh_memory}
    , m_config         {config}
{
    // Ray query gates the whole feature: the probe update has no rasterized
    // fallback (doc/ddgi-plan.md - the probe-cubemap path is future work).
    m_supported = graphics_device.get_info().use_ray_query;
    if (!m_supported) {
        log_startup->info("Ddgi_renderer: ray query not available, DDGI disabled");
        return;
    }
    log_startup->info("Ddgi_renderer: DDGI available");
}

Ddgi_renderer::~Ddgi_renderer() noexcept = default;

auto Ddgi_renderer::is_supported() const -> bool
{
    return m_supported;
}

auto Ddgi_renderer::is_active() const -> bool
{
    return m_supported && m_config.enabled && m_grid.is_valid() && m_irradiance_texture;
}

auto Ddgi_renderer::get_grid() const -> const Grid&
{
    return m_grid;
}

auto Ddgi_renderer::get_irradiance_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_irradiance_texture;
}

auto Ddgi_renderer::get_distance_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_distance_texture;
}

auto Ddgi_renderer::get_probe_data_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_probe_data_texture;
}

auto Ddgi_renderer::get_ray_data_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&
{
    return m_ray_data_texture;
}

auto Ddgi_renderer::get_texture_byte_count() const -> std::size_t
{
    return m_texture_byte_count;
}

auto Ddgi_renderer::get_rays_per_probe() const -> int
{
    return m_rays_per_probe;
}

auto Ddgi_renderer::get_irradiance_texels() const -> int
{
    return m_irradiance_texels;
}

auto Ddgi_renderer::get_distance_texels() const -> int
{
    return m_distance_texels;
}

auto Ddgi_renderer::compute_volume_bounds(Scene_root& scene_root) const -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};

    const erhe::scene::Mesh_layer* content_layer = scene_root.layers().content();
    if (content_layer == nullptr) {
        return bounds;
    }

    // Union of the visible content meshes' world bounds. Skinned meshes are
    // included: they do not go into the acceleration structure, but they are
    // lit by the volume, so the volume has to cover them.
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
        if (!mesh || !mesh->is_visible()) {
            continue;
        }
        const erhe::math::Aabb mesh_bounds = mesh->get_aabb_world();
        if (!mesh_bounds.is_valid()) {
            continue;
        }
        bounds.include(mesh_bounds);
    }
    if (!bounds.is_valid()) {
        return bounds;
    }

    const float padding = std::max(0.0f, m_config.volume_padding_m);
    bounds.min -= glm::vec3{padding};
    bounds.max += glm::vec3{padding};
    return bounds;
}

auto Ddgi_renderer::fit_grid(const erhe::math::Aabb& bounds) const -> Grid
{
    Grid grid{};
    if (!bounds.is_valid()) {
        return grid;
    }
    const glm::vec3 min    = bounds.min;
    const glm::vec3 extent = glm::max(bounds.max - bounds.min, glm::vec3{1.0e-3f});

    // Probe counts from the target spacing; at least 2 per axis so the
    // trilinear interpolation always has a cell to interpolate inside. If
    // the result exceeds the probe budget, grow the spacing and retry - the
    // budget is a hard memory bound, the spacing is a target.
    const int max_probes = std::max(8, m_config.max_probes);
    float     spacing    = std::max(0.01f, m_config.probe_spacing_m);
    glm::ivec3 counts{0};
    for (;;) {
        counts = glm::ivec3{
            std::max(2, static_cast<int>(std::ceil(extent.x / spacing)) + 1),
            std::max(2, static_cast<int>(std::ceil(extent.y / spacing)) + 1),
            std::max(2, static_cast<int>(std::ceil(extent.z / spacing)) + 1)
        };
        const int64_t probe_count =
            static_cast<int64_t>(counts.x) *
            static_cast<int64_t>(counts.y) *
            static_cast<int64_t>(counts.z);
        if (probe_count <= static_cast<int64_t>(max_probes)) {
            break;
        }
        // Cube root of the overshoot is the spacing factor that would land
        // exactly on the budget; the 1.05 keeps the loop from stalling on
        // rounding. Both counts are >= 2, so this terminates.
        const double overshoot = static_cast<double>(probe_count) / static_cast<double>(max_probes);
        spacing *= static_cast<float>(std::cbrt(overshoot)) * 1.05f;
        if ((counts.x == 2) && (counts.y == 2) && (counts.z == 2)) {
            break; // Cannot get any coarser.
        }
    }

    grid.counts  = counts;
    grid.origin  = min;
    grid.spacing = extent / glm::vec3{counts - glm::ivec3{1}};
    return grid;
}

void Ddgi_renderer::allocate_textures(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    const int probe_count = m_grid.get_probe_count();
    // Probe (x,y,z) -> tile (x + counts.x * z, y): a row of tiles per
    // (y) slice, so the atlas stays close to square for typical grids.
    const int tiles_x = m_grid.counts.x * m_grid.counts.z;
    const int tiles_y = m_grid.counts.y;

    const int irradiance_tile = m_irradiance_texels + (2 * c_border_texels);
    const int distance_tile   = m_distance_texels   + (2 * c_border_texels);

    m_texture_byte_count = 0;
    const auto make_texture = [&](
        const char*                     debug_label,
        const erhe::dataformat::Format  format,
        const int                       width,
        const int                       height
    ) -> std::shared_ptr<Texture> {
        std::shared_ptr<Texture> texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = Image_usage_flag_bit_mask::storage      |
                               Image_usage_flag_bit_mask::sampled      |
                               Image_usage_flag_bit_mask::transfer_dst |
                               Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = format,
                .width       = width,
                .height      = height,
                .level_count = 1,
                .debug_label = erhe::utility::Debug_label{debug_label}
            }
        );
        m_texture_byte_count +=
            static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) *
            erhe::dataformat::get_format_size_bytes(format);
        // Probes start black with zero distance; the blend passes overwrite
        // them progressively, and until then the sampling path reads defined
        // (dark) data instead of undefined memory.
        command_buffer.clear_texture(*texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*texture, Image_layout::shader_read_only_optimal);
        return texture;
    };

    m_irradiance_texture = make_texture("DDGI irradiance", c_irradiance_format, tiles_x * irradiance_tile, tiles_y * irradiance_tile);
    m_distance_texture   = make_texture("DDGI distance",   c_distance_format,   tiles_x * distance_tile,   tiles_y * distance_tile  );
    m_probe_data_texture = make_texture("DDGI probe data", c_probe_data_format, tiles_x,                   tiles_y                  );
    m_ray_data_texture   = make_texture("DDGI ray data",   c_ray_data_format,   m_rays_per_probe,          probe_count              );

    // Refits happen at runtime (content moved, settings changed), so this
    // is a render-log event, not a startup one.
    log_render->info(
        "Ddgi_renderer: grid {}x{}x{} = {} probes, spacing {:.2f} {:.2f} {:.2f} m, {} rays/probe, {:.1f} MB",
        m_grid.counts.x, m_grid.counts.y, m_grid.counts.z, probe_count,
        m_grid.spacing.x, m_grid.spacing.y, m_grid.spacing.z,
        m_rays_per_probe,
        static_cast<double>(m_texture_byte_count) / (1024.0 * 1024.0)
    );
}

void Ddgi_renderer::tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root)
{
    if (!m_supported) {
        return;
    }
    if (!m_config.enabled) {
        // Release the probe memory while the feature is off; the textures
        // are cheap to recreate and the grid is refitted anyway.
        if (m_irradiance_texture) {
            m_irradiance_texture.reset();
            m_distance_texture  .reset();
            m_probe_data_texture.reset();
            m_ray_data_texture  .reset();
            m_texture_byte_count = 0;
            m_grid          = Grid{};
            m_volume_bounds = erhe::math::Aabb{};
        }
        return;
    }

    const int   rays_per_probe    = round_up(std::max(8, m_config.rays_per_probe), c_trace_workgroup_size);
    const int   irradiance_texels = std::clamp(m_config.irradiance_texels, 2, 32);
    const int   distance_texels   = std::clamp(m_config.distance_texels,   2, 64);
    const float fit_spacing_m     = std::max(0.01f, m_config.probe_spacing_m);
    const float fit_padding_m     = std::max(0.0f,  m_config.volume_padding_m);
    const int   fit_max_probes    = std::max(8,     m_config.max_probes);

    const erhe::math::Aabb bounds = compute_volume_bounds(scene_root);
    if (!bounds.is_valid()) {
        return;
    }

    // Refit only when the content left the current volume, or shrank so far
    // inside it that the probe density is being wasted. Everything else -
    // meshes moving within the volume - keeps the existing grid and its
    // converged probe contents.
    const bool settings_changed =
        (rays_per_probe    != m_rays_per_probe   ) ||
        (irradiance_texels != m_irradiance_texels) ||
        (distance_texels   != m_distance_texels  ) ||
        (fit_spacing_m     != m_fit_spacing_m    ) ||
        (fit_padding_m     != m_fit_padding_m    ) ||
        (fit_max_probes    != m_fit_max_probes   ) ||
        !m_irradiance_texture ||
        !m_grid.is_valid();
    const bool have_volume = m_volume_bounds.is_valid();
    const bool outside     = have_volume && (
        glm::any(glm::lessThan   (bounds.min, m_volume_bounds.min)) ||
        glm::any(glm::greaterThan(bounds.max, m_volume_bounds.max))
    );
    const bool much_smaller = have_volume && (bounds.volume() < (0.5f * m_volume_bounds.volume()));
    const bool bounds_changed = !have_volume || outside || much_smaller;
    if (!settings_changed && !bounds_changed) {
        return;
    }

    // A settings change refits to the current content exactly - the fit
    // parameters (spacing, padding, budget) are what changed, so the old
    // volume carries no information worth keeping. A pure bounds change
    // grows the box instead: a mesh that just left it takes it along, and
    // keeping the old extent stops a mesh oscillating across the boundary
    // from retriggering a refit every other frame. A large shrink refits to
    // the content, so the volume can also get smaller again.
    erhe::math::Aabb fit_bounds = bounds;
    if (!settings_changed && have_volume && outside && !much_smaller) {
        fit_bounds.include(m_volume_bounds);
    }
    const Grid grid = fit_grid(fit_bounds);
    if (!grid.is_valid()) {
        return;
    }

    m_volume_bounds     = fit_bounds;
    m_fit_spacing_m     = fit_spacing_m;
    m_fit_padding_m     = fit_padding_m;
    m_fit_max_probes    = fit_max_probes;
    m_grid              = grid;
    m_rays_per_probe    = rays_per_probe;
    m_irradiance_texels = irradiance_texels;
    m_distance_texels   = distance_texels;
    allocate_textures(command_buffer);
}

} // namespace editor
