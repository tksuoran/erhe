#pragma once

#include "erhe/components/components.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/scene/scene_message.hpp"

namespace erhe::scene
{

class Scene_message_bus
    : public erhe::components::Component
    , public erhe::message_bus::Message_bus<erhe::scene::Scene_message>
{
public:
    static constexpr std::string_view c_type_name{"Scene_message_bus"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Scene_message_bus();
    ~Scene_message_bus() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void initialize_component() override;
};

extern Scene_message_bus* g_scene_message_bus;

} // namespace erhe::application
