#include "scene/node_ik_settings.hpp"

namespace editor {

Ik_settings::Ik_settings(const Ik_settings&) = default;
Ik_settings::~Ik_settings() noexcept         = default;

Ik_settings::Ik_settings(const std::string_view name)
    : Item{name}
{
}

Ik_settings::Ik_settings(const Ik_settings& src, erhe::for_clone)
    : Item{src, erhe::for_clone{}}
    , data{src.data}
{
}

} // namespace editor
