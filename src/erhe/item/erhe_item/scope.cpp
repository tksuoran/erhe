#include "erhe_item/scope.hpp"

namespace erhe {

Scope::Scope()           = default;
Scope::~Scope() noexcept = default;

Scope::Scope(const Scope& other) = default;

Scope& Scope::operator=(const Scope& other) = default;

Scope::Scope(const std::string_view name)
    : Item{name}
{
}

auto Scope::get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type>
{
    return erhe::property::root_owner_type;
}

} // namespace erhe
