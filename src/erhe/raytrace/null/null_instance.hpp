#pragma once

#include "erhe/raytrace/iinstance.hpp"

#include <glm/glm.hpp>

#include <string>

namespace erhe::raytrace
{

class Null_scene;

class Null_instance
    : public IInstance
{
public:
    explicit Null_instance(const std::string_view debug_label);
    ~Null_instance() override;

    // Implements IInstance
    void commit       () override;
    void set_transform(const glm::mat4 transform) override;
    void set_scene    (IScene* scene) override;
    void set_user_data(void* ptr) override;
    [[nodiscard]] auto get_user_data() -> void* override;
    [[nodiscard]] auto debug_label() const -> std::string_view override;

private:
    glm::mat4   m_transform{1.0f};
    IScene*     m_scene    {nullptr};
    void*       m_user_data{nullptr};
    std::string m_debug_label;
};

}
