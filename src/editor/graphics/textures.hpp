#pragma once

#include <filesystem>

namespace erhe::graphics {
    class Texture;
}

namespace editor {

class Textures
{
public:
    Textures ();
    ~Textures() noexcept override;

    // Public API
    [[nodiscard]] auto load(const std::filesystem::path& path) -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background;
};

}
