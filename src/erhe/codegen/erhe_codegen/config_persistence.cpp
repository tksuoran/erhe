#include "erhe_codegen/config_persistence.hpp"

namespace erhe::codegen {

namespace {

Config_persistence s_config_persistence{Config_persistence::read_write};

}

void set_config_persistence(const Config_persistence persistence)
{
    s_config_persistence = persistence;
}

auto get_config_persistence() -> Config_persistence
{
    return s_config_persistence;
}

} // namespace erhe::codegen
