#pragma once

#include <cstddef>
#include <cstdint>

namespace erhe::graphics { class Device; }
namespace erhe::gltf     { class Gltf_device_options; }

struct Load_config;

namespace editor {

class App_context;

// Effective limits, 0 = none (doc/plans/memory_budget.md step 3).
class Load_memory_budgets
{
public:
    std::size_t texture_byte_count{0};
    std::size_t mesh_byte_count   {0};
};

[[nodiscard]] auto get_load_memory_budgets(const Load_config& load_config, erhe::graphics::Device& graphics_device) -> Load_memory_budgets;

// Budget minus resident content textures; 0 = no budget, 1 = used up.
[[nodiscard]] auto get_remaining_texture_byte_budget(App_context& context, erhe::graphics::Device& graphics_device) -> std::size_t;

// erhe::gltf::query_gltf_device_options() plus the remaining texture budget.
[[nodiscard]] auto make_gltf_device_options(App_context& context, erhe::graphics::Device& graphics_device) -> erhe::gltf::Gltf_device_options;

}
