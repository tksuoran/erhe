#pragma once

#include "operations/operation.hpp"

#include <glm/glm.hpp>

namespace erhe::scene {
    class Light_layer;
}

namespace editor {

class Ambient_light_operation : public Operation
{
public:
    Ambient_light_operation(erhe::scene::Light_layer* layer, glm::vec4 after);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    erhe::scene::Light_layer* m_layer;
    glm::vec4                 m_before{0.0f};
    glm::vec4                 m_after {0.0f};
};

}
