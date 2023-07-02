#pragma once

#include <filesystem>

namespace erhe::graphics {
    class Texture;
}

namespace editor {

class Image_transfer;

class Textures
{
public:
    Textures();

    [[nodiscard]] auto load(const std::filesystem::path& path) -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background;

private:
    std::shared_ptr<Image_transfer> m_image_transfer;
};

} // namespace editor
