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

    Configuration(int argc, char** argv);

    bool gui                    {true};
    bool openxr                 {true};
    bool parallel_initialization{true};
    bool reverse_depth          {true};

    auto depth_clear_value_pointer() const -> const float *; // reverse_depth ? 0.0f : 1.0f;
    auto depth_function           (const gl::Depth_function depth_function) const -> const gl::Depth_function;
};

} // namespace editor