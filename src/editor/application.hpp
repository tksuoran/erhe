#pragma once

#include "erhe/components/component.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/window.hpp"

#include <renderdoc_app.h>

namespace editor {

class Application
    : public erhe::components::Component
    , public std::enable_shared_from_this<Application>
{
public:
    static constexpr std::string_view c_name{"Application"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Application ();
    ~Application() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    auto initialize_components(int argc, char** argv) -> bool;
    void run();

    void component_initialization_complete(const bool initialization_succeeded);

private:

    erhe::components::Components m_components;
};

}
