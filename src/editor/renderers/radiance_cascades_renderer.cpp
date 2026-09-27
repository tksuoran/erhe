#include "renderers/radiance_cascades_renderer.hpp"

#include "config/generated/radiance_cascades_config.hpp"
#include "editor_log.hpp"
#include "renderers/content_bounds.hpp"

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <string>

namespace editor {

namespace {

// rgb radiance, a transparency beta (doc/plans/radiance_cascades.md section 3).
constexpr erhe::dataformat::Format c_radiance_format = erhe::dataformat::Format::format_16_vec4_float;

} // anonymous namespace

Radiance_cascades_renderer::Radiance_cascades_renderer(
    erhe::graphics::Device&         graphics_device,
    const Radiance_cascades_config& config,
    const Producer_selection        selection
)
    : m_graphics_device{graphics_device}
    , m_config         {config}
    , m_selection      {selection}
{
    // Ray query gates the whole feature, as for DDGI: the interval trace has
    // no rasterized fallback (doc/plans/radiance_cascades.md section 11).
    m_supported = graphics_device.get_info().use_ray_query;
    if (!m_supported) {
        log_startup->info("Radiance_cascades_renderer: ray query not available, radiance cascades disabled");
    }
}

Radiance_cascades_renderer::~Radiance_cascades_renderer() noexcept = default;

auto Radiance_cascades_renderer::is_supported() const -> bool
{
    return m_supported;
}

auto Radiance_cascades_renderer::is_selected() const -> bool
{
    return m_selection == Producer_selection::selected;
}

void Radiance_cascades_renderer::set_selection(const Producer_selection selection)
{
    if (selection == m_selection) {
        return;
    }
    m_selection = selection;
    if (selection == Producer_selection::deselected) {
        // Release the cascade memory while another source is selected; the
        // layout is refitted when the source is selected again.
        release_textures();
        m_layout = Radiance_cascades_layout{};
        m_volume_bounds.reset();
    }
}

auto Radiance_cascades_renderer::is_active() const -> bool
{
    return is_supported() && is_selected() && m_layout.is_valid() && static_cast<bool>(m_cascade_textures[0].raw);
}

auto Radiance_cascades_renderer::has_field() const -> bool
{
    return false;
}

auto Radiance_cascades_renderer::get_layout() const -> const Radiance_cascades_layout&
{
    return m_layout;
}

auto Radiance_cascades_renderer::get_cascade_textures(const int cascade) const -> const Cascade_textures&
{
    ERHE_VERIFY((cascade >= 0) && (cascade < c_max_radiance_cascades));
    return m_cascade_textures[static_cast<std::size_t>(cascade)];
}

auto Radiance_cascades_renderer::get_cascade_texture_byte_count(const int cascade) const -> std::size_t
{
    ERHE_VERIFY((cascade >= 0) && (cascade < c_max_radiance_cascades));
    return m_cascade_byte_counts[static_cast<std::size_t>(cascade)];
}

auto Radiance_cascades_renderer::get_texture_byte_count() const -> std::size_t
{
    return m_texture_byte_count;
}

auto Radiance_cascades_renderer::get_fit_count() const -> uint64_t
{
    return m_fit_count;
}

void Radiance_cascades_renderer::release_textures()
{
    for (Cascade_textures& textures : m_cascade_textures) {
        textures.raw.reset();
        textures.merged.reset();
    }
    m_cascade_byte_counts.fill(0);
    m_texture_byte_count = 0;
}

void Radiance_cascades_renderer::allocate_textures(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    release_textures();
    const std::size_t texel_bytes = erhe::dataformat::get_format_size_bytes(c_radiance_format);
    const auto make_texture = [&](const std::string& debug_label, const int width, const int height) -> std::shared_ptr<Texture> {
        std::shared_ptr<Texture> texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = Image_usage_flag_bit_mask::storage      |
                               Image_usage_flag_bit_mask::sampled      |
                               Image_usage_flag_bit_mask::transfer_dst |
                               Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = c_radiance_format,
                .width       = width,
                .height      = height,
                .level_count = 1,
                .debug_label = erhe::utility::Debug_label{debug_label}
            }
        );
        // Zero radiance, zero transparency: every reader sees defined data
        // until the trace and merge passes write the atlases.
        command_buffer.clear_texture(*texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*texture, Image_layout::shader_read_only_optimal);
        return texture;
    };

    for (int i = 0; i < m_layout.cascade_count; ++i) {
        const Radiance_cascade& cascade = m_layout.cascades[static_cast<std::size_t>(i)];
        const int width  = cascade.get_atlas_width();
        const int height = cascade.get_atlas_height();
        Cascade_textures& textures = m_cascade_textures[static_cast<std::size_t>(i)];
        textures.raw    = make_texture(fmt::format("RC cascade {} raw",    i), width, height);
        textures.merged = make_texture(fmt::format("RC cascade {} merged", i), width, height);
        const std::size_t bytes = 2 * static_cast<std::size_t>(cascade.get_atlas_texel_count()) * texel_bytes;
        m_cascade_byte_counts[static_cast<std::size_t>(i)] = bytes;
        m_texture_byte_count += bytes;
    }

    // Refits happen at runtime (content moved, settings changed), so this is
    // a render-log event, not a startup one.
    const Radiance_cascade& cascade0 = m_layout.cascades[0];
    log_render->info(
        "Radiance_cascades_renderer: {} cascades, cascade 0 {}x{}x{} probes, spacing {:.3f} {:.3f} {:.3f} m, q0 {}, r0 {:.3f} m, {} probes / {} texels total, {:.1f} MB",
        m_layout.cascade_count,
        cascade0.grid.counts.x, cascade0.grid.counts.y, cascade0.grid.counts.z,
        cascade0.grid.spacing.x, cascade0.grid.spacing.y, cascade0.grid.spacing.z,
        cascade0.tile_texels, m_layout.r0,
        m_layout.get_total_probes(), m_layout.get_total_texels(),
        static_cast<double>(m_texture_byte_count) / (1024.0 * 1024.0)
    );
}

