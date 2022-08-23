#pragma once

#include "erhe/components/components.hpp"

namespace erhe::toolkit {

class Context_window;

}

namespace erhe::application {

class Renderdoc_capture_support
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Renderdoc_capture_support"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Renderdoc_capture_support();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    void start_frame_capture(const erhe::toolkit::Context_window* context_window);
    void end_frame_capture  (const erhe::toolkit::Context_window* context_window);

private:
    bool m_is_initialized{false};
};

} // namespace erhe::application
