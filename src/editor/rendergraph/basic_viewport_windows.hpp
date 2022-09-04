#pragma once

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/components/components.hpp"

#include <memory>

namespace editor
{

class Basic_viewport_window;

class Basic_viewport_windows
    : public erhe::components::Component
    , public erhe::application::Rendergraph_node
{
public:
    static constexpr std::string_view c_type_name{"Basic_viewport_window"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Basic_viewport_windows ();
    ~Basic_viewport_windows() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

private:
    std::vector<std::shared_ptr<Basic_viewport_window>> m_viewport_windows;
};

} // namespace editor
