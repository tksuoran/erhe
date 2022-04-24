#pragma once

#include "erhe/components/components.hpp"
#include "erhe/toolkit/window.hpp"

namespace erhe::application {

class Application
    : public erhe::components::Component
    , public std::enable_shared_from_this<Application>
{
public:
    static constexpr std::string_view c_label{"Application"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Application ();
    ~Application() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    auto initialize_components(int argc, char** argv) -> bool;
    void run();

    void component_initialization_complete(const bool initialization_succeeded);

private:
    erhe::components::Components m_components;
};

} // namespace erhe::application
