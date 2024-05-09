#include "erhe_graphics/png_loader.hpp"

namespace erhe::graphics
{

PNG_loader::PNG_loader() = default;

PNG_loader::~PNG_loader() noexcept
{
    close();
}

auto PNG_loader::open(const std::filesystem::path&, Image_info&) -> bool
{
    return false;
}

auto PNG_loader::load(std::span<std::byte>) -> bool
{
    return false;
}

void PNG_loader::close()
{
}

} // namespace erhe::graphics

