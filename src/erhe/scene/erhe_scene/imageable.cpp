#include "erhe_scene/imageable.hpp"

namespace erhe::scene {

Imageable::Imageable()           = default;
Imageable::~Imageable() noexcept = default;

Imageable::Imageable(const Imageable& src) = default;

Imageable& Imageable::operator=(const Imageable& src) = default;

Imageable::Imageable(const std::string_view name)
    : Item{name}
{
}

Imageable::Imageable(const Imageable& src, erhe::for_clone)
    : Imageable{src}
{
}

} // namespace erhe::scene
