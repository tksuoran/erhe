#pragma once

#include "erhe_gl/wrapper_enums.hpp"
#include <type_traits>

namespace gl
{

template<typename Enum>
struct Enable_bit_mask_operators
{
    static const bool enable = false;
};

template<typename Enum>
constexpr auto
operator |(Enum lhs, Enum rhs)
-> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
{
    using underlying = typename std::underlying_type<Enum>::type;
    return static_cast<Enum> (static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

template<typename Enum>
constexpr auto
operator &(Enum lhs, Enum rhs)
-> typename std::enable_if<Enable_bit_mask_operators<Enum>::enable, Enum>::type
{
    using underlying = typename std::underlying_type<Enum>::type;
    return static_cast<Enum> (static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

template<> struct Enable_bit_mask_operators<Buffer_storage_mask   > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Clear_buffer_mask     > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Map_buffer_access_mask> { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Memory_barrier_mask   > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Sync_object_mask      > { static const bool enable = true; };
template<> struct Enable_bit_mask_operators<Use_program_stage_mask> { static const bool enable = true; };

} // namespace erhe::graphics
