#pragma once

#include "editor_message.hpp"

#include "erhe/components/components.hpp"
#include "erhe/message_bus/message_bus.hpp"

namespace editor
{

class Editor_message_bus
    : public erhe::components::Component
    , public erhe::message_bus::Message_bus<Editor_message>
{
public:
    static constexpr std::string_view c_type_name{"Editor_bus"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Editor_message_bus();
    ~Editor_message_bus() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void initialize_component       () override;
};

extern Editor_message_bus* g_editor_message_bus;

} // namespace editor
