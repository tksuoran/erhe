#pragma once

#include "erhe/application/application_view.hpp"
#include "erhe/components/components.hpp"

namespace editor
{


class Editor_view_client
    : public erhe::application::View_client
    , public erhe::components::Component
    , public erhe::components::IUpdate_fixed_step
{
public:
    static constexpr std::string_view c_type_name{"editor::View_client"};
    static constexpr uint32_t         c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Editor_view_client ();
    ~Editor_view_client() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements erhe::components::IUpdate_fixed_step
    void update_fixed_step(const erhe::components::Time_context&) override;

    // Implements erhe::application::View_client
    void update(erhe::application::View& view) override;
};

extern Editor_view_client* g_editor_view_client;

} // namespace editor

