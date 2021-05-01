#include "erhe/graphics/png_loader.hpp"
#include "erhe/graphics/log.hpp"
#include <fstream>

namespace erhe::graphics
{

PNG_loader::~PNG_loader()
{
    close();
}

void PNG_loader::close()
{
}

auto PNG_loader::open(const std::filesystem::path& path,
                      Texture::Create_info&        create_info)
-> bool
{
    static_cast<void>(path);
    static_cast<void>(create_info);
    return false;
}

auto PNG_loader::load(gsl::span<std::byte> transfer_buffer)
-> bool
{
    static_cast<void>(transfer_buffer);
    return false;
}

} // namespace erhe::graphics

