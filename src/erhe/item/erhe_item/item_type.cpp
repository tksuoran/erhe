#include "erhe_item/item_type.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe {

namespace {

std::span<const Item_type_info> s_application_types{};

} // anonymous namespace

void Item_type::register_application_types(const std::span<const Item_type_info> types)
{
    uint64_t seen = 0u;
    for (const Item_type_info& type : types) {
        ERHE_VERIFY(type.index >= application_first_index);
        ERHE_VERIFY(type.index < library_first_index);
        ERHE_VERIFY(type.label != nullptr);
        const uint64_t bit = uint64_t{1} << type.index;
        ERHE_VERIFY((seen & bit) == 0u);
        seen |= bit;
    }
    s_application_types = types;
}

auto Item_type::get_application_types() -> std::span<const Item_type_info>
{
    return s_application_types;
}

auto Item_type::label(const uint64_t index) -> const char*
{
    if ((index >= library_first_index) && (index < library_end_index)) {
        return c_bit_labels[index - library_first_index];
    }
    for (const Item_type_info& type : s_application_types) {
        if (type.index == index) {
            return type.label;
        }
    }
    return nullptr;
}

} // namespace erhe
