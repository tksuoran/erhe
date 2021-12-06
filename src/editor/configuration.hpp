#pragma once

#include "erhe/components/component.hpp"
#include "erhe/components/components.hpp"
#include "erhe/gl/wrapper_enums.hpp"

namespace editor {

class Configuration
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Configuration"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Configuration(int argc, char** argv);

    // Implements Component
    auto get_type_hash() const -> uint32_t override { return hash; }

    bool gui                    {true};
    bool openxr                 {false};
    bool parallel_initialization{false};
    bool reverse_depth          {true};

    auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    auto depth_function           (const gl::Depth_function depth_function) const -> gl::Depth_function;
};

} // namespace editor
