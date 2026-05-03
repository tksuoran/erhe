#pragma once

#include <memory>

namespace erhe::primitive {
    class Material;
}

namespace editor {

class Make_mesh_config
{
public:
    std::shared_ptr<erhe::primitive::Material> material      {};
    int                                        instance_count{1};
    float                                      instance_gap  {0.5f};
    float                                      object_scale  {1.0f};
    int                                        detail        {4};
};

}
