#pragma once

#include "erhe/components/component.hpp"

namespace erhe::physics
{

class Physics_system
    : public erhe::components::Component
{
public:
    static constexpr const char* c_name = "Physics_system";
    Physics_system();
    virtual ~Physics_system() = default;
};

} // namespace erhe::physics
