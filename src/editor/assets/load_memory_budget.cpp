#include "assets/load_memory_budget.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"

#include "erhe_gltf/gltf.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/texture.hpp"

#include <algorithm>

namespace editor {

namespace {

[[nodiscard]] auto get_effective_limit(const int mib, const float fraction, const std::uint64_t device_local_budget) -> std::size_t
{
    const std::size_t absolute   = (mib > 0) ? (static_cast<std::size_t>(mib) * 1024 * 1024) : 0;
    const std::size_t fractional = ((fraction > 0.0f) && (device_local_budget > 0))
        ? static_cast<std::size_t>(static_cast<double>(fraction) * static_cast<double>(device_local_budget))
        : 0;
    if (absolute == 0) {
        return fractional;
    }
    if (fractional == 0) {
        return absolute;
    }
    return std::min(absolute, fractional);
}

}

auto get_load_memory_budgets(const Load_config& load_config, erhe::graphics::Device& graphics_device) -> Load_memory_budgets
{
    const std::uint64_t device_local_budget = graphics_device.get_memory_budget().device_local_budget;
    return Load_memory_budgets{
        .texture_byte_count = get_effective_limit(load_config.texture_memory_budget_mib, load_config.texture_memory_budget_fraction, device_local_budget),
        .mesh_byte_count    = get_effective_limit(load_config.mesh_memory_budget_mib,    load_config.mesh_memory_budget_fraction,    device_local_budget)
    };
}

auto get_remaining_texture_byte_budget(App_context& context, erhe::graphics::Device& graphics_device) -> std::size_t
{
    if (context.editor_settings == nullptr) {
        return 0;
    }
    const std::size_t budget = get_load_memory_budgets(context.editor_settings->load, graphics_device).texture_byte_count;
    if (budget == 0) {
        return 0;
    }
    const std::size_t resident = erhe::graphics::Texture::get_memory_statistics().content_byte_count;
    return (budget > resident) ? (budget - resident) : 1;
}

auto make_gltf_device_options(App_context& context, erhe::graphics::Device& graphics_device) -> erhe::gltf::Gltf_device_options
{
    erhe::gltf::Gltf_device_options options = erhe::gltf::query_gltf_device_options(graphics_device);
    options.texture_byte_budget     = get_remaining_texture_byte_budget(context, graphics_device);
    options.texture_cache_directory = std::filesystem::path{"cache"} / "texture_encode";
    return options;
}

}
