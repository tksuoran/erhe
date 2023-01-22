#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/window.hpp"

namespace erhe::application {

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
    ~Window();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    [[nodiscard]] auto get_context_window() const -> erhe::toolkit::Context_window*;
    auto create_gl_window() -> bool;
    void begin_renderdoc_capture();
    void end_renderdoc_capture  ();

private:
    std::unique_ptr<erhe::toolkit::Context_window> m_context_window;
};

extern Window* g_window;

} // namespace erhe::application
