#ifndef png_loader_none_hpp_erhe_toolkit
#define png_loader_none_hpp_erhe_toolkit

#include "erhe/graphics/texture.hpp"
//#include <filesystem>
#include <memory>

namespace std::filesystem { class path; }

namespace erhe::graphics { class Texture::Create_info; }

namespace erhe::graphics
{

class PNG_loader
{
public:
    PNG_loader() = default;

    PNG_loader(const PNG_loader&) = delete;

    auto operator=(const PNG_loader&)
    -> PNG_loader& = delete;

    PNG_loader(PNG_loader&&) = delete;

    auto operator=(PNG_loader&&)
    -> PNG_loader = delete;

    ~PNG_loader();

    auto open(const std::filesystem::path& path,
              Texture::Create_info&        create_info)
    -> bool;

    auto load(gsl::span<std::byte> transfer_buffer)
    -> bool;

    void close();
};

} // namespace erhe::graphics

#endif

