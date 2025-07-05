#pragma once

#include <cstdint>

namespace erhe::graphics {

class Draw_primitives_indirect_command
{
public:
    uint32_t count;
    uint32_t instance_count;
    uint32_t first;
    uint32_t base_instance;
};

class Draw_indexed_primitives_indirect_command
{
public:
    uint32_t index_count;
    uint32_t instance_count;
    uint32_t first_index;
    uint32_t base_vertex;
    uint32_t base_instance;
};

} // namespace erhe::graphics
