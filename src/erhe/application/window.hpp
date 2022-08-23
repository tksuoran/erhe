#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/window.hpp"

namespace erhe::application {

class Configuration;
class Renderdoc_capture_support;

class Window
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Window"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Window();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;

    // Public API
    [[nodiscard]] auto get_context_window() const -> erhe::toolkit::Context_window*;
    auto create_gl_window() -> bool;
    void begin_renderdoc_capture();
    void end_renderdoc_capture  ();

private:
    std::unique_ptr<erhe::toolkit::Context_window> m_context_window;
    std::shared_ptr<Configuration>                 m_configuration;
    std::shared_ptr<Renderdoc_capture_support>     m_renderdoc_capture_support;
};

} // namespace erhe::application
