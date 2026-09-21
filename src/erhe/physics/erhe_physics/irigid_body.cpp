#include "erhe_physics/irigid_body.hpp"

namespace erhe::physics {

namespace {

constexpr erhe::property::Enum_entry c_motion_mode_entries[] = {
    {"None",                   static_cast<int32_t>(Motion_mode::e_none)},
    {"Static",                 static_cast<int32_t>(Motion_mode::e_static)},
    {"Kinematic Non-Physical", static_cast<int32_t>(Motion_mode::e_kinematic_non_physical)},
    {"Kinematic Physical",     static_cast<int32_t>(Motion_mode::e_kinematic_physical)},
    {"Dynamic",                static_cast<int32_t>(Motion_mode::e_dynamic)},
};

} // anonymous namespace

const erhe::property::Enum_info c_motion_mode_enum_info{"Motion_mode", c_motion_mode_entries};

}
