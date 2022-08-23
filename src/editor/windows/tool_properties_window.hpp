#pragma once

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace editor
{

class Operations;

class Tool_properties_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Tool_properties_window"};
    static constexpr std::string_view c_title{"Tool Properties"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Tool_properties_window ();
    ~Tool_properties_window() noexcept override;

    // Implements erhe::Components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements erhe::applications::Imgui_window
    void imgui() override;

private:
    // Component dependencies
    std::shared_ptr<Operations> m_operations;
};

} // namespace erhe::application