auto Radiance_cascades_renderer::update_layout(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root) -> bool
{
    const Radiance_cascades_layout_settings settings{
        .probe_spacing_m      = std::max(0.01f, m_config.probe_spacing_m),
        .max_probes_cascade0  = std::max(8,     m_config.max_probes_cascade0),
        .max_cascades         = std::clamp(m_config.max_cascades, 1, c_max_radiance_cascades),
        .cascade0_tile_texels = std::clamp(m_config.cascade0_tile_texels, 1, 64),
        .interval_scale       = std::max(1.0f,  m_config.interval_scale),
        .max_texture_size     = m_graphics_device.get_info().max_texture_size
    };
    const float padding_m = std::max(0.0f, m_config.volume_padding_m);

    const erhe::math::Aabb bounds = compute_padded_content_bounds(scene_root, padding_m);
    if (!bounds.is_valid()) {
        return false;
    }

    const bool settings_changed =
        (settings  != m_fit_settings ) ||
        (padding_m != m_fit_padding_m) ||
        !m_layout.is_valid() ||
        !m_cascade_textures[0].raw;
    const bool bounds_changed = m_volume_bounds.content_changed(bounds);
    if (!settings_changed && !bounds_changed) {
        return true;
    }

    const Volume_refit_cause       cause      = settings_changed ? Volume_refit_cause::settings : Volume_refit_cause::content;
    const erhe::math::Aabb         fit_bounds = m_volume_bounds.get_fit_bounds(bounds, cause);
    const Radiance_cascades_layout layout     = fit_radiance_cascades(fit_bounds, settings);
    if (!layout.is_valid()) {
        return false;
    }

    m_volume_bounds.set(fit_bounds);
    m_fit_settings  = settings;
    m_fit_padding_m = padding_m;
    m_layout        = layout;
    ++m_fit_count;
    allocate_textures(command_buffer);
    return true;
}

void Radiance_cascades_renderer::tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root)
{
    if (!is_supported()) {
        return;
    }
    if (!is_selected()) {
        return;
    }
    static_cast<void>(update_layout(command_buffer, scene_root));
}

} // namespace editor
