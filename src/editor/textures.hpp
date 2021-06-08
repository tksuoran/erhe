#pragma once

#include "erhe/components/component.hpp"
#include <filesystem>

namespace erhe::graphics {
    class Texture;
    class Image_transfer;
}

namespace editor {

class Textures
    : public erhe::components::Component
{
public:
    static constexpr const char* c_name = "Textures";
    Textures();
    ~Textures() override;

    // Implements COmponent
    void connect() override;
    void initialize_component() override;

    auto load(const std::filesystem::path& path)
    -> std::shared_ptr<erhe::graphics::Texture>;

    std::shared_ptr<erhe::graphics::Texture> background_texture;

private:
    std::shared_ptr<erhe::graphics::Image_transfer> m_image_transfer;
};

}
